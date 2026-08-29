#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sticky_note {

constexpr std::array<uint8_t, 4> MAGIC = {'C', 'I', 'N', 'T'};
constexpr uint8_t VERSION = 1;
constexpr uint8_t TYPE_NOTE = 1;
constexpr uint8_t TYPE_ACK = 2;
constexpr size_t HEADER_BYTES = 16;
constexpr size_t MAX_MESSAGE_BYTES = 220;
constexpr size_t MAX_PACKET_BYTES = HEADER_BYTES + MAX_MESSAGE_BYTES;

struct Note {
  uint32_t sequence = 0;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  std::array<char, MAX_MESSAGE_BYTES + 1> message{};
  uint8_t messageLength = 0;
};

inline uint16_t readU16Le(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8);
}

inline uint32_t readU32Le(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

inline void writeU16Le(uint8_t* output, const uint16_t value) {
  output[0] = static_cast<uint8_t>(value & 0xffU);
  output[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
}

inline void writeU32Le(uint8_t* output, const uint32_t value) {
  output[0] = static_cast<uint8_t>(value & 0xffU);
  output[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
  output[2] = static_cast<uint8_t>((value >> 16) & 0xffU);
  output[3] = static_cast<uint8_t>((value >> 24) & 0xffU);
}

inline bool validDate(const uint16_t year, const uint8_t month, const uint8_t day) {
  if (year < 2024 || year > 2099 || month == 0 || month > 12 || day == 0) return false;
  static constexpr uint8_t DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t daysInMonth = DAYS_PER_MONTH[month - 1];
  if (month == 2 && (year % 4U == 0U) && (year % 100U != 0U || year % 400U == 0U)) ++daysInMonth;
  return day <= daysInMonth;
}

inline bool validUtf8(const uint8_t* data, const size_t length) {
  size_t i = 0;
  while (i < length) {
    const uint8_t first = data[i++];
    if (first < 0x80) {
      if (first == 0 || (first < 0x20 && first != '\n' && first != '\r' && first != '\t')) return false;
      continue;
    }

    uint8_t continuationCount = 0;
    uint32_t codepoint = 0;
    uint32_t minimum = 0;
    if ((first & 0xe0U) == 0xc0U) {
      continuationCount = 1;
      codepoint = first & 0x1fU;
      minimum = 0x80;
    } else if ((first & 0xf0U) == 0xe0U) {
      continuationCount = 2;
      codepoint = first & 0x0fU;
      minimum = 0x800;
    } else if ((first & 0xf8U) == 0xf0U) {
      continuationCount = 3;
      codepoint = first & 0x07U;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (i + continuationCount > length) return false;
    for (uint8_t j = 0; j < continuationCount; ++j) {
      const uint8_t next = data[i++];
      if ((next & 0xc0U) != 0x80U) return false;
      codepoint = (codepoint << 6) | (next & 0x3fU);
    }
    if (codepoint < minimum || codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return false;
  }
  return true;
}

inline bool parseNote(const uint8_t* data, const size_t length, Note& output) {
  if (!data || length < HEADER_BYTES || !std::equal(MAGIC.begin(), MAGIC.end(), data) || data[4] != VERSION ||
      data[5] != TYPE_NOTE || data[6] != 0) {
    return false;
  }
  const uint8_t messageLength = data[7];
  if (messageLength == 0 || messageLength > MAX_MESSAGE_BYTES || length != HEADER_BYTES + messageLength) return false;

  Note parsed;
  parsed.sequence = readU32Le(data + 8);
  parsed.year = readU16Le(data + 12);
  parsed.month = data[14];
  parsed.day = data[15];
  if (parsed.sequence == 0 || !validDate(parsed.year, parsed.month, parsed.day) ||
      !validUtf8(data + HEADER_BYTES, messageLength)) {
    return false;
  }

  parsed.messageLength = messageLength;
  memcpy(parsed.message.data(), data + HEADER_BYTES, messageLength);
  parsed.message[messageLength] = '\0';
  for (uint8_t i = 0; i < messageLength; ++i) {
    if (parsed.message[i] == '\r' || parsed.message[i] == '\t') parsed.message[i] = ' ';
  }
  output = parsed;
  return true;
}

inline std::array<uint8_t, HEADER_BYTES> makeAck(const uint32_t sequence) {
  std::array<uint8_t, HEADER_BYTES> packet{};
  std::copy(MAGIC.begin(), MAGIC.end(), packet.begin());
  packet[4] = VERSION;
  packet[5] = TYPE_ACK;
  writeU32Le(packet.data() + 8, sequence);
  return packet;
}

}  // namespace sticky_note
