#pragma once
#include <Arduino.h>

// Общие настройки пульта в /config.json: калибровка и id активного пресета.
namespace Settings {

extern char activeId[8];

bool load();
bool save();

}  // namespace Settings
