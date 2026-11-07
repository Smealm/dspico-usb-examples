#pragma once
#include "common.h"

/// @brief Reads the keypad once per frame and reports button presses.
class Input
{
public:
    enum Button
    {
        A,
        B,
        Select,
        Start,
        Right,
        Left,
        Up,
        Down,
        ButtonCount
    };

    void Update();

    /// @brief True on the frame the button went down.
    bool Pressed(Button button) const { return (_pressed & Mask(button)) != 0; }
    bool Held(Button button) const { return (_held & Mask(button)) != 0; }

private:
    u16 _held = 0;
    u16 _pressed = 0;
    u8 _holdFrames[ButtonCount] = {};

    static u16 Mask(Button button) { return (u16)(1u << (u32)button); }
    static u16 RawMask(Button button);
};
