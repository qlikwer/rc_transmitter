#pragma once
#include <Arduino.h>

namespace Radio {

enum State : uint8_t {
  R_OFF,     // передача не ведётся (не экран управления)
  R_ERROR,   // модуль не отвечает по SPI
  R_NOLINK,  // шлём, подтверждений нет
  R_LINK,    // приёмник подтверждает пакеты
};

bool begin();                // после Ui::begin(): шина SPI уже поднята экраном
void setActive(bool active); // true только на экране управления; иначе тишина, и приёмник уходит в failsafe
void update();               // вызывать каждый проход loop()

State state();
uint8_t quality();           // доля подтверждённых пакетов за последние 32 отправки, %

bool hasTelemetry();         // свежий ответ приёмника (не старше 1 с)
uint16_t batteryMv();
bool rxFailsafe();

}  // namespace Radio
