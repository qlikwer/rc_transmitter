#include "settings.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "inputs.h"

namespace Settings {

char activeId[8] = "";

static const char* kPath = "/config.json";

bool load() {
  File f = LittleFS.open(kPath, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("config.json: %s\n", err.c_str());
    return false;
  }

  strlcpy(activeId, doc["active"] | "", sizeof(activeId));

  Calibration& cal = Inputs::cal();
  JsonArray arr = doc["cal"].as<JsonArray>();
  if (!arr.isNull() && arr.size() == A_COUNT) {
    for (uint8_t i = 0; i < A_COUNT; i++) {
      JsonArray p = arr[i];
      if (p.size() != 3) return true;  // битая калибровка: останутся значения по умолчанию
      cal.ch[i] = {p[0].as<uint16_t>(), p[1].as<uint16_t>(), p[2].as<uint16_t>()};
    }
    cal.done = doc["cal_done"] | false;
  }
  return true;
}

bool save() {
  JsonDocument doc;
  doc["active"] = activeId;
  doc["cal_done"] = Inputs::cal().done;
  JsonArray arr = doc["cal"].to<JsonArray>();
  for (uint8_t i = 0; i < A_COUNT; i++) {
    const CalPoint& c = Inputs::cal().ch[i];
    JsonArray p = arr.add<JsonArray>();
    p.add(c.min);
    p.add(c.center);
    p.add(c.max);
  }

  File f = LittleFS.open(kPath, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

}  // namespace Settings
