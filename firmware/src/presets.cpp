#include "presets.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "inputs.h"
#include "settings.h"

namespace {

const char* kDir = "/presets";

const char* const kModelNames[M_COUNT] = {"Boat", "Car", "Plane", "Copter", "Generic"};
const char* const kModelKeys[M_COUNT] = {"boat", "car", "plane", "copter", "generic"};

// Подписи каналов 1..8 для каждой модели.
const char* const kLabels[M_COUNT][CHANNELS] = {
    {"THR", "RUD", "AUX", "AUX", "P1", "P2", "-", "-"},      // Boat
    {"THR", "STR", "AUX", "AUX", "P1", "P2", "-", "-"},      // Car
    {"AIL", "ELE", "THR", "RUD", "P1", "P2", "-", "-"},      // Plane
    {"AIL", "ELE", "THR", "YAW", "P1", "P2", "-", "-"},      // Copter
    {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "-", "-"},    // Generic
};

Preset presets[MAX_PRESETS];
int presetCount = 0;
int active = 0;

AxisCfg axis(uint8_t ch, uint8_t expo = 0, uint8_t rate = 100) {
  return AxisCfg{ch, false, 5, expo, rate, 0};
}

void applyDefaults(Preset& p, Model m) {
  p.model = m;
  p.potCh[0] = 5;
  p.potCh[1] = 6;
  p.radioCh = 76;
  strlcpy(p.addr, "PULT1", sizeof(p.addr));

  // Оси: J1X, J1Y, J2X, J2Y. Режим 2: слева газ, справа элероны/руль.
  switch (m) {
    case M_BOAT:
      p.axes[0] = axis(3);
      p.axes[1] = axis(1);        // J1Y -> газ
      p.axes[2] = axis(2, 20);    // J2X -> руль
      p.axes[3] = axis(4);
      strlcpy(p.addr, "BOAT1", sizeof(p.addr));
      break;
    case M_CAR:
      p.axes[0] = axis(3);
      p.axes[1] = axis(1);        // J1Y -> газ/тормоз
      p.axes[2] = axis(2, 30);    // J2X -> руль
      p.axes[3] = axis(4);
      strlcpy(p.addr, "CAR01", sizeof(p.addr));
      break;
    case M_PLANE:
    case M_COPTER:
      p.axes[0] = axis(4);        // J1X -> руль направления / рысканье
      p.axes[1] = axis(3);        // J1Y -> газ
      p.axes[2] = axis(1, 20);    // J2X -> элероны
      p.axes[3] = axis(2, 20);    // J2Y -> руль высоты
      strlcpy(p.addr, m == M_PLANE ? "PLN01" : "COP01", sizeof(p.addr));
      break;
    default:
      for (int i = 0; i < 4; i++) p.axes[i] = axis(i + 1);
      break;
  }
}

bool freeId(char* out, size_t len) {
  for (int n = 1; n < 100; n++) {
    char id[8];
    snprintf(id, sizeof(id), "p%02d", n);
    bool used = false;
    for (int i = 0; i < presetCount; i++)
      if (strcmp(presets[i].id, id) == 0) used = true;
    if (!used) {
      strlcpy(out, id, len);
      return true;
    }
  }
  return false;
}

Model modelFromKey(const char* key) {
  for (int i = 0; i < M_COUNT; i++)
    if (strcmp(key, kModelKeys[i]) == 0) return (Model)i;
  return M_GENERIC;
}

bool loadFile(const char* id, Preset& p) {
  char path[32];
  snprintf(path, sizeof(path), "%s/%s.json", kDir, id);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("%s: %s\n", path, err.c_str());
    return false;
  }

  strlcpy(p.id, id, sizeof(p.id));
  strlcpy(p.name, doc["name"] | id, sizeof(p.name));
  applyDefaults(p, modelFromKey(doc["model"] | "generic"));

  p.radioCh = doc["radio"]["channel"] | p.radioCh;
  strlcpy(p.addr, doc["radio"]["addr"] | p.addr, sizeof(p.addr));

  JsonArray axes = doc["axes"].as<JsonArray>();
  for (int i = 0; i < 4 && i < (int)axes.size(); i++) {
    JsonObject a = axes[i];
    p.axes[i].ch = a["ch"] | p.axes[i].ch;
    p.axes[i].invert = a["inv"] | p.axes[i].invert;
    p.axes[i].deadzone = a["dz"] | p.axes[i].deadzone;
    p.axes[i].expo = a["expo"] | p.axes[i].expo;
    p.axes[i].rate = a["rate"] | p.axes[i].rate;
    p.axes[i].trim = a["trim"] | p.axes[i].trim;
  }
  JsonArray pots = doc["pots"].as<JsonArray>();
  for (int i = 0; i < 2 && i < (int)pots.size(); i++) p.potCh[i] = pots[i] | p.potCh[i];
  return true;
}

void sortById() {
  for (int i = 1; i < presetCount; i++)
    for (int j = i; j > 0 && strcmp(presets[j - 1].id, presets[j].id) > 0; j--) {
      Preset tmp = presets[j];
      presets[j] = presets[j - 1];
      presets[j - 1] = tmp;
    }
}

float applyAxis(const AxisCfg& a, float v) {
  if (a.invert) v = -v;
  float dz = a.deadzone / 100.0f;
  float mag = fabsf(v);
  mag = mag < dz ? 0 : (mag - dz) / (1 - dz);
  float e = a.expo / 100.0f;
  mag = (1 - e) * mag + e * mag * mag * mag;
  v = (v < 0 ? -mag : mag) * (a.rate / 100.0f);
  v += a.trim / 100.0f;
  return constrain(v, -1.0f, 1.0f);
}

}  // namespace

namespace Presets {

void begin() {
  LittleFS.mkdir(kDir);
  presetCount = 0;

  File dir = LittleFS.open(kDir);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f && presetCount < MAX_PRESETS; f = dir.openNextFile()) {
      String name = f.name();
      f.close();
      if (!name.endsWith(".json")) continue;
      name.remove(name.length() - 5);
      if (name.length() >= sizeof(Preset::id)) continue;
      if (loadFile(name.c_str(), presets[presetCount])) presetCount++;
    }
  }

  if (presetCount == 0) {
    create(M_BOAT);
    create(M_CAR);
    create(M_PLANE);
  }
  sortById();

  active = 0;
  for (int i = 0; i < presetCount; i++)
    if (strcmp(presets[i].id, Settings::activeId) == 0) active = i;
  strlcpy(Settings::activeId, presets[active].id, sizeof(Settings::activeId));
}

int count() { return presetCount; }
Preset& at(int index) { return presets[index]; }
int activeIndex() { return active; }

void setActive(int index) {
  active = constrain(index, 0, presetCount - 1);
  strlcpy(Settings::activeId, presets[active].id, sizeof(Settings::activeId));
  Settings::save();
}

bool save(int index) {
  const Preset& p = presets[index];
  JsonDocument doc;
  doc["name"] = p.name;
  doc["model"] = kModelKeys[p.model];
  doc["radio"]["channel"] = p.radioCh;
  doc["radio"]["addr"] = p.addr;
  JsonArray axes = doc["axes"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject a = axes.add<JsonObject>();
    a["ch"] = p.axes[i].ch;
    a["inv"] = p.axes[i].invert;
    a["dz"] = p.axes[i].deadzone;
    a["expo"] = p.axes[i].expo;
    a["rate"] = p.axes[i].rate;
    a["trim"] = p.axes[i].trim;
  }
  JsonArray pots = doc["pots"].to<JsonArray>();
  pots.add(p.potCh[0]);
  pots.add(p.potCh[1]);

  char path[32];
  snprintf(path, sizeof(path), "%s/%s.json", kDir, p.id);
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

int create(Model model) {
  if (presetCount >= MAX_PRESETS) return -1;
  Preset& p = presets[presetCount];
  memset(&p, 0, sizeof(p));
  if (!freeId(p.id, sizeof(p.id))) return -1;

  int same = 1;
  for (int i = 0; i < presetCount; i++)
    if (presets[i].model == model) same++;
  snprintf(p.name, sizeof(p.name), "%s %d", kModelNames[model], same);
  applyDefaults(p, model);

  if (!save(presetCount)) return -1;

  char newId[8];
  char activeKeep[8];
  strlcpy(newId, p.id, sizeof(newId));
  strlcpy(activeKeep, active < presetCount ? presets[active].id : "", sizeof(activeKeep));
  presetCount++;
  sortById();

  int newIndex = 0;
  for (int i = 0; i < presetCount; i++) {
    if (strcmp(presets[i].id, newId) == 0) newIndex = i;
    if (strcmp(presets[i].id, activeKeep) == 0) active = i;
  }
  return newIndex;
}

bool remove(int index) {
  if (presetCount <= 1 || index < 0 || index >= presetCount) return false;
  char path[32];
  snprintf(path, sizeof(path), "%s/%s.json", kDir, presets[index].id);
  LittleFS.remove(path);
  for (int i = index; i < presetCount - 1; i++) presets[i] = presets[i + 1];
  presetCount--;
  if (active > index || active >= presetCount) active = max(0, active - 1);
  strlcpy(Settings::activeId, presets[active].id, sizeof(Settings::activeId));
  Settings::save();
  return true;
}

float axisOutput(const AxisCfg& a, float norm) { return applyAxis(a, norm); }

void compute(const Preset& p, uint16_t out[CHANNELS]) {
  float ch[CHANNELS];
  for (int i = 0; i < CHANNELS; i++) ch[i] = 0;

  for (int i = 0; i < 4; i++) {
    if (p.axes[i].ch < 1 || p.axes[i].ch > CHANNELS) continue;
    ch[p.axes[i].ch - 1] = applyAxis(p.axes[i], Inputs::norm((AnalogId)i));
  }
  for (int i = 0; i < 2; i++) {
    if (p.potCh[i] < 1 || p.potCh[i] > CHANNELS) continue;
    ch[p.potCh[i] - 1] = Inputs::norm((AnalogId)(A_POT1 + i)) * 2.0f - 1.0f;
  }
  for (int i = 0; i < CHANNELS; i++) out[i] = (uint16_t)(1500 + ch[i] * 500);
}

const char* modelName(Model m) { return kModelNames[m]; }

const char* channelLabel(Model m, int ch) { return kLabels[m][ch - 1]; }

}  // namespace Presets
