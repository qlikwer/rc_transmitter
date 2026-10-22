#include "ui.h"

#include <TFT_eSPI.h>

#include "inputs.h"
#include "presets.h"
#include "radio.h"
#include "settings.h"

// Управление в меню: левый стик (J1) листает, кнопка J1 = OK, кнопка J2 = назад.
// Шрифты TFT_eSPI без кириллицы, поэтому весь текст на экране латиницей.

namespace {

TFT_eSPI tft;
TFT_eSprite spr(&tft);

constexpr int W = 128, H = 160;
constexpr int HEADER_H = 16, FOOTER_H = 12, ROW_H = 16;
constexpr int VISIBLE = (H - HEADER_H - FOOTER_H) / ROW_H;
constexpr uint32_t FRAME_MS = 33;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t C_BG = TFT_BLACK;
constexpr uint16_t C_HEAD = rgb(0, 70, 140);
constexpr uint16_t C_TEXT = TFT_WHITE;
constexpr uint16_t C_DIM = rgb(130, 130, 130);
constexpr uint16_t C_SEL = rgb(255, 170, 0);
constexpr uint16_t C_OK = rgb(60, 200, 90);
constexpr uint16_t C_BAD = rgb(230, 60, 60);
constexpr uint16_t C_BAR = rgb(40, 40, 40);

enum Screen {
  S_CONTROL, S_MENU, S_PRESETS, S_NEWMODEL, S_CALIB, S_TEST, S_CONFIRM,
  S_EDIT, S_EDIT_AXIS, S_TEXT,
};
enum ConfirmKind { CK_DELETE, CK_SAVE };
ConfirmKind confirmKind = CK_DELETE;
Screen screen = S_CONTROL;
int sel = 0;
int topRow = 0;
uint32_t lastFrame = 0;

const char* const kMenuItems[] = {"Control", "Presets", "Edit preset", "New preset", "Calibrate", "Input test"};
constexpr int MENU_N = 6;

// --- редактор пресета -----------------------------------------------------

Preset edit;                 // рабочая копия, в пресет попадает по «Save»
int editIndex = -1;
int editAxis = 0;            // 0..3 оси стиков
bool editDirty = false;
char editLines[12][28];
const char* editPtrs[12];

// Ввод текста: символ выбирается стиком вверх/вниз, курсор двигается влево/вправо.
char textBuf[20];
uint8_t textLen = 0;
uint8_t textMax = 0;
bool textFixed = false;      // true: длина фиксирована (адрес), false: можно удлинять
const char* textCharset = "";
char* textTarget = nullptr;
uint8_t textCursor = 0;
const char* textTitle = "";

constexpr const char* kNameChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
constexpr const char* kAddrChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// Пункты верхнего списка редактора.
enum EditItem {
  E_NAME, E_J1X, E_J1Y, E_J2X, E_J2Y, E_POT1, E_POT2, E_RFCH, E_ADDR, E_SAVE, E_COUNT
};

// Пункты списка параметров оси.
enum AxisItem { X_CH, X_INV, X_DZ, X_EXPO, X_RATE, X_TRIM, X_COUNT };

char presetLines[MAX_PRESETS][24];
const char* presetPtrs[MAX_PRESETS];

int confirmIndex = -1;

// --- калибровка ---------------------------------------------------------

enum CalStep { CS_CENTER, CS_SAMPLE, CS_RANGE };
CalStep calStep = CS_CENTER;
Calibration calWork;
uint32_t calSum[A_COUNT];
int calSamples = 0;
const char* calMsg = nullptr;
uint32_t calMsgUntil = 0;
bool calFirstRun = false;

constexpr int CAL_SAMPLE_FRAMES = 15;
constexpr uint16_t MIN_STICK_HALF_RANGE = 600;
constexpr uint16_t MIN_POT_RANGE = 2000;

// --- отрисовка ------------------------------------------------------------

void text(const char* s, int x, int y, int font, uint16_t color, uint8_t datum = TL_DATUM) {
  spr.setTextDatum(datum);
  spr.setTextColor(color);
  spr.drawString(s, x, y, font);
}

void header(const char* title) {
  spr.fillRect(0, 0, W, HEADER_H, C_HEAD);
  text(title, 3, 0, 2, C_TEXT);
}

void footer(const char* s) {
  spr.fillRect(0, H - FOOTER_H, W, FOOTER_H, rgb(25, 25, 25));
  text(s, 3, H - FOOTER_H + 2, 1, C_DIM);
}

void listNav(int n, Nav nav) {
  if (n <= 0) return;
  if (nav == NAV_DOWN) sel = (sel + 1) % n;
  if (nav == NAV_UP) sel = (sel + n - 1) % n;
  if (sel < topRow) topRow = sel;
  if (sel >= topRow + VISIBLE) topRow = sel - VISIBLE + 1;
}

void drawList(const char* title, const char* const* items, int n, const char* foot) {
  header(title);
  for (int i = 0; i < VISIBLE && topRow + i < n; i++) {
    int idx = topRow + i;
    int y = HEADER_H + i * ROW_H;
    bool s = idx == sel;
    if (s) spr.fillRect(0, y, W, ROW_H, rgb(60, 40, 0));
    text(items[idx], 8, y, 2, s ? C_SEL : C_TEXT);
    if (s) text(">", 0, y, 2, C_SEL);
  }
  if (topRow > 0) text("^", W - 2, HEADER_H, 1, C_DIM, TR_DATUM);
  if (topRow + VISIBLE < n) text("v", W - 2, H - FOOTER_H - 8, 1, C_DIM, TR_DATUM);
  footer(foot);
}

// Горизонтальная шкала. centered: заливка от середины (стики), иначе слева (потенциометры).
void drawBar(int x, int y, int w, int h, float v, bool centered, uint16_t color) {
  spr.fillRect(x, y, w, h, C_BAR);
  if (centered) {
    int mid = x + w / 2;
    int len = (int)(v * (w / 2));
    if (len >= 0) spr.fillRect(mid, y, len, h, color);
    else spr.fillRect(mid + len, y, -len, h, color);
    spr.drawFastVLine(mid, y - 1, h + 2, C_DIM);
  } else {
    spr.fillRect(x, y, (int)(v * w), h, color);
  }
}

void drawSwitches(int y) {
  const DigitalId ids[4] = {D_SW1, D_SW2, D_SW3, D_SW4};
  const char* names[4] = {"S1", "S2", "S3", "S4"};
  for (int i = 0; i < 4; i++) {
    int x = 3 + i * 31;
    bool on = Inputs::on(ids[i]);
    spr.fillRoundRect(x, y, 28, 14, 3, on ? C_OK : C_BAR);
    text(names[i], x + 14, y + 3, 1, on ? TFT_BLACK : C_DIM, TC_DATUM);
  }
}

// Справа в шапке: качество связи или причина её отсутствия.
void drawRadioStatus() {
  char buf[12];
  uint16_t color = C_BAD;
  switch (Radio::state()) {
    case Radio::R_LINK:
      snprintf(buf, sizeof(buf), "%u%%", Radio::quality());
      color = Radio::quality() >= 80 ? C_OK : C_SEL;
      break;
    case Radio::R_NOLINK: strlcpy(buf, "NO LINK", sizeof(buf)); break;
    case Radio::R_ERROR:  strlcpy(buf, "NO RF", sizeof(buf)); break;
    default:              strlcpy(buf, "...", sizeof(buf)); color = C_DIM; break;
  }
  text(buf, W - 3, 3, 1, color, TR_DATUM);
}

// Строка телеметрии устройства над тумблерами.
void drawTelemetry(int y) {
  if (Radio::rxFailsafe()) {
    text("RX FAILSAFE", 3, y, 1, C_BAD);
    return;
  }
  if (!Radio::hasTelemetry()) {
    text("no telemetry", 3, y, 1, C_DIM);
    return;
  }
  char buf[20];
  snprintf(buf, sizeof(buf), "batt %.1f V", Radio::batteryMv() / 1000.0f);
  text(buf, 3, y, 1, C_TEXT);
}

// --- переходы ------------------------------------------------------------

void go(Screen s, int initialSel = 0) {
  screen = s;
  sel = initialSel;
  topRow = 0;
  if (sel >= VISIBLE) topRow = sel - VISIBLE + 1;
}

void startCalibration(bool firstRun) {
  calWork = Inputs::cal();
  calStep = CS_CENTER;
  calMsg = nullptr;
  calFirstRun = firstRun;
  go(S_CALIB);
}

void startEdit(int index) {
  editIndex = index;
  edit = Presets::at(index);
  editDirty = false;
  go(S_EDIT);
}

void saveEdit() {
  Presets::at(editIndex) = edit;
  Presets::save(editIndex);
  editDirty = false;
}

// Шаг значения стиком влево/вправо; editDirty выставляется, только если значение изменилось.
int adjust(int v, int lo, int hi, int step, Nav n) {
  if (n != NAV_LEFT && n != NAV_RIGHT) return v;
  int nv = constrain(v + (n == NAV_RIGHT ? step : -step), lo, hi);
  if (nv != v) editDirty = true;
  return nv;
}

int textReturnSel = 0;

void startText(const char* title, char* target, uint8_t maxLen, bool fixed, const char* charset, int retSel) {
  textTitle = title;
  textTarget = target;
  textMax = maxLen;
  textFixed = fixed;
  textCharset = charset;
  textReturnSel = retSel;
  strlcpy(textBuf, target, sizeof(textBuf));
  textLen = strlen(textBuf);
  if (fixed) {
    while (textLen < maxLen) textBuf[textLen++] = charset[0];
  } else if (textLen == 0) {
    textBuf[textLen++] = charset[0];
  }
  textBuf[textLen] = 0;
  textCursor = 0;
  go(S_TEXT);
}

// --- экраны ---------------------------------------------------------------

void controlScreen() {
  if (Inputs::pressed(D_J2SW)) {
    go(S_MENU);
    return;
  }

  const Preset& p = Presets::at(Presets::activeIndex());
  uint16_t ch[CHANNELS];
  Presets::compute(p, ch);

  header(p.name);
  drawRadioStatus();

  // Показываем только занятые каналы.
  int row = 0;
  for (int c = 1; c <= CHANNELS && row < 6; c++) {
    bool used = false;
    for (int a = 0; a < 4; a++) used |= p.axes[a].ch == c;
    for (int a = 0; a < 2; a++) used |= p.potCh[a] == c;
    if (!used) continue;

    int y = HEADER_H + 6 + row * 15;
    text(Presets::channelLabel(p.model, c), 3, y, 1, C_TEXT);
    drawBar(28, y, 70, 8, (ch[c - 1] - 1500) / 500.0f, true, C_SEL);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", ch[c - 1]);
    text(buf, W - 2, y, 1, C_DIM, TR_DATUM);
    row++;
  }

  drawTelemetry(H - FOOTER_H - 34);
  drawSwitches(H - FOOTER_H - 20);
  footer("J2 btn: menu");
}

void menuScreen() {
  Nav n = Inputs::nav();
  listNav(MENU_N, n);
  if (Inputs::pressed(D_J2SW)) {
    go(S_CONTROL);
    return;
  }
  if (Inputs::pressed(D_J1SW)) {
    switch (sel) {
      case 0: go(S_CONTROL); return;
      case 1: go(S_PRESETS, Presets::activeIndex()); return;
      case 2: startEdit(Presets::activeIndex()); return;
      case 3: go(S_NEWMODEL); return;
      case 4: startCalibration(false); return;
      case 5: go(S_TEST); return;
    }
  }
  drawList("Menu", kMenuItems, MENU_N, "J1 ok   J2 back");
}

void presetsScreen() {
  int n = Presets::count();
  listNav(n, Inputs::nav());

  if (Inputs::pressed(D_J2SW)) {
    go(S_MENU, 1);
    return;
  }
  if (Inputs::pressed(D_J1SW)) {
    Presets::setActive(sel);
    go(S_CONTROL);
    return;
  }
  if (Inputs::nav() == NAV_RIGHT && n > 1) {
    confirmIndex = sel;
    confirmKind = CK_DELETE;
    go(S_CONFIRM);
    return;
  }
  if (Inputs::nav() == NAV_LEFT) {
    startEdit(sel);
    return;
  }

  for (int i = 0; i < n; i++) {
    snprintf(presetLines[i], sizeof(presetLines[i]), "%c%s", i == Presets::activeIndex() ? '*' : ' ',
             Presets::at(i).name);
    presetPtrs[i] = presetLines[i];
  }
  drawList("Presets", presetPtrs, n, "J1 use <edit >del");
}

void confirmScreen() {
  if (confirmKind == CK_SAVE) {
    // Выход из редактора с несохранёнными правками: J1 сохранить, J2 отбросить.
    if (Inputs::pressed(D_J1SW)) {
      saveEdit();
      go(S_MENU, 2);
      return;
    }
    if (Inputs::pressed(D_J2SW)) {
      go(S_MENU, 2);
      return;
    }
    header("Unsaved");
    text(edit.name, W / 2, 60, 2, C_SEL, TC_DATUM);
    text("Save changes?", W / 2, 90, 1, C_DIM, TC_DATUM);
    footer("J1 save  J2 discard");
    return;
  }

  if (Inputs::pressed(D_J2SW) || confirmIndex < 0) {
    go(S_PRESETS);
    return;
  }
  if (Inputs::pressed(D_J1SW)) {
    Presets::remove(confirmIndex);
    confirmIndex = -1;
    go(S_PRESETS, Presets::activeIndex());
    return;
  }
  header("Delete?");
  text(Presets::at(confirmIndex).name, W / 2, 60, 2, C_SEL, TC_DATUM);
  text("This cannot be undone", W / 2, 90, 1, C_DIM, TC_DATUM);
  footer("J1 delete  J2 cancel");
}

void newModelScreen() {
  static const char* items[M_COUNT];
  for (int i = 0; i < M_COUNT; i++) items[i] = Presets::modelName((Model)i);

  listNav(M_COUNT, Inputs::nav());
  if (Inputs::pressed(D_J2SW)) {
    go(S_MENU, 3);
    return;
  }
  if (Inputs::pressed(D_J1SW)) {
    int idx = Presets::create((Model)sel);
    if (idx >= 0) {
      Presets::setActive(idx);
      go(S_CONTROL);
    } else {
      go(S_MENU, 3);  // достигнут предел пресетов
    }
    return;
  }
  drawList("Select model", items, M_COUNT, "J1 create  J2 back");
}

void calibScreen() {
  uint32_t now = millis();

  // Первую калибровку пропустить нельзя.
  if (Inputs::pressed(D_J2SW) && calStep != CS_SAMPLE && !calFirstRun) {
    go(S_MENU, 4);
    return;
  }

  header("Calibration");

  switch (calStep) {
    case CS_CENTER:
      text("Step 1/2", 4, 24, 2, C_SEL);
      text("Release both sticks", 4, 50, 1, C_TEXT);
      text("to the centre and", 4, 62, 1, C_TEXT);
      text("press J1 button.", 4, 74, 1, C_TEXT);
      if (calMsg && now < calMsgUntil) text(calMsg, 4, 110, 1, C_BAD);
      footer(calFirstRun ? "J1 ok" : "J1 ok   J2 back");
      if (Inputs::pressed(D_J1SW)) {
        memset(calSum, 0, sizeof(calSum));
        calSamples = 0;
        calStep = CS_SAMPLE;
      }
      break;

    case CS_SAMPLE:
      text("Measuring...", 4, 60, 2, C_TEXT);
      for (int i = 0; i < A_COUNT; i++) calSum[i] += Inputs::raw((AnalogId)i);
      if (++calSamples >= CAL_SAMPLE_FRAMES) {
        for (int i = 0; i < 4; i++) {
          uint16_t c = calSum[i] / calSamples;
          calWork.ch[i] = {c, c, c};
        }
        // Потенциометры: границы набираются на следующем шаге с текущего значения.
        for (int i = A_POT1; i < A_COUNT; i++) {
          uint16_t v = Inputs::raw((AnalogId)i);
          calWork.ch[i] = {v, v, v};
        }
        calStep = CS_RANGE;
      }
      break;

    case CS_RANGE: {
      // Накапливаем крайние значения.
      for (int i = 0; i < A_COUNT; i++) {
        uint16_t v = Inputs::raw((AnalogId)i);
        if (v < calWork.ch[i].min) calWork.ch[i].min = v;
        if (v > calWork.ch[i].max) calWork.ch[i].max = v;
      }

      text("Step 2/2", 4, 20, 2, C_SEL);
      text("Move sticks to all", 4, 38, 1, C_TEXT);
      text("limits, turn pots", 4, 48, 1, C_TEXT);
      text("fully both ways.", 4, 58, 1, C_TEXT);

      const char* names[A_COUNT] = {"J1X", "J1Y", "J2X", "J2Y", "P1", "P2"};
      for (int i = 0; i < A_COUNT; i++) {
        int y = 74 + i * 11;
        const CalPoint& c = calWork.ch[i];
        bool ok = i < 4 ? (c.center - c.min >= MIN_STICK_HALF_RANGE && c.max - c.center >= MIN_STICK_HALF_RANGE)
                        : (c.max - c.min >= MIN_POT_RANGE);
        text(names[i], 3, y, 1, ok ? C_OK : C_TEXT);
        // Шкала диапазона: серый фон 0..4095, полоса от min до max.
        spr.fillRect(28, y + 2, 96, 5, C_BAR);
        int x1 = 28 + c.min * 96 / 4095;
        int x2 = 28 + c.max * 96 / 4095;
        spr.fillRect(x1, y + 2, max(1, x2 - x1), 5, ok ? C_OK : C_SEL);
        int xr = 28 + Inputs::raw((AnalogId)i) * 96 / 4095;
        spr.drawFastVLine(xr, y, 9, TFT_WHITE);
      }
      if (calMsg && now < calMsgUntil) text(calMsg, 4, 142, 1, C_BAD);
      footer("J1 finish");

      if (Inputs::pressed(D_J1SW)) {
        bool ok = true;
        for (int i = 0; i < 4; i++) {
          const CalPoint& c = calWork.ch[i];
          ok &= c.center - c.min >= MIN_STICK_HALF_RANGE && c.max - c.center >= MIN_STICK_HALF_RANGE;
        }
        for (int i = A_POT1; i < A_COUNT; i++) ok &= calWork.ch[i].max - calWork.ch[i].min >= MIN_POT_RANGE;

        if (!ok) {
          calMsg = "Range too small";
          calMsgUntil = now + 2000;
        } else {
          for (int i = A_POT1; i < A_COUNT; i++) calWork.ch[i].center = (calWork.ch[i].min + calWork.ch[i].max) / 2;
          calWork.done = true;
          Inputs::cal() = calWork;
          Settings::save();
          go(S_CONTROL);
        }
      }
      break;
    }
  }
}

const char* chName(Model m, uint8_t ch) {
  return ch == 0 ? "off" : Presets::channelLabel(m, ch);
}

void editScreen() {
  Nav n = Inputs::nav();
  listNav(E_COUNT, n);

  if (Inputs::pressed(D_J2SW)) {
    if (editDirty) {
      confirmKind = CK_SAVE;
      go(S_CONFIRM);
    } else {
      go(S_MENU, 2);
    }
    return;
  }

  // Значения, которые меняются прямо в списке.
  if (sel == E_POT1 || sel == E_POT2) {
    uint8_t& ch = edit.potCh[sel - E_POT1];
    ch = adjust(ch, 0, CHANNELS, 1, n);
  } else if (sel == E_RFCH) {
    edit.radioCh = adjust(edit.radioCh, 0, 125, 1, n);
  }

  if (Inputs::pressed(D_J1SW)) {
    switch (sel) {
      case E_NAME: startText("Name", edit.name, 16, false, kNameChars, E_NAME); return;
      case E_J1X: case E_J1Y: case E_J2X: case E_J2Y:
        editAxis = sel - E_J1X;
        go(S_EDIT_AXIS);
        return;
      case E_ADDR: startText("RF address", edit.addr, 5, true, kAddrChars, E_ADDR); return;
      case E_SAVE:
        saveEdit();
        go(S_MENU, 2);
        return;
    }
  }

  static const char* const axisNames[4] = {"J1 X", "J1 Y", "J2 X", "J2 Y"};
  snprintf(editLines[E_NAME], sizeof(editLines[0]), "%s", edit.name);
  for (int a = 0; a < 4; a++)
    snprintf(editLines[E_J1X + a], sizeof(editLines[0]), "%s > %s", axisNames[a], chName(edit.model, edit.axes[a].ch));
  for (int p = 0; p < 2; p++)
    snprintf(editLines[E_POT1 + p], sizeof(editLines[0]), "Pot %d > %s", p + 1, chName(edit.model, edit.potCh[p]));
  snprintf(editLines[E_RFCH], sizeof(editLines[0]), "RF chan %u", edit.radioCh);
  snprintf(editLines[E_ADDR], sizeof(editLines[0]), "RF addr %s", edit.addr);
  snprintf(editLines[E_SAVE], sizeof(editLines[0]), "%s", editDirty ? "Save *" : "Save");
  for (int i = 0; i < E_COUNT; i++) editPtrs[i] = editLines[i];

  drawList("Edit preset", editPtrs, E_COUNT, "J1 open  <> change");
}

void editAxisScreen() {
  Nav n = Inputs::nav();
  listNav(X_COUNT, n);
  if (Inputs::pressed(D_J2SW)) {
    go(S_EDIT, E_J1X + editAxis);
    return;
  }

  AxisCfg& a = edit.axes[editAxis];
  switch (sel) {
    case X_CH:   a.ch = adjust(a.ch, 0, CHANNELS, 1, n); break;
    case X_INV:
      if (n == NAV_LEFT || n == NAV_RIGHT || Inputs::pressed(D_J1SW)) {
        a.invert = !a.invert;
        editDirty = true;
      }
      break;
    case X_DZ:   a.deadzone = adjust(a.deadzone, 0, 30, 1, n); break;
    case X_EXPO: a.expo = adjust(a.expo, 0, 100, 5, n); break;
    case X_RATE: a.rate = adjust(a.rate, 10, 100, 5, n); break;
    case X_TRIM: a.trim = adjust(a.trim, -50, 50, 1, n); break;
  }

  static const char* const titles[4] = {"J1 X", "J1 Y", "J2 X", "J2 Y"};
  snprintf(editLines[X_CH], sizeof(editLines[0]), "Channel %s", chName(edit.model, a.ch));
  snprintf(editLines[X_INV], sizeof(editLines[0]), "Reverse %s", a.invert ? "yes" : "no");
  snprintf(editLines[X_DZ], sizeof(editLines[0]), "Deadzone %u%%", a.deadzone);
  snprintf(editLines[X_EXPO], sizeof(editLines[0]), "Expo %u%%", a.expo);
  snprintf(editLines[X_RATE], sizeof(editLines[0]), "Rate %u%%", a.rate);
  snprintf(editLines[X_TRIM], sizeof(editLines[0]), "Trim %+d%%", a.trim);
  for (int i = 0; i < X_COUNT; i++) editPtrs[i] = editLines[i];
  drawList(titles[editAxis], editPtrs, X_COUNT, "<> change  J2 back");

  // Живой выход оси: двигаем стик и видим, как действуют параметры.
  float out = Presets::axisOutput(a, Inputs::norm((AnalogId)editAxis));
  int y = HEADER_H + X_COUNT * ROW_H + 8;
  drawBar(4, y, W - 8, 8, out, true, C_SEL);
  char buf[12];
  snprintf(buf, sizeof(buf), "%+.2f", out);
  text(buf, W / 2, y + 12, 1, C_DIM, TC_DATUM);
}

void textScreen() {
  Nav n = Inputs::nav();

  if (Inputs::pressed(D_J2SW)) {
    go(S_EDIT, textReturnSel);
    return;
  }

  if (n == NAV_UP || n == NAV_DOWN) {
    int len = strlen(textCharset);
    const char* p = strchr(textCharset, textBuf[textCursor]);
    int idx = p ? p - textCharset : 0;
    idx = (idx + (n == NAV_UP ? 1 : len - 1)) % len;
    textBuf[textCursor] = textCharset[idx];
  } else if (n == NAV_RIGHT) {
    if (textCursor + 1 < textLen) {
      textCursor++;
    } else if (!textFixed && textLen < textMax) {
      textBuf[textLen++] = 'A';
      textBuf[textLen] = 0;
      textCursor++;
    }
  } else if (n == NAV_LEFT && textCursor > 0) {
    textCursor--;
  }

  if (Inputs::pressed(D_J1SW)) {
    if (!textFixed)
      while (textLen > 0 && textBuf[textLen - 1] == ' ') textBuf[--textLen] = 0;
    if (textLen > 0 && strcmp(textBuf, textTarget) != 0) {
      strlcpy(textTarget, textBuf, textMax + 1);
      editDirty = true;
    }
    go(S_EDIT, textReturnSel);
    return;
  }

  header(textTitle);
  // Строка в ячейках по 8 пикселей; текущий символ крупно над строкой.
  for (uint8_t i = 0; i < textLen; i++) {
    char s[2] = {textBuf[i], 0};
    text(s, i * 8 + 4, 62, 1, i == textCursor ? C_SEL : C_TEXT, TC_DATUM);
  }
  spr.drawFastHLine(textCursor * 8, 74, 8, C_SEL);
  char big[2] = {textBuf[textCursor], 0};
  text(big[0] == ' ' ? "_" : big, W / 2, 30, 2, C_SEL, TC_DATUM);
  text("up/down: letter", W / 2, 96, 1, C_DIM, TC_DATUM);
  text(textFixed ? "left/right: move" : "right: add / move", W / 2, 108, 1, C_DIM, TC_DATUM);
  footer("J1 ok   J2 cancel");
}

void testScreen() {
  if (Inputs::pressed(D_J2SW)) {
    go(S_MENU, 5);
    return;
  }
  header("Input test");

  const char* names[A_COUNT] = {"J1X", "J1Y", "J2X", "J2Y", "P1", "P2"};
  for (int i = 0; i < A_COUNT; i++) {
    int y = HEADER_H + 4 + i * 16;
    AnalogId id = (AnalogId)i;
    char buf[12];
    text(names[i], 2, y, 1, C_TEXT);
    snprintf(buf, sizeof(buf), "%4u", Inputs::raw(id));
    text(buf, 28, y, 1, C_DIM);
    snprintf(buf, sizeof(buf), "%+.2f", Inputs::norm(id));
    text(buf, W - 2, y, 1, C_SEL, TR_DATUM);
    drawBar(2, y + 9, W - 4, 3, Inputs::norm(id), i < 4, C_SEL);
  }

  drawSwitches(HEADER_H + 4 + 6 * 16 + 2);
  spr.fillRoundRect(3, H - FOOTER_H - 14, 28, 12, 3, Inputs::on(D_J1SW) ? C_OK : C_BAR);
  text("B1", 17, H - FOOTER_H - 12, 1, Inputs::on(D_J1SW) ? TFT_BLACK : C_DIM, TC_DATUM);
  spr.fillRoundRect(34, H - FOOTER_H - 14, 28, 12, 3, Inputs::on(D_J2SW) ? C_OK : C_BAR);
  text("B2", 48, H - FOOTER_H - 12, 1, Inputs::on(D_J2SW) ? TFT_BLACK : C_DIM, TC_DATUM);

  static const char* navNames[] = {"", "UP", "DOWN", "LEFT", "RIGHT"};
  text(navNames[Inputs::nav()], W - 3, H - FOOTER_H - 12, 1, C_SEL, TR_DATUM);
  footer("J2 back");
}

}  // namespace

namespace Ui {

void begin() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);
  spr.setColorDepth(16);
  if (!spr.createSprite(W, H)) Serial.println("sprite: no memory");

  spr.fillSprite(C_BG);
  text("PULT", W / 2, 60, 4, C_SEL, TC_DATUM);
  text("Korablik", W / 2, 95, 2, C_DIM, TC_DATUM);
  spr.pushSprite(0, 0);
  delay(800);

  if (!Inputs::cal().done) startCalibration(true);
  else go(S_CONTROL);
}

void update() {
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  spr.fillSprite(C_BG);
  switch (screen) {
    case S_CONTROL:  controlScreen();  break;
    case S_MENU:     menuScreen();     break;
    case S_PRESETS:  presetsScreen();  break;
    case S_NEWMODEL: newModelScreen(); break;
    case S_CALIB:    calibScreen();    break;
    case S_TEST:     testScreen();     break;
    case S_CONFIRM:  confirmScreen();  break;
    case S_EDIT:      editScreen();     break;
    case S_EDIT_AXIS: editAxisScreen(); break;
    case S_TEXT:      textScreen();     break;
  }
  spr.pushSprite(0, 0);
  Inputs::clearEvents();

  // Передаём только пока открыт экран управления: в меню и калибровке приёмник
  // остаётся без команд и сам уходит в failsafe.
  Radio::setActive(screen == S_CONTROL);
}

}  // namespace Ui
