#pragma once
#include <Arduino.h>
#include "config.h"

enum Model : uint8_t { M_BOAT, M_CAR, M_PLANE, M_COPTER, M_GENERIC, M_COUNT };

constexpr uint8_t CHANNELS = 8;

struct AxisCfg {
  uint8_t ch;         // канал 1..8, 0 = ось не используется
  bool invert;
  uint8_t deadzone;   // %
  uint8_t expo;       // %
  uint8_t rate;       // % хода
  int8_t trim;        // % смещения нуля
};

struct Preset {
  char id[8];         // имя файла без расширения: p01, p02...
  char name[17];
  Model model;
  AxisCfg axes[4];    // J1X, J1Y, J2X, J2Y
  uint8_t potCh[2];   // каналы потенциометров, 0 = не используется
  uint8_t radioCh;
  char addr[6];
};

namespace Presets {

void begin();  // читает /presets, при пустой папке создаёт по одному пресету на модель

int count();
Preset& at(int index);

int activeIndex();
void setActive(int index);

int create(Model model);  // новый пресет по шаблону модели; индекс или -1
bool remove(int index);
bool save(int index);

// Выход оси -1..1 после реверса, мёртвой зоны, экспоненты, хода и триммера.
float axisOutput(const AxisCfg& a, float norm);

// Каналы в микросекундах 1000..2000 (центр 1500).
void compute(const Preset& p, uint16_t out[CHANNELS]);

const char* modelName(Model m);
const char* channelLabel(Model m, int ch);  // ch 1..8

}  // namespace Presets
