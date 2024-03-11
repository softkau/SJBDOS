#pragma once

#include "message.hpp"

#include <deque>

constexpr uint8_t kLControlBitMask = 0b00000001u;
constexpr uint8_t kLShiftBitMask   = 0b00000010u;
constexpr uint8_t kLAltBitMask     = 0b00000100u;
constexpr uint8_t kLGUIBitMask     = 0b00001000u;
constexpr uint8_t kRControlBitMask = 0b00010000u;
constexpr uint8_t kRShiftBitMask   = 0b00100000u;
constexpr uint8_t kRAltBitMask     = 0b01000000u;
constexpr uint8_t kRGUIBitMask     = 0b10000000u;

void InitializeKeyboard();