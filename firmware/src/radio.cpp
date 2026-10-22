#include "radio.h"

#include <RF24.h>

#include "config.h"
#include "inputs.h"
#include "presets.h"
#include "protocol.h"

namespace {

// SPI.begin() уже вызван TFT_eSPI (SCK 18, MISO 19, MOSI 23): RF24 использует ту же шину.
RF24 rf(PIN_NRF_CE, PIN_NRF_CSN);

constexpr uint32_t kSendPeriodMs = 20;       // 50 кадров в секунду
constexpr uint32_t kTelemetryTimeoutMs = 1000;
constexpr uint8_t kLinkOkPercent = 40;        // ниже считаем, что связи нет

bool present = false;
bool active = false;
Radio::State st = Radio::R_OFF;

char appliedId[8] = "";
char appliedAddr[6] = "";
uint8_t appliedChannel = 0;

uint32_t lastSend = 0;
uint8_t seq = 0;
uint32_t history = 0xFFFFFFFF;  // 1 = пакет подтверждён, последние 32 отправки

TelemetryPacket tel;
uint32_t telAt = 0;
bool telEver = false;

void applyPreset(const Preset& p) {
  rf.setChannel(p.radioCh);
  rf.openWritingPipe((const uint8_t*)p.addr);  // 5 байт адреса = первые 5 символов
  strlcpy(appliedId, p.id, sizeof(appliedId));
  strlcpy(appliedAddr, p.addr, sizeof(appliedAddr));
  appliedChannel = p.radioCh;
  history = 0;
  telEver = false;
}

bool presetChanged(const Preset& p) {
  return strcmp(appliedId, p.id) != 0 || strcmp(appliedAddr, p.addr) != 0 || appliedChannel != p.radioCh;
}

uint8_t flagsNow() {
  uint8_t f = 0;
  if (Inputs::on(D_SW1)) f |= FLAG_SW1;
  if (Inputs::on(D_SW2)) f |= FLAG_SW2;
  if (Inputs::on(D_SW3)) f |= FLAG_SW3;
  if (Inputs::on(D_SW4)) f |= FLAG_SW4;
  if (Inputs::on(D_J1SW)) f |= FLAG_BTN1;
  if (Inputs::on(D_J2SW)) f |= FLAG_BTN2;
  return f;
}

}  // namespace

namespace Radio {

bool begin() {
  present = rf.begin() && rf.isChipConnected();
  if (!present) {
    st = R_ERROR;
    Serial.println("nRF24: module not found");
    return false;
  }
  rf.setDataRate(RF24_250KBPS);  // максимум дальности
  rf.setPALevel(RF24_PA_MAX);
  rf.setAddressWidth(5);
  rf.setAutoAck(true);
  rf.enableDynamicPayloads();
  rf.enableAckPayload();
  rf.setRetries(5, 3);  // пауза 1,5 мс (хватает для ACK с данными), до 3 повторов
  rf.setCRCLength(RF24_CRC_16);
  rf.stopListening();
  st = R_OFF;
  return true;
}

void setActive(bool a) {
  if (!present) return;
  active = a;
  if (!a) st = R_OFF;
}

void update() {
  if (!present || !active) return;

  uint32_t now = millis();
  if (now - lastSend < kSendPeriodMs) return;
  lastSend = now;

  const Preset& p = Presets::at(Presets::activeIndex());
  if (presetChanged(p)) applyPreset(p);

  ControlPacket pkt;
  pkt.ver = PROTO_VER;
  pkt.seq = seq++;
  pkt.flags = flagsNow();
  uint16_t ch[CHANNELS];
  Presets::compute(p, ch);
  for (uint8_t i = 0; i < PROTO_CHANNELS; i++) pkt.ch[i] = ch[i];
  sealPacket(pkt);

  bool acked = rf.write(&pkt, sizeof(pkt));
  history = (history << 1) | (acked ? 1 : 0);

  if (acked) {
    while (rf.available()) {
      uint8_t len = rf.getDynamicPayloadSize();
      if (len == sizeof(TelemetryPacket)) {
        TelemetryPacket t;
        rf.read(&t, sizeof(t));
        if (packetValid(t)) {
          tel = t;
          telAt = now;
          telEver = true;
        }
      } else {
        rf.flush_rx();  // чужой/битый ответ
      }
    }
  }

  st = quality() >= kLinkOkPercent ? R_LINK : R_NOLINK;
}

State state() { return st; }

uint8_t quality() { return __builtin_popcount(history) * 100 / 32; }

bool hasTelemetry() { return telEver && millis() - telAt < kTelemetryTimeoutMs; }
uint16_t batteryMv() { return tel.battMv; }
bool rxFailsafe() { return hasTelemetry() && (tel.status & RX_STATUS_FAILSAFE); }

}  // namespace Radio
