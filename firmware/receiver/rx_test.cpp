// Тестовый приёмник для пульта: принимает ControlPacket, печатает каналы в Serial,
// в ACK возвращает TelemetryPacket. Нужен, чтобы проверить радиоканал без кораблика.
// При потере пульта на 500 мс помечает failsafe и сообщает об этом в телеметрии
// (в настоящем приёмнике здесь нужно вернуть выходы в безопасное состояние).
#include <Arduino.h>
#include <RF24.h>

#include "protocol.h"

#ifndef RX_ADDR
#define RX_ADDR "BOAT1"
#endif
#ifndef RX_CHANNEL
#define RX_CHANNEL 76
#endif

constexpr uint8_t PIN_CE = 4;
constexpr uint8_t PIN_CSN = 5;
constexpr uint32_t FAILSAFE_MS = 500;
constexpr uint16_t FAKE_BATT_MV = 7400;  // заглушка: в кораблике здесь будет измерение батареи

RF24 radio(PIN_CE, PIN_CSN);

uint32_t lastRx = 0;
bool failsafe = true;
uint32_t lastPrint = 0;
uint32_t received = 0;
uint32_t lost = 0;
int lastSeq = -1;
ControlPacket last;

void queueTelemetry() {
  TelemetryPacket t;
  t.ver = PROTO_VER;
  t.battMv = FAKE_BATT_MV;
  t.status = failsafe ? RX_STATUS_FAILSAFE : 0;
  sealPacket(t);
  radio.writeAckPayload(1, &t, sizeof(t));
}

void setup() {
  Serial.begin(115200);
  if (!radio.begin() || !radio.isChipConnected()) {
    Serial.println("nRF24: module not found");
    while (true) delay(1000);
  }
  radio.setDataRate(RF24_250KBPS);
  radio.setPALevel(RF24_PA_LOW);  // на столе мощность не нужна
  radio.setAddressWidth(5);
  radio.setAutoAck(true);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setCRCLength(RF24_CRC_16);
  radio.setChannel(RX_CHANNEL);
  radio.openReadingPipe(1, (const uint8_t*)RX_ADDR);
  queueTelemetry();
  radio.startListening();
  Serial.printf("rx_test: addr %s, channel %d\n", RX_ADDR, RX_CHANNEL);
}

void loop() {
  uint32_t now = millis();

  while (radio.available()) {
    uint8_t len = radio.getDynamicPayloadSize();
    if (len != sizeof(ControlPacket)) {
      radio.flush_rx();
      break;
    }
    ControlPacket p;
    radio.read(&p, sizeof(p));
    if (packetValid(p)) {
      if (lastSeq >= 0) lost += (uint8_t)(p.seq - lastSeq - 1);
      lastSeq = p.seq;
      last = p;
      lastRx = now;
      received++;
      failsafe = false;
    }
    queueTelemetry();
  }

  if (!failsafe && now - lastRx > FAILSAFE_MS) {
    failsafe = true;
    lastSeq = -1;
    queueTelemetry();
    Serial.println("FAILSAFE: link lost");
  }

  if (now - lastPrint >= 250) {
    lastPrint = now;
    if (!failsafe) {
      Serial.printf("ch:");
      for (uint8_t i = 0; i < PROTO_CHANNELS; i++) Serial.printf(" %4u", last.ch[i]);
      Serial.printf(" | flags %02X | rx %lu lost %lu\n", last.flags, (unsigned long)received, (unsigned long)lost);
    }
  }
}
