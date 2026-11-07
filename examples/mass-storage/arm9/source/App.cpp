#include "App.h"
#include <cstdio>
#include <cstring>
#include "Activity.h"
#include "usb/UsbStatus.h"
#include "usbStatusMessages.h"

namespace
{
constexpr u16 ColorBackground = Rgb15(10, 13, 18);
constexpr u16 ColorPanel = Rgb15(21, 27, 36);
constexpr u16 ColorPanelSelected = Rgb15(36, 48, 64);
constexpr u16 ColorEdge = Rgb15(46, 59, 76);
constexpr u16 ColorText = Rgb15(234, 240, 248);
constexpr u16 ColorDim = Rgb15(136, 149, 166);
constexpr u16 ColorReady = Rgb15(74, 198, 134);
constexpr u16 ColorWaiting = Rgb15(98, 162, 242);
constexpr u16 ColorWarning = Rgb15(242, 180, 76);
constexpr u16 ColorBad = Rgb15(234, 100, 100);

alignas(32) u16 sTopFramebuffer[256 * 192];
alignas(32) u16 sBottomFramebuffer[256 * 192];

const char* const kMenuLabels[App::MenuItemCount] =
{
    "Write protection",
    "Rescan card",
    "Reset counters",
    "About"
};

const char* const kMenuHints[App::MenuItemCount] =
{
    "The PC cannot change files",
    "Re-read free space and format",
    "Clear this session's traffic",
    "Version and USB identity"
};

constexpr int kSpeedHistoryLength = 40;
constexpr int kSpeedSampleFrames = 15;

void FormatBytes(char* out, size_t size, u64 bytes)
{
    if (bytes < 1024)
    {
        snprintf(out, size, "%u B", (unsigned)bytes);
        return;
    }
    if (bytes < 1024ULL * 1024)
    {
        snprintf(out, size, "%u KB", (unsigned)((bytes + 512) / 1024));
        return;
    }
    if (bytes < 1024ULL * 1024 * 1024)
    {
        u64 megabytes = bytes / (1024ULL * 1024);
        unsigned fraction = (unsigned)(((bytes % (1024ULL * 1024)) * 10) / (1024ULL * 1024));
        snprintf(out, size, "%u.%u MB", (unsigned)megabytes, fraction);
        return;
    }

    u64 gigabytes = bytes / (1024ULL * 1024 * 1024);
    unsigned fraction = (unsigned)(((bytes % (1024ULL * 1024 * 1024)) * 100) / (1024ULL * 1024 * 1024));
    snprintf(out, size, "%u.%02u GB", (unsigned)gigabytes, fraction);
}

void FormatSpeed(char* out, size_t size, u32 kilobytesPerSecond)
{
    if (kilobytesPerSecond == 0)
    {
        snprintf(out, size, "idle");
        return;
    }

    unsigned megabytes = kilobytesPerSecond / 1024;
    unsigned fraction = ((kilobytesPerSecond % 1024) * 100) / 1024;
    snprintf(out, size, "%u.%02u MB/s", megabytes, fraction);
}
}

App::App()
    : _top(sTopFramebuffer, (volatile u16*)0x06000000)
    , _bottom(sBottomFramebuffer, (volatile u16*)0x06200000)
    , _card{}
    , _cardReady(false)
    , _writeProtected(false)
    , _showAbout(false)
    , _selected(0)
    , _frame(0)
    , _lastTrafficFrame(0)
    , _lastTrafficWasWrite(false)
    , _previousReadBytes(0)
    , _previousWriteBytes(0)
    , _seenUsbEvent(0)
    , _speedHistory{}
    , _speedHead(0)
    , _sampleFrames(0)
    , _sampledReadBytes(0)
    , _sampledWriteBytes(0)
    , _speedKbPerSecond(0)
    , _toast{}
    , _toastColor(ColorText)
    , _toastFrames(0)
{
}

void App::Run(rtos_event_t& vblankEvent, bool cardReady)
{
    _cardReady = cardReady;
    if (!_cardReady)
    {
        Toast("Card interface not available", ColorBad, 300);
    }
    else if (!ReadCardInfo(_card))
    {
        Toast("Card could not be read", ColorBad, 300);
    }

    while (true)
    {
        UpdateUsbEvents();
        HandleInput();
        UpdateTraffic();

        Render();

        rtos_waitEvent(&vblankEvent, true, true);
        _top.Blit();
        _bottom.Blit();

        _frame++;
        if (_toastFrames > 0)
            _toastFrames--;
    }
}

void App::UpdateUsbEvents()
{
    u32 sequence = gUsbLink.eventSeq;
    if (sequence == _seenUsbEvent)
        return;

    _seenUsbEvent = sequence;
    switch ((UsbStatusEvent)gUsbLink.lastEvent)
    {
        case USB_STATUS_MOUNTED:
            Toast("PC connected", ColorReady);
            break;
        case USB_STATUS_UNMOUNTED:
            Toast("PC disconnected", ColorWaiting);
            break;
        case USB_STATUS_SUSPENDED:
            Toast("The PC suspended the drive", ColorWarning);
            break;
        case USB_STATUS_RESUMED:
            Toast("The PC resumed the drive", ColorReady);
            break;
        case USB_STATUS_MEDIA_EJECTED:
            Toast("Ejected by the PC", ColorWarning, 300);
            break;
        case USB_STATUS_MEDIA_LOADED:
            Toast("Drive mounted again", ColorReady);
            break;
    }
}

void App::HandleInput()
{
    _input.Update();

    if (_showAbout)
    {
        if (_input.Pressed(Input::A) || _input.Pressed(Input::B))
            _showAbout = false;
        return;
    }

    if (_input.Pressed(Input::Up) && _selected > 0)
        _selected--;
    if (_input.Pressed(Input::Down) && _selected < MenuItemCount - 1)
        _selected++;

    if (_input.Pressed(Input::A))
        Activate((MenuItem)_selected);
    if (_input.Pressed(Input::Start))
        Activate(MenuWriteProtect);
    if (_input.Pressed(Input::Select))
        Activate(MenuRescan);
}

void App::UpdateTraffic()
{
    u64 readBytes = gHostReadBytes;
    u64 writeBytes = gHostWriteBytes;

    if (readBytes != _previousReadBytes || writeBytes != _previousWriteBytes)
    {
        _lastTrafficWasWrite = writeBytes != _previousWriteBytes;
        _lastTrafficFrame = _frame;
        _previousReadBytes = readBytes;
        _previousWriteBytes = writeBytes;
    }

    if (++_sampleFrames < kSpeedSampleFrames)
        return;

    u64 bytes = (readBytes - _sampledReadBytes) + (writeBytes - _sampledWriteBytes);
    _sampledReadBytes = readBytes;
    _sampledWriteBytes = writeBytes;
    _sampleFrames = 0;

    // Four samples per second, so KB/s is four times the bytes seen per sample.
    _speedKbPerSecond = (u32)((bytes / 1024) * 4);

    u32 sample = _speedKbPerSecond / 16;
    if (sample > 255)
        sample = 255;
    _speedHistory[_speedHead] = sample;
    _speedHead = (_speedHead + 1) % kSpeedHistoryLength;
}

void App::Activate(MenuItem item)
{
    switch (item)
    {
        case MenuWriteProtect:
            _writeProtected = !_writeProtected;
            UsbSendWriteProtect(_writeProtected);
            Toast(_writeProtected ? "Write protection is on" : "Write protection is off",
                _writeProtected ? ColorWarning : ColorReady);
            break;

        case MenuRescan:
            if (!_cardReady)
            {
                Toast("Card interface not available", ColorBad);
                break;
            }
            Toast(ReadCardInfo(_card) ? "Card scanned" : "Card scan failed",
                _card.valid ? ColorText : ColorBad);
            break;

        case MenuResetCounters:
            ActivityReset();
            _sampledReadBytes = 0;
            _sampledWriteBytes = 0;
            _previousReadBytes = 0;
            _previousWriteBytes = 0;
            _speedKbPerSecond = 0;
            for (int i = 0; i < kSpeedHistoryLength; i++)
                _speedHistory[i] = 0;
            Toast("Counters cleared", ColorText);
            break;

        case MenuAbout:
            _showAbout = true;
            break;

        default:
            break;
    }
}

void App::Toast(const char* text, u16 color, int frames)
{
    snprintf(_toast, sizeof(_toast), "%s", text);
    _toastColor = color;
    _toastFrames = frames;
}

App::LinkState App::GetLinkState() const
{
    if (!gUsbLink.mounted)
        return LinkWaiting;
    if (gUsbLink.suspended)
        return LinkSuspended;
    if (!gUsbLink.mediaPresent)
        return LinkEjected;
    if (_frame - _lastTrafficFrame < 45)
        return LinkActive;
    return LinkReady;
}

const char* App::MenuValue(int index) const
{
    if (index == MenuWriteProtect)
        return _writeProtected ? "ON" : "OFF";
    return "";
}

void App::Render()
{
    LinkState state = GetLinkState();

    _top.Clear(ColorBackground);
    _bottom.Clear(ColorBackground);
    RenderTop(state);

    if (_showAbout)
        RenderAbout();
    else
        RenderBottom();
}

void App::RenderTop(LinkState state)
{
    const char* statusText;
    u16 statusColor;

    switch (state)
    {
        case LinkWaiting:
            statusText = "WAITING FOR PC";
            statusColor = ColorWaiting;
            break;
        case LinkSuspended:
            statusText = "SUSPENDED BY PC";
            statusColor = ColorWarning;
            break;
        case LinkEjected:
            statusText = "EJECTED BY PC";
            statusColor = ColorWarning;
            break;
        case LinkActive:
            statusText = _lastTrafficWasWrite ? "PC IS WRITING" : "PC IS READING";
            statusColor = ColorReady;
            break;
        default:
            statusText = _writeProtected ? "CONNECTED - WRITE PROTECTED" : "CONNECTED - READY";
            statusColor = _writeProtected ? ColorWarning : ColorReady;
            break;
    }

    _top.FillRect(0, 0, 256, 28, ColorPanel);
    _top.FillRect(0, 0, 4, 28, statusColor);
    _top.Text(10, 6, "MOUNT microSD", ColorText, 2);

    _top.FillRect(0, 28, 256, 18, ColorPanel);
    _top.FillRect(0, 28, 4, 18, statusColor);
    _top.Text(10, 33, statusText, statusColor);
    if (state == LinkActive)
    {
        int pulse = 2 + (int)((_frame >> 2) & 3);
        _top.FillRect(236, 37 - pulse / 2, pulse, pulse, statusColor);
    }

    char capacity[24];
    char freeSpace[24];
    char clusterSize[24];
    FormatBytes(capacity, sizeof(capacity), _card.totalBytes);
    if (_card.freeKnown)
        FormatBytes(freeSpace, sizeof(freeSpace), _card.freeBytes);
    else
        snprintf(freeSpace, sizeof(freeSpace), "not known");
    FormatBytes(clusterSize, sizeof(clusterSize),
        (u64)_card.sectorsPerCluster * _card.sectorSize);

    _top.FillRect(8, 50, 240, 66, ColorPanel);
    _top.DrawRect(8, 50, 240, 66, ColorEdge);
    _top.Text(14, 54, "CARD", ColorDim);

    _top.Text(14, 68, "Capacity", ColorDim);
    _top.TextRight(242, 68, _card.valid ? capacity : "unknown", ColorText);
    _top.Text(14, 80, "Free", ColorDim);
    _top.TextRight(242, 80, _card.valid ? freeSpace : "unknown", ColorText);
    _top.Text(14, 92, "Filesystem", ColorDim);
    _top.TextRight(242, 92, _card.valid ? _card.fsType : "unknown", ColorText);
    _top.Text(14, 104, "Cluster size", ColorDim);
    _top.TextRight(242, 104, _card.valid ? clusterSize : "unknown", ColorText);

    char readTotal[24];
    char writeTotal[24];
    char speed[24];
    FormatBytes(readTotal, sizeof(readTotal), gHostReadBytes);
    FormatBytes(writeTotal, sizeof(writeTotal), gHostWriteBytes);
    FormatSpeed(speed, sizeof(speed), _speedKbPerSecond);

    _top.FillRect(8, 120, 240, 52, ColorPanel);
    _top.DrawRect(8, 120, 240, 52, ColorEdge);
    _top.Text(14, 124, "TRAFFIC", ColorDim);
    _top.TextRight(242, 124, speed, _speedKbPerSecond > 0 ? ColorReady : ColorDim);
    _top.Text(14, 138, "Read", ColorDim);
    _top.TextRight(242, 138, readTotal, ColorText);
    _top.Text(14, 150, "Write", ColorDim);
    _top.TextRight(242, 150, writeTotal, ColorText);

    for (int i = 0; i < kSpeedHistoryLength; i++)
    {
        u32 sample = _speedHistory[(_speedHead + i) % kSpeedHistoryLength];
        int height = (int)(sample * 7 / 128);
        if (height > 7)
            height = 7;
        if (height > 0)
            _top.FillRect(10 + i * 6, 168 - height, 4, height, ColorReady);
    }
    _top.HLine(10, 169, 236, ColorEdge);

    if (_toastFrames > 0)
        _top.Text(12, 178, _toast, _toastColor);
    else
        _top.Text(12, 178, "Eject the drive in the PC before unplugging", ColorDim);
}

void App::RenderBottom()
{
    _bottom.FillRect(0, 0, 256, 28, ColorPanel);
    _bottom.FillRect(0, 0, 4, 28, _writeProtected ? ColorWarning : ColorReady);
    _bottom.Text(10, 6, "SETTINGS", ColorText, 2);
    _bottom.TextRight(246, 10, _writeProtected ? "PROTECTED" : "WRITABLE",
        _writeProtected ? ColorWarning : ColorDim);

    for (int i = 0; i < MenuItemCount; i++)
    {
        int y = 34 + i * 32;
        bool selected = i == _selected;

        _bottom.FillRect(8, y, 240, 28, selected ? ColorPanelSelected : ColorPanel);
        if (selected)
            _bottom.FillRect(8, y, 3, 28, ColorReady);

        _bottom.Text(18, y + 4, kMenuLabels[i], ColorText);
        const char* value = MenuValue(i);
        if (value[0] != 0)
        {
            u16 valueColor = i == MenuWriteProtect
                ? (_writeProtected ? ColorWarning : ColorReady)
                : ColorDim;
            _bottom.TextRight(240, y + 4, value, valueColor);
        }
        _bottom.Text(18, y + 16, kMenuHints[i], ColorDim);
    }

    _bottom.Text(12, 168, "A select   START write protect", ColorDim);
    _bottom.Text(12, 180, "D-pad move   SELECT rescan", ColorDim);
}

void App::RenderAbout()
{
    _bottom.FillRect(8, 8, 240, 176, ColorPanel);
    _bottom.DrawRect(8, 8, 240, 176, ColorEdge);
    _bottom.Text(20, 18, "ABOUT", ColorDim);
    _bottom.Text(20, 34, "Mount microSD", ColorText, 2);

    char capacity[24];
    FormatBytes(capacity, sizeof(capacity), _card.totalBytes);

    char readTotal[24];
    char writeTotal[24];
    FormatBytes(readTotal, sizeof(readTotal), gHostReadBytes);
    FormatBytes(writeTotal, sizeof(writeTotal), gHostWriteBytes);

    _bottom.Text(20, 60, "USB mass storage for DSpico", ColorText);
    _bottom.Text(20, 72, "VID 0xCAFE   PID 0x4002", ColorDim);

    char line[80];
    snprintf(line, sizeof(line), "Volume %s", _card.label[0] != 0 ? _card.label : "(no label)");
    _bottom.Text(20, 84, line, ColorText);

    snprintf(line, sizeof(line), "Card %s %s", _card.valid ? _card.fsType : "unknown",
        _card.valid ? capacity : "");
    _bottom.Text(20, 96, line, ColorText);

    snprintf(line, sizeof(line), "Read %s   Write %s", readTotal, writeTotal);
    _bottom.Text(20, 108, line, ColorText);

    _bottom.Text(20, 120, _writeProtected ? "Write protection is on" : "Write protection is off",
        _writeProtected ? ColorWarning : ColorReady);
    _bottom.Text(20, 156, "A or B closes this page", ColorDim);
}
