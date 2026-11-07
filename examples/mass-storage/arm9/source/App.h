#pragma once
#include "common.h"
#include <libtwl/rtos/rtosEvent.h>
#include "card/CardInfo.h"
#include "ui/Canvas.h"
#include "ui/Input.h"

/// @brief The Mount microSD UI: live link state, card details, traffic graph
///        and the settings menu.
class App
{
public:
    App();

    /// @brief Runs the main loop. Waits for the given vblank event before
    ///        pushing each finished frame to the screens.
    void Run(rtos_event_t& vblankEvent, bool cardReady);

    enum MenuItem
    {
        MenuWriteProtect,
        MenuRescan,
        MenuResetCounters,
        MenuAbout,
        MenuItemCount
    };

private:
    enum LinkState
    {
        LinkWaiting,
        LinkReady,
        LinkActive,
        LinkSuspended,
        LinkEjected
    };

    Canvas _top;
    Canvas _bottom;
    Input _input;
    CardInfo _card;

    bool _cardReady;
    bool _writeProtected;
    bool _showAbout;
    int _selected;

    u32 _frame;
    u32 _lastTrafficFrame;
    bool _lastTrafficWasWrite;
    u64 _previousReadBytes;
    u64 _previousWriteBytes;
    u32 _seenUsbEvent;

    u32 _speedHistory[40];
    int _speedHead;
    int _sampleFrames;
    u64 _sampledReadBytes;
    u64 _sampledWriteBytes;
    u32 _speedKbPerSecond;

    char _toast[64];
    u16 _toastColor;
    int _toastFrames;

    void HandleInput();
    void UpdateUsbEvents();
    void UpdateTraffic();
    void Activate(MenuItem item);
    void Toast(const char* text, u16 color, int frames = 150);

    LinkState GetLinkState() const;
    const char* MenuValue(int index) const;

    void Render();
    void RenderTop(LinkState state);
    void RenderBottom();
    void RenderAbout();
};
