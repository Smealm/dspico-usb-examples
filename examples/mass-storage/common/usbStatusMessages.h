#pragma once
#include <nds/ndstypes.h>

/// @brief IPC channel the arm7 uses to tell the arm9 what the USB link is doing.
///        One message per event, the payload is a UsbStatusEvent.
#define IPC_CHANNEL_USB_STATUS  21

/// @brief IPC channel the arm9 uses to send UsbControlCommand messages to the arm7.
#define IPC_CHANNEL_USB_CONTROL 22

/// @brief IPC channels the arm7 uses to report host traffic, in bytes.
#define IPC_CHANNEL_USB_TRAFFIC_READ  23
#define IPC_CHANNEL_USB_TRAFFIC_WRITE 24

/// @brief USB link events, sent from the arm7 to the arm9.
enum UsbStatusEvent : u32
{
    /// @brief The host enumerated the device and mounted the drive.
    USB_STATUS_MOUNTED,
    /// @brief The cable was unplugged or the host reset the device.
    USB_STATUS_UNMOUNTED,
    /// @brief The host suspended the device.
    USB_STATUS_SUSPENDED,
    /// @brief The host resumed the device.
    USB_STATUS_RESUMED,
    /// @brief The host ejected the medium (START STOP UNIT with start = 0).
    USB_STATUS_MEDIA_EJECTED,
    /// @brief The host loaded the medium again.
    USB_STATUS_MEDIA_LOADED
};

/// @brief Requests the arm9 can send to the arm7.
enum UsbControlCommand : u32
{
    /// @brief data = 0 disables, 1 enables write protection.
    USB_CONTROL_SET_WRITE_PROTECT = 0
};

/// @brief Packs a control command and its argument into a single FIFO message.
#define USB_CONTROL_MESSAGE(command, argument) (((u32)(command) << 1) | ((u32)(argument) & 1))
#define USB_CONTROL_GET_COMMAND(message)       ((u32)(message) >> 1)
#define USB_CONTROL_GET_ARGUMENT(message)      ((u32)(message) & 1)
