#include "common.h"
#include <nds/disc_io.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/sound/soundChannel.h>
#include <libtwl/timer/timer.h>
#include <libtwl/sound/sound.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/sys/sysPower.h>
#include <libtwl/sio/sioRtc.h>
#include <libtwl/sio/sio.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memSwap.h>
#include <libtwl/i2c/i2cMcu.h>
#include <libtwl/spi/spiPmic.h>
#include "ipcServices/DldiIpcService.h"
#include "ExitMode.h"
#include "Arm7State.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "usbStatusMessages.h"

static DldiIpcService sDldiIpcService;

rtos_mutex_t gCardMutex;

static rtos_event_t sVBlankEvent;
static ExitMode sExitMode;
static Arm7State sState;
static volatile u8 sMcuIrqFlag = false;

static u32 sSdBlockCount;
static u8 sSector0Buffer[512] alignas(4);

// Host-visible state. The arm9 UI mirrors all of it over IPC.
static volatile bool sWriteProtected = false;
static volatile bool sMediaPresent = true;
static volatile u32 sHostReadBytes = 0;
static volatile u32 sHostWriteBytes = 0;
static u32 sLastReportedReadBytes = 0;
static u32 sLastReportedWriteBytes = 0;
static u32 sTrafficReportFrame = 0;

static rtos_thread_t sUsbThread;
static u32 sUsbThreadStack[512];

extern FN_MEDIUM_READSECTORS _DLDI_readSectors_ptr;
extern FN_MEDIUM_WRITESECTORS _DLDI_writeSectors_ptr;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVBlankEvent);
}

static void mcuIrq(u32 irq2Mask)
{
    sMcuIrqFlag = true;
}

static void checkMcuIrq(void)
{
    // mcu only exists in DSi mode
    if (isDSiMode())
    {
        // check and ack the flag atomically
        if (mem_swapByte(false, &sMcuIrqFlag))
        {
            // check the irq mask
            u32 irqMask = mcu_getIrqMask();
            if (irqMask & MCU_IRQ_RESET)
            {
                // power button was released
                sExitMode = ExitMode::Reset;
                sState = Arm7State::ExitRequested;
            }
            else if (irqMask & MCU_IRQ_POWER_OFF)
            {
                // power button was held long to trigger a power off
                sExitMode = ExitMode::PowerOff;
                sState = Arm7State::ExitRequested;
            }
        }
    }
}

static void notifyArm9(UsbStatusEvent event)
{
    ipc_sendFifoMessage(IPC_CHANNEL_USB_STATUS, (u32)event);
}

static void usbControlHandler(u32 channel, u32 data, void* arg)
{
    (void)channel;
    (void)arg;
    switch (USB_CONTROL_GET_COMMAND(data))
    {
        case USB_CONTROL_SET_WRITE_PROTECT:
            sWriteProtected = USB_CONTROL_GET_ARGUMENT(data) != 0;
            break;
        default:
            break;
    }
}

static void initializeVBlankIrq()
{
    rtos_createEvent(&sVBlankEvent);
    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);
}

static void usbThreadMain(void* arg)
{
    while (true)
    {
        tud_task();
    }
}

// Based on https://github.com/asiekierka/nrio-usb-disk/blob/main/source/msc.c msc_find_block_count by Asie
// Note: This might not work correctly in some cases. It would be better if the DSpico would expose the actual SD capacity.
static u32 findSdCardBlockCount()
{
    rtos_lockMutex(&gCardMutex);
    _DLDI_readSectors_ptr(0, 1, sSector0Buffer);
    rtos_unlockMutex(&gCardMutex);

    u16 footer = *(u16*)(sSector0Buffer + 510);
    if (footer == 0xAA55)
    {
        u8 bootOpcode = sSector0Buffer[0];
        if (bootOpcode == 0xEB || bootOpcode == 0xE9 || bootOpcode == 0xE8)
        {
            if (!memcmp(sSector0Buffer + 54, "FAT", 3) || !memcmp(sSector0Buffer + 82, "FAT32   ", 8))
            {
                u32 totalSectors = *(u32*)(sSector0Buffer + 32);
                if (totalSectors < 0x10000)
                {
                    totalSectors = sSector0Buffer[19] | (sSector0Buffer[20] << 8);
                }
                return totalSectors;
            }
        }

        u32 blockCount = 0;
        for (u32 tableEntry = 0x1BE; tableEntry < 0x1FE; tableEntry += 16)
        {
            u32 pStart = *(u16*)(sSector0Buffer + tableEntry + 8) | (*(u16*)(sSector0Buffer + tableEntry + 10) << 16);
            u32 pCount = *(u16*)(sSector0Buffer + tableEntry + 12) | (*(u16*)(sSector0Buffer + tableEntry + 14) << 16);
            u32 pEnd = pStart + pCount;
            if (pEnd > blockCount)
            {
                blockCount = pEnd;
            }
        }

        return blockCount;
    }

    return 0;
}

static void initializeArm7()
{
    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();
    ipc_setChannelHandler(IPC_CHANNEL_USB_CONTROL, usbControlHandler, nullptr);

    rtos_createMutex(&gCardMutex);

    // clear sound registers
    dmaFillWords(0, (void*)0x04000400, 0x100);

    pmic_setAmplifierEnable(true);
    sys_setSoundPower(true);

    readUserSettings();
    pmic_setPowerLedBlink(PMIC_CONTROL_POWER_LED_BLINK_NONE);

    sio_setGpioSiIrq(false);
    sio_setGpioMode(RCNT0_L_MODE_GPIO);

    rtc_init();

    sDldiIpcService.Start();

    snd_setMasterVolume(127);
    snd_setMasterEnable(true);

    initializeVBlankIrq();

    if (isDSiMode())
    {
        rtos_setIrq2Func(RTOS_IRQ2_MCU, mcuIrq);
        rtos_enableIrq2Mask(RTOS_IRQ2_MCU);
    }

    ipc_setArm7SyncBits(7);

    while (ipc_getArm9SyncBits() != 6)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);
    }

    sSdBlockCount = findSdCardBlockCount();

    tusb_rhport_init_t dev_init =
    {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    rtos_createThread(&sUsbThread, 3, usbThreadMain, NULL, sUsbThreadStack, sizeof(sUsbThreadStack));
    rtos_wakeupThread(&sUsbThread);
}

static void reportTraffic()
{
    if (++sTrafficReportFrame < 10)
        return;
    sTrafficReportFrame = 0;

    u32 readBytes = sHostReadBytes;
    if (readBytes != sLastReportedReadBytes)
    {
        ipc_sendFifoMessage(IPC_CHANNEL_USB_TRAFFIC_READ, readBytes - sLastReportedReadBytes);
        sLastReportedReadBytes = readBytes;
    }

    u32 writeBytes = sHostWriteBytes;
    if (writeBytes != sLastReportedWriteBytes)
    {
        ipc_sendFifoMessage(IPC_CHANNEL_USB_TRAFFIC_WRITE, writeBytes - sLastReportedWriteBytes);
        sLastReportedWriteBytes = writeBytes;
    }
}

static void updateArm7IdleState()
{
    checkMcuIrq();

    if (sState == Arm7State::ExitRequested)
    {
        snd_setMasterVolume(0); // mute sound
    }
}

static bool performExit(ExitMode exitMode)
{
    switch (exitMode)
    {
        case ExitMode::Reset:
        {
            mcu_setWarmBootFlag(true);
            mcu_hardReset();
            break;
        }
        case ExitMode::PowerOff:
        {
            pmic_shutdown();
            break;
        }
    }

    while (true); // wait infinitely for exit
}

static void updateArm7ExitRequestedState()
{
    performExit(sExitMode);
}

static void updateArm7()
{
    reportTraffic();

    switch (sState)
    {
        case Arm7State::Idle:
        {
            updateArm7IdleState();
            break;
        }
        case Arm7State::ExitRequested:
        {
            updateArm7ExitRequestedState();
            break;
        }
    }
}

int main()
{
    sState = Arm7State::Idle;
    initializeArm7();

    while (true)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);
        updateArm7();
    }

    return 0;
}

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void) lun;

    const char vid[] = "DSpico";
    const char pid[] = "microSD Card";
    const char rev[] = "1.0";

    memcpy(vendor_id  , vid, strlen(vid));
    memcpy(product_id , pid, strlen(pid));
    memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void) lun;
    // An ejected medium stays absent until the host loads it again.
    return sMediaPresent;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
    (void) lun;

    *block_count = sMediaPresent ? sSdBlockCount : 0;
    *block_size  = 512;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void) lun;
    (void) power_condition;

    if (load_eject && sMediaPresent != start)
    {
        sMediaPresent = start;
        notifyArm9(start ? USB_STATUS_MEDIA_LOADED : USB_STATUS_MEDIA_EJECTED);
    }
    return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    (void) lun;

    if (!sMediaPresent)
    {
        return -1;
    }

    if (offset != 0 || (bufsize % 512) != 0)
    {
        return -1;
    }

    rtos_lockMutex(&gCardMutex);
    _DLDI_readSectors_ptr(lba, bufsize / 512, buffer);
    rtos_unlockMutex(&gCardMutex);

    sHostReadBytes += bufsize;
    return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void) lun;
    // Writing to the card while the launcher (or the user) wants it untouched
    // fails at the SCSI layer, so the PC reports a write-protected disk.
    return !sWriteProtected;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    (void) lun;

    if (!sMediaPresent)
    {
        return -1;
    }

    if (sWriteProtected)
    {
        tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
        return -1;
    }

    if (offset != 0 || (bufsize % 512) != 0)
    {
        return -1;
    }

    rtos_lockMutex(&gCardMutex);
    _DLDI_writeSectors_ptr(lba, bufsize / 512, buffer);
    rtos_unlockMutex(&gCardMutex);

    sHostWriteBytes += bufsize;
    return (int32_t)bufsize;
}

// Invoked when the host mounts or unmounts the drive
void tud_mount_cb(void)
{
    sMediaPresent = true;
    notifyArm9(USB_STATUS_MOUNTED);
}

void tud_umount_cb(void)
{
    notifyArm9(USB_STATUS_UNMOUNTED);
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    notifyArm9(USB_STATUS_SUSPENDED);
}

void tud_resume_cb(void)
{
    notifyArm9(USB_STATUS_RESUMED);
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
    // read10 & write10 has their own callback and MUST not be handled here

    void const* response = NULL;
    int32_t resplen = 0;

    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0])
    {
        default:
        {
            // Set Sense = Invalid Command Operation
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

            // negative means error -> tinyusb could stall and/or response with failed status
            resplen = -1;
            break;
        }
    }

    // return resplen must not larger than bufsize
    if (resplen > bufsize)
    {
        resplen = bufsize;
    }

    if (response && (resplen > 0))
    {
        if(in_xfer)
        {
            memcpy(buffer, response, (size_t)resplen);
        }
        else
        {
            // SCSI output
        }
    }

    return (int32_t)resplen;
}
