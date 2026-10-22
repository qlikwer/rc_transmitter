#include <Arduino.h>
#include <LittleFS.h>

#include "inputs.h"
#include "presets.h"
#include "radio.h"
#include "settings.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) Serial.println("LittleFS: mount failed");

  Inputs::begin();
  Settings::load();  // калибровка и id активного пресета
  Presets::begin();
  Ui::begin();     // поднимает SPI вместе с экраном
  Radio::begin();  // радио садится на ту же шину
}

void loop() {
  Inputs::update();
  Ui::update();
  Radio::update();
}
