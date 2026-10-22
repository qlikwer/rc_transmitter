#include "inputs.h"

namespace {

const uint8_t kAnalogPins[A_COUNT] = {PIN_J1X, PIN_J1Y, PIN_J2X, PIN_J2Y, PIN_POT1, PIN_POT2};
const uint8_t kDigitalPins[D_COUNT] = {PIN_J1SW, PIN_J2SW, PIN_SW1, PIN_SW2, PIN_SW3, PIN_SW4};

constexpr uint8_t kOversample = 8;
constexpr float kSmooth = 0.4f;          // доля нового значения в фильтре
constexpr uint16_t kDebounceMs = 20;
constexpr float kNavThreshold = 0.6f;
constexpr uint16_t kNavFirstMs = 400;
constexpr uint16_t kNavRepeatMs = 150;

float filtered[A_COUNT];

struct Button {
  bool rawLevel = false;
  bool state = false;
  bool edgePressed = false;
  uint32_t rawChangedAt = 0;
  uint32_t downSince = 0;
};
Button btn[D_COUNT];

Calibration calib;

Nav navState = NAV_NONE;
Nav navCurrent = NAV_NONE;
uint32_t navNextAt = 0;

uint16_t readAveraged(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kOversample; i++) sum += analogRead(pin);
  return sum / kOversample;
}

Nav readNavDirection() {
  float x = Inputs::norm(A_J1X);
  float y = Inputs::norm(A_J1Y);
  if (fabsf(x) < kNavThreshold && fabsf(y) < kNavThreshold) return NAV_NONE;
  if (fabsf(y) >= fabsf(x)) return y > 0 ? NAV_UP : NAV_DOWN;
  return x > 0 ? NAV_RIGHT : NAV_LEFT;
}

}  // namespace

namespace Inputs {

void resetCal() {
  for (uint8_t i = 0; i < A_COUNT; i++) calib.ch[i] = {0, 2048, 4095};
  calib.done = false;
}

void begin() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // диапазон 0..3.3 В
  for (uint8_t i = 0; i < A_COUNT; i++) {
    pinMode(kAnalogPins[i], INPUT);
    filtered[i] = readAveraged(kAnalogPins[i]);
  }
  for (uint8_t i = 0; i < D_COUNT; i++) pinMode(kDigitalPins[i], INPUT_PULLUP);
  resetCal();
}

void update() {
  uint32_t now = millis();

  for (uint8_t i = 0; i < A_COUNT; i++) {
    float v = readAveraged(kAnalogPins[i]);
    filtered[i] += (v - filtered[i]) * kSmooth;
  }

  for (uint8_t i = 0; i < D_COUNT; i++) {
    Button& b = btn[i];
    bool level = digitalRead(kDigitalPins[i]) == LOW;
    if (level != b.rawLevel) {
      b.rawLevel = level;
      b.rawChangedAt = now;
    }
    if (now - b.rawChangedAt >= kDebounceMs && b.state != b.rawLevel) {
      b.state = b.rawLevel;
      if (b.state) {
        b.edgePressed = true;
        b.downSince = now;
      }
    }
  }

  // Навигация: первое срабатывание сразу, потом пауза и автоповтор.
  Nav dir = readNavDirection();
  if (dir == NAV_NONE) {
    navState = NAV_NONE;
  } else if (dir != navState) {
    navState = dir;
    navCurrent = dir;
    navNextAt = now + kNavFirstMs;
  } else if (now >= navNextAt) {
    navCurrent = dir;
    navNextAt = now + kNavRepeatMs;
  }
}

uint16_t raw(AnalogId id) { return (uint16_t)(filtered[id] + 0.5f); }

float norm(AnalogId id) {
  float v = filtered[id];
  const CalPoint& c = calib.ch[id];

  if (id >= A_POT1) {
    float span = (float)c.max - (float)c.min;
    if (span < 1) return 0;
    return constrain((v - c.min) / span, 0.0f, 1.0f);
  }

  float out;
  if (v >= c.center) {
    float span = (float)c.max - (float)c.center;
    out = span < 1 ? 0 : (v - c.center) / span;
  } else {
    float span = (float)c.center - (float)c.min;
    out = span < 1 ? 0 : (v - c.center) / span;
  }
  return constrain(out, -1.0f, 1.0f) * AXIS_SIGN[id];
}

bool on(DigitalId id) { return btn[id].state; }
bool pressed(DigitalId id) { return btn[id].edgePressed; }
uint32_t heldMs(DigitalId id) { return btn[id].state ? millis() - btn[id].downSince : 0; }

Nav nav() { return navCurrent; }

void clearEvents() {
  for (uint8_t i = 0; i < D_COUNT; i++) btn[i].edgePressed = false;
  navCurrent = NAV_NONE;
}

Calibration& cal() { return calib; }

}  // namespace Inputs
