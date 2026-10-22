#pragma once
#include <Arduino.h>

// Распиновка: см. «Пульт — схема подключения» в хранилище.

// Аналоговые входы (ADC1)
constexpr uint8_t PIN_J1X  = 36;
constexpr uint8_t PIN_J1Y  = 39;
constexpr uint8_t PIN_J2X  = 34;
constexpr uint8_t PIN_J2Y  = 35;
constexpr uint8_t PIN_POT1 = 32;
constexpr uint8_t PIN_POT2 = 33;

// Цифровые входы (INPUT_PULLUP, замкнуто на GND = включено)
constexpr uint8_t PIN_J1SW = 13;
constexpr uint8_t PIN_J2SW = 14;
constexpr uint8_t PIN_SW1  = 27;
constexpr uint8_t PIN_SW2  = 26;
constexpr uint8_t PIN_SW3  = 25;
constexpr uint8_t PIN_SW4  = 4;

// Радио nRF24L01+ (SCK/MISO/MOSI общие с экраном, шина VSPI)
constexpr uint8_t PIN_NRF_CE  = 2;
constexpr uint8_t PIN_NRF_CSN = 15;

// Звук
constexpr uint8_t PIN_BUZZER = 17;

// Знак каждой оси (J1X, J1Y, J2X, J2Y). Если стик вверх/вправо даёт
// уменьшение значения, поставить -1. Проверяется на экране "Input test".
constexpr int8_t AXIS_SIGN[4] = {1, 1, 1, 1};

constexpr uint8_t MAX_PRESETS = 12;
