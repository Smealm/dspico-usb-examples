#pragma once
#include "common.h"

/// @brief Builds a 15 bit BGR555 color from 8 bit components.
static constexpr u16 Rgb15(u32 r, u32 g, u32 b)
{
    return (u16)(((r >> 3) & 0x1F) | (((g >> 3) & 0x1F) << 5) | (((b >> 3) & 0x1F) << 10));
}

/// @brief A 256x192 16 bpp canvas that can be blitted to a screen framebuffer.
class Canvas
{
public:
    Canvas(u16* framebuffer, volatile u16* vram, int width = 256, int height = 192);

    void Clear(u16 color);
    void FillRect(int x, int y, int width, int height, u16 color);
    void DrawRect(int x, int y, int width, int height, u16 color);
    void HLine(int x, int y, int width, u16 color);
    void VLine(int x, int y, int height, u16 color);

    void Text(int x, int y, const char* text, u16 color, int scale = 1);
    void TextRight(int rightX, int y, const char* text, u16 color, int scale = 1);
    void Textf(int x, int y, u16 color, int scale, const char* format, ...);

    int TextWidth(const char* text, int scale = 1) const;

    /// @brief Copies the canvas to the mapped screen framebuffer during vblank.
    void Blit() const;

private:
    u16* _framebuffer;
    volatile u16* _vram;
    int _width;
    int _height;

    void DrawGlyph(int x, int y, char character, u16 color, int scale);
};
