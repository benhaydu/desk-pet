#pragma once
#include <stdint.h>

// (r, g, b) to the screen's 16-bit colour, worked out at compile time.
// 5 bits of red, 6 of green, 5 of blue: green gets the extra bit because eyes see it best
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}