#include "common.h"
#include <nds/arm9/video.h>
#include <nds/system.h>
#include <libtwl/gfx/gfx.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/mem/memExtern.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include "App.h"
#include "dldiIpc.h"
#include "usb/UsbStatus.h"

static rtos_event_t sVblankEvent;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVblankEvent);
}

/// @brief Puts a 16 bpp framebuffer on each screen, read straight from VRAM.
static void initializeDisplay()
{
    powerOn(POWER_ALL_2D);

    videoSetMode(MODE_FB0);
    videoSetModeSub(MODE_FB2);

    vramSetBankA(VRAM_A_LCD);
    vramSetBankC(VRAM_C_LCD);

    REG_MASTER_BRIGHT = 0;
    REG_MASTER_BRIGHT_SUB = 0;
}

int main(int argc, char* argv[])
{
    REG_DISPCNT = 0;
    REG_DISPCNT_SUB = 0;

    mem_setDsCartridgeCpu(EXMEMCNT_SLOT1_CPU_ARM7);

    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    rtos_createEvent(&sVblankEvent);
    UsbStatusInit();

    while (ipc_getArm7SyncBits() != 7);

    bool cardReady = dldi_init();

    ipc_setArm9SyncBits(6);

    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);

    initializeDisplay();

    App app;
    app.Run(sVblankEvent, cardReady);
    return 0;
}
