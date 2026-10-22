#pragma once
#include <Arduino.h>
#include "config.h"

enum AnalogId : uint8_t { A_J1X, A_J1Y, A_J2X, A_J2Y, A_POT1, A_POT2, A_COUNT };
enum DigitalId : uint8_t { D_J1SW, D_J2SW, D_SW1, D_SW2, D_SW3, D_SW4, D_COUNT };
enum Nav : uint8_t { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT };

struct CalPoint {
  uint16_t min, center, max;
};

struct Calibration {
  CalPoint ch[A_COUNT];
  bool done;
};

namespace Inputs {

void begin();
void update();  // вызывать каждый проход loop()

// Нажатия и события навигации копятся между кадрами интерфейса и живут до
// clearEvents(), которую вызывает интерфейс после обработки кадра.
void clearEvents();

uint16_t raw(AnalogId id);   // 0..4095, сглаженное
float norm(AnalogId id);     // стики -1..1, потенциометры 0..1, с учётом калибровки

bool on(DigitalId id);       // тумблер/кнопка сейчас нажаты
bool pressed(DigitalId id);  // фронт нажатия с прошлого update()
uint32_t heldMs(DigitalId id);

Nav nav();                   // навигация левым стиком, с автоповтором

Calibration& cal();
void resetCal();

}  // namespace Inputs
