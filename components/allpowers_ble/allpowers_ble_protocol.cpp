// SPDX-License-Identifier: MIT

#include "allpowers_ble_protocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace esphome::allpowers_ble::protocol {

namespace {

static constexpr size_t STATUS_OFFSET = 7;
static constexpr size_t SOC_OFFSET = 8;
static constexpr size_t INPUT_POWER_OFFSET = 9;
static constexpr size_t OUTPUT_POWER_OFFSET = 11;
static constexpr size_t REMAINING_TIME_OFFSET = 13;
static constexpr size_t SETTINGS_FLAGS_OFFSET = 7;
static constexpr size_t SETTINGS_ECO_TIME_OFFSET = 8;
static constexpr size_t SETTINGS_HARDWARE_VERSION_OFFSET = 11;
static constexpr size_t SETTINGS_FIRMWARE_VERSION_OFFSET = 12;
static constexpr uint8_t STATUS_DC_MASK = 1U << 0U;
static constexpr uint8_t STATUS_AC_MASK = 1U << 1U;
static constexpr uint8_t STATUS_FREQUENCY_MASK = 1U << 2U;
static constexpr uint8_t STATUS_LIGHT_MASK = 1U << 4U;
static constexpr uint8_t CONTROL_DC_MASK = 1U << 0U;
static constexpr uint8_t CONTROL_AC_MASK = 1U << 1U;
static constexpr uint8_t CONTROL_LIGHT_MASK = 1U << 5U;

uint8_t xor_bytes(const uint8_t *data, size_t length) {
  uint8_t checksum = 0;
  for (size_t index = 0; index < length; index++)
    checksum ^= data[index];
  return checksum;
}

}  // namespace

ParseError validate_frame(const uint8_t *data, size_t length) {
  if (data == nullptr || length < MIN_FRAME_LENGTH)
    return ParseError::SHORT_FRAME;
  if (data[0] != 0xA5 || data[1] != 0x65)
    return ParseError::INVALID_HEADER;
  if (MIN_FRAME_LENGTH + data[PAYLOAD_LENGTH_OFFSET] != length)
    return ParseError::INCONSISTENT_LENGTH;
  if (xor_bytes(data, length) != 0)
    return ParseError::INVALID_CHECKSUM;
  return ParseError::NONE;
}

ParseError parse_status(const uint8_t *data, size_t length, StatusData *status) {
  const ParseError validation = validate_frame(data, length);
  if (validation != ParseError::NONE)
    return validation;
  if (data[COMMAND_OFFSET] != STATUS_COMMAND)
    return ParseError::WRONG_COMMAND;
  if (length < MIN_STATUS_PACKET_LENGTH)
    return ParseError::INCOMPLETE_STATUS;
  if (data[SOC_OFFSET] > 100U)
    return ParseError::INVALID_STATE_OF_CHARGE;

  StatusData parsed;
  parsed.status = data[STATUS_OFFSET];
  parsed.soc = data[SOC_OFFSET];
  parsed.input_power = (static_cast<uint16_t>(data[INPUT_POWER_OFFSET]) << 8U) | data[INPUT_POWER_OFFSET + 1];
  parsed.output_power = (static_cast<uint16_t>(data[OUTPUT_POWER_OFFSET]) << 8U) | data[OUTPUT_POWER_OFFSET + 1];
  parsed.remaining_minutes =
      (static_cast<uint16_t>(data[REMAINING_TIME_OFFSET]) << 8U) | data[REMAINING_TIME_OFFSET + 1];
  parsed.dc_on = (parsed.status & STATUS_DC_MASK) != 0;
  parsed.ac_on = (parsed.status & STATUS_AC_MASK) != 0;
  parsed.light_on = (parsed.status & STATUS_LIGHT_MASK) != 0;
  parsed.ac_frequency_hz = (parsed.status & STATUS_FREQUENCY_MASK) != 0 ? 60 : 50;
  if (status != nullptr)
    *status = parsed;
  return ParseError::NONE;
}

ParseError parse_settings(const uint8_t *data, size_t length, SettingsData *settings) {
  const ParseError validation = validate_frame(data, length);
  if (validation != ParseError::NONE)
    return validation;
  if (data[COMMAND_OFFSET] != SETTINGS_STATUS_COMMAND)
    return ParseError::WRONG_COMMAND;
  if (length < MIN_SETTINGS_PACKET_LENGTH)
    return ParseError::INCOMPLETE_SETTINGS;

  SettingsData parsed;
  parsed.flags = data[SETTINGS_FLAGS_OFFSET];
  parsed.eco_time = data[SETTINGS_ECO_TIME_OFFSET];
  parsed.eco_enabled = (parsed.flags & SETTINGS_ECO_MASK) != 0;
  parsed.work_mode = (parsed.flags & SETTINGS_WORK_MODE_MASK) >> SETTINGS_WORK_MODE_SHIFT;
  parsed.car_charger_enabled = (parsed.flags & SETTINGS_CAR_CHARGER_MASK) != 0;
  parsed.hardware_version = format_version(data[SETTINGS_HARDWARE_VERSION_OFFSET]);
  parsed.firmware_version = format_version(data[SETTINGS_FIRMWARE_VERSION_OFFSET]);
  if (settings != nullptr)
    *settings = parsed;
  return ParseError::NONE;
}

ParseError parse_device_name(const uint8_t *data, size_t length, std::string *name) {
  const ParseError validation = validate_frame(data, length);
  if (validation != ParseError::NONE)
    return validation;
  if (data[COMMAND_OFFSET] != DEVICE_NAME_COMMAND)
    return ParseError::WRONG_COMMAND;

  const size_t payload_length = data[PAYLOAD_LENGTH_OFFSET];
  if (payload_length > MAX_DEVICE_NAME_LENGTH)
    return ParseError::INVALID_DEVICE_NAME_LENGTH;
  if (!valid_utf8(data + 7, payload_length))
    return ParseError::INVALID_UTF8;
  if (name != nullptr)
    name->assign(reinterpret_cast<const char *>(data + 7), payload_length);
  return ParseError::NONE;
}

const char *parse_error_message(ParseError error) {
  switch (error) {
    case ParseError::NONE:
      return "none";
    case ParseError::SHORT_FRAME:
      return "short frame";
    case ParseError::INVALID_HEADER:
      return "unknown header";
    case ParseError::INCONSISTENT_LENGTH:
      return "inconsistent payload length";
    case ParseError::INVALID_CHECKSUM:
      return "invalid XOR checksum";
    case ParseError::WRONG_COMMAND:
      return "unexpected command";
    case ParseError::INCOMPLETE_STATUS:
      return "incomplete status packet";
    case ParseError::INVALID_STATE_OF_CHARGE:
      return "state of charge is outside 0-100%";
    case ParseError::INCOMPLETE_SETTINGS:
      return "incomplete settings packet";
    case ParseError::INVALID_DEVICE_NAME_LENGTH:
      return "invalid device-name length";
    case ParseError::INVALID_UTF8:
      return "invalid UTF-8";
  }
  return "unknown parse error";
}

bool valid_utf8(const uint8_t *data, size_t length) {
  if (data == nullptr && length != 0)
    return false;

  size_t index = 0;
  while (index < length) {
    const uint8_t first = data[index++];
    if (first <= 0x7F)
      continue;

    size_t continuation_count;
    uint32_t codepoint;
    if ((first & 0xE0) == 0xC0) {
      continuation_count = 1;
      codepoint = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
      continuation_count = 2;
      codepoint = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
      continuation_count = 3;
      codepoint = first & 0x07;
    } else {
      return false;
    }

    if (index + continuation_count > length)
      return false;
    for (size_t offset = 0; offset < continuation_count; offset++) {
      const uint8_t continuation = data[index++];
      if ((continuation & 0xC0) != 0x80)
        return false;
      codepoint = (codepoint << 6U) | (continuation & 0x3F);
    }

    if ((continuation_count == 1 && codepoint < 0x80) || (continuation_count == 2 && codepoint < 0x800) ||
        (continuation_count == 3 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF))
      return false;
  }
  return true;
}

bool normalize_station_name(const std::string &name, std::string *normalized_name) {
  size_t first = 0;
  while (first < name.size() && std::isspace(static_cast<unsigned char>(name[first])))
    first++;
  size_t last = name.size();
  while (last > first && std::isspace(static_cast<unsigned char>(name[last - 1])))
    last--;

  if (first == last || last - first > MAX_DEVICE_NAME_LENGTH)
    return false;

  const std::string normalized = name.substr(first, last - first);
  if (!valid_utf8(reinterpret_cast<const uint8_t *>(normalized.data()), normalized.size()))
    return false;
  for (const unsigned char byte : normalized) {
    if (byte < 0x20 || byte == 0x7F)
      return false;
  }

  std::string lowercase;
  lowercase.reserve(normalized.size());
  for (const unsigned char byte : normalized)
    lowercase.push_back(byte < 0x80 ? static_cast<char>(std::tolower(byte)) : static_cast<char>(byte));

  static constexpr std::array<const char *, 16> INVALID_NAMES{{
      "unknown",
      "(unknown)",
      "unknown device",
      "unavailable",
      "not available",
      "not found",
      "not set",
      "n/a",
      "none",
      "null",
      "undefined",
      "failed",
      "error",
      "-",
      "--",
      "?",
  }};
  if (std::find(INVALID_NAMES.begin(), INVALID_NAMES.end(), lowercase) != INVALID_NAMES.end())
    return false;

  if (normalized_name != nullptr)
    *normalized_name = normalized;
  return true;
}

std::string format_version(uint8_t encoded_version) {
  const uint8_t major = encoded_version >> 4U;
  const uint8_t minor = encoded_version & 0x0FU;
  char buffer[5];
  if (major > 9U || minor > 9U) {
    std::snprintf(buffer, sizeof(buffer), "0x%02X", encoded_version);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%u.%u", major, minor);
  }
  return buffer;
}

ControlFrame make_control_frame(const OutputState &state) {
  ControlFrame frame{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x00, 0x71}};
  if (state.dc_on)
    frame[7] |= CONTROL_DC_MASK;
  if (state.ac_on)
    frame[7] |= CONTROL_AC_MASK;
  if (state.light_on)
    frame[7] |= CONTROL_LIGHT_MASK;
  frame[8] = static_cast<uint8_t>(113U - frame[7]);
  if (state.ac_on)
    frame[8] = static_cast<uint8_t>(frame[8] + 4U);
  return frame;
}

SettingsFrame make_settings_frame(uint8_t settings_flags, uint8_t eco_time) {
  SettingsFrame frame{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x02, 0x02, settings_flags, eco_time, 0x00}};
  frame.back() = xor_bytes(frame.data(), frame.size() - 1);
  return frame;
}

std::vector<uint8_t> make_device_name_frame(const std::string &name) {
  if (name.size() > MAX_DEVICE_NAME_LENGTH || !valid_utf8(reinterpret_cast<const uint8_t *>(name.data()), name.size()))
    return {};
  std::vector<uint8_t> frame(MIN_FRAME_LENGTH + name.size(), 0);
  frame[0] = 0xA5;
  frame[1] = 0x65;
  frame[2] = 0x00;
  frame[3] = 0xB1;
  frame[4] = 0x01;
  frame[PAYLOAD_LENGTH_OFFSET] = static_cast<uint8_t>(name.size());
  frame[COMMAND_OFFSET] = DEVICE_NAME_COMMAND;
  std::copy(name.begin(), name.end(), frame.begin() + 7);
  frame.back() = xor_bytes(frame.data(), frame.size() - 1);
  return frame;
}

const StatusRequestFrame &status_request_frame() {
  static constexpr StatusRequestFrame FRAME{{0xA5, 0x65, 0xB1, 0x00, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}};
  return FRAME;
}

}  // namespace esphome::allpowers_ble::protocol
