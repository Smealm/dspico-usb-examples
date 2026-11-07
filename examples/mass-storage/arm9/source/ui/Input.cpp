#include "ui/Input.h"

#define REG_KEYINPUT (*(vu16*)0x04000130)

/// @brief Maps a logical button to the active-low keypad register bit.
u16 Input::RawMask(Button button)
{
    switch (button)
    {
        case A: return 1u << 0;
        case B: return 1u << 1;
        case Select: return 1u << 2;
        case Start: return 1u << 3;
        case Right: return 1u << 4;
        case Left: return 1u << 5;
        case Up: return 1u << 6;
        case Down: return 1u << 7;
        default: return 0;
    }
}

void Input::Update()
{
    u16 down = (u16)(~REG_KEYINPUT & 0x3FF);
    _pressed = (u16)(down & ~_held);
    _held = down;

    for (u32 i = 0; i < ButtonCount; i++)
    {
        if (_held & (1u << i))
        {
            if (_holdFrames[i] < 255)
                _holdFrames[i]++;
        }
        else
        {
            _holdFrames[i] = 0;
        }
    }
}
