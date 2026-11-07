#include "usb/UsbStatus.h"
#include <libtwl/ipc/ipcFifoSystem.h>
#include "Activity.h"

UsbLinkState gUsbLink = { false, false, true, 0, 0 };

static void OnUsbStatusMessage(u32 channel, u32 data, void* arg)
{
    (void)channel;
    (void)arg;

    switch ((UsbStatusEvent)data)
    {
        case USB_STATUS_MOUNTED:
            gUsbLink.mounted = true;
            gUsbLink.suspended = false;
            gUsbLink.mediaPresent = true;
            break;
        case USB_STATUS_UNMOUNTED:
            gUsbLink.mounted = false;
            gUsbLink.suspended = false;
            gUsbLink.mediaPresent = true;
            break;
        case USB_STATUS_SUSPENDED:
            gUsbLink.suspended = true;
            break;
        case USB_STATUS_RESUMED:
            gUsbLink.suspended = false;
            break;
        case USB_STATUS_MEDIA_EJECTED:
            gUsbLink.mediaPresent = false;
            break;
        case USB_STATUS_MEDIA_LOADED:
            gUsbLink.mediaPresent = true;
            break;
        default:
            return;
    }

    gUsbLink.lastEvent = data;
    gUsbLink.eventSeq++;
}

static void OnReadTrafficMessage(u32 channel, u32 data, void* arg)
{
    (void)channel;
    (void)arg;
    gHostReadBytes += data;
}

static void OnWriteTrafficMessage(u32 channel, u32 data, void* arg)
{
    (void)channel;
    (void)arg;
    gHostWriteBytes += data;
}

void UsbStatusInit()
{
    ipc_setChannelHandler(IPC_CHANNEL_USB_STATUS, OnUsbStatusMessage, nullptr);
    ipc_setChannelHandler(IPC_CHANNEL_USB_TRAFFIC_READ, OnReadTrafficMessage, nullptr);
    ipc_setChannelHandler(IPC_CHANNEL_USB_TRAFFIC_WRITE, OnWriteTrafficMessage, nullptr);
}

void UsbSendWriteProtect(bool enabled)
{
    ipc_sendFifoMessage(IPC_CHANNEL_USB_CONTROL,
        USB_CONTROL_MESSAGE(USB_CONTROL_SET_WRITE_PROTECT, enabled ? 1 : 0));
}
