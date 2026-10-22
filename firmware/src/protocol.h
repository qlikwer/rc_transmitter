#pragma once
#include <stdint.h>

// Радиопротокол пульт <-> приёмник. Общий заголовок для пульта и приёмника.
// Пульт шлёт ControlPacket, приёмник отвечает TelemetryPacket в ACK-пакете.
// Оба пакета не длиннее 32 байт (предел nRF24L01).

constexpr uint8_t PROTO_VER = 1;
constexpr uint8_t PROTO_CHANNELS = 8;

// Биты ControlPacket::flags
constexpr uint8_t FLAG_SW1 = 1 << 0;
constexpr uint8_t FLAG_SW2 = 1 << 1;
constexpr uint8_t FLAG_SW3 = 1 << 2;
constexpr uint8_t FLAG_SW4 = 1 << 3;
constexpr uint8_t FLAG_BTN1 = 1 << 4;  // кнопка стика 1
constexpr uint8_t FLAG_BTN2 = 1 << 5;  // кнопка стика 2

// Биты TelemetryPacket::status
constexpr uint8_t RX_STATUS_FAILSAFE = 1 << 0;  // приёмник потерял пульт и держит failsafe

struct __attribute__((packed)) ControlPacket {
  uint8_t ver;
  uint8_t seq;
  uint8_t flags;
  uint16_t ch[PROTO_CHANNELS];  // 1000..2000 мкс
  uint8_t crc;
};

struct __attribute__((packed)) TelemetryPacket {
  uint8_t ver;
  uint16_t battMv;   // напряжение батареи устройства, мВ
  uint8_t status;
  uint8_t crc;
};

inline uint8_t crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
  }
  return crc;
}

template <typename T>
inline void sealPacket(T& p) {
  p.crc = crc8((const uint8_t*)&p, sizeof(T) - 1);
}

template <typename T>
inline bool packetValid(const T& p) {
  return p.ver == PROTO_VER && p.crc == crc8((const uint8_t*)&p, sizeof(T) - 1);
}
