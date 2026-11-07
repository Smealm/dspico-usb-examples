#pragma once
#include "common.h"
#include "usbStatusMessages.h"

/// @brief What the arm7 last reported about the USB link.
struct UsbLinkState
{
    volatile bool mounted;
    volatile bool suspended;
    volatile bool mediaPresent;
    volatile u32 eventSeq;
    volatile u32 lastEvent;
};

extern UsbLinkState gUsbLink;

/// @brief Registers the status channel handler. Call once after ipc_initFifoSystem.
void UsbStatusInit();

/// @brief Asks the arm7 to accept or reject writes from the host.
void UsbSendWriteProtect(bool enabled);
