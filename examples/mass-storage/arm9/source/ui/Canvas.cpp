#include "ui/Canvas.h"
#include "ui/DefaultFont.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <libtwl/dma/dmaNitro.h>
#include <nds/arm9/cache.h>

Canvas::Canvas(u16* framebuffer, volatile u16* vram, int width, int height)
    : _framebuffer(framebuffer)
    , _vram(vram)
    , _width(width)
    , _height(height)
{
}

void Canvas::Clear(u16 color)
{
    u32 value = ((u32)color << 16) | color;
    u32* pixels = (u32*)_framebuffer;
    for (int i = 0; i < _width * _height / 2; i++)
        pixels[i] = value;
}

void Canvas::FillRect(int x, int y, int width, int height, u16 color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > _width)
        x1 = _width;
    if (y1 > _height)
        y1 = _height;
    if (x0 >= x1 || y0 >= y1)
        return;

    for (int row = y0; row < y1; row++)
    {
        u16* destination = _framebuffer + row * _width + x0;
        for (int column = x0; column < x1; column++)
            *destination++ = color;
    }
}

void Canvas::DrawRect(int x, int y, int width, int height, u16 color)
{
    HLine(x, y, width, color);
    HLine(x, y + height - 1, width, color);
    VLine(x, y, height, color);
    VLine(x + width - 1, y, height, color);
}

void Canvas::HLine(int x, int y, int width, u16 color)
{
    if (y < 0 || y >= _height)
        return;
    int x0 = x < 0 ? 0 : x;
    int x1 = x + width;
    if (x1 > _width)
        x1 = _width;
    u16* destination = _framebuffer + y * _width;
    for (int column = x0; column < x1; column++)
        destination[column] = color;
}

void Canvas::VLine(int x, int y, int height, u16 color)
{
    if (x < 0 || x >= _width)
        return;
    int y0 = y < 0 ? 0 : y;
    int y1 = y + height;
    if (y1 > _height)
        y1 = _height;
    for (int row = y0; row < y1; row++)
        _framebuffer[row * _width + x] = color;
}

void Canvas::DrawGlyph(int x, int y, char character, u16 color, int scale)
{
    const u8* glyph = &gDefaultFont8x8[(u8)character * 8];
    for (int row = 0; row < 8; row++)
    {
        u8 bits = glyph[row];
        if (bits == 0)
            continue;
        int pixelY = y + row * scale;
        for (int column = 0; column < 8; column++)
        {
            if (bits & (0x80 >> column))
                FillRect(x + column * scale, pixelY, scale, scale, color);
        }
    }
}

void Canvas::Text(int x, int y, const char* text, u16 color, int scale)
{
    if (text == nullptr)
        return;

    int cursor = x;
    for (const char* character = text; *character != 0; character++)
    {
        DrawGlyph(cursor, y, *character, color, scale);
        cursor += 8 * scale;
    }
}

int Canvas::TextWidth(const char* text, int scale) const
{
    if (text == nullptr)
        return 0;
    return (int)strlen(text) * 8 * scale;
}

void Canvas::TextRight(int rightX, int y, const char* text, u16 color, int scale)
{
    Text(rightX - TextWidth(text, scale), y, text, color, scale);
}

void Canvas::Textf(int x, int y, u16 color, int scale, const char* format, ...)
{
    char buffer[96];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    Text(x, y, buffer, color, scale);
}

void Canvas::Blit() const
{
    if (_vram == nullptr)
        return;

    u32 size = (u32)_width * (u32)_height * 2;
    DC_FlushRange(_framebuffer, size);
    dma_ntrCopy32(3, _framebuffer, (void*)_vram, size);
}
