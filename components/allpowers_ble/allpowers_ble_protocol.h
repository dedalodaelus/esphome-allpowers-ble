// SPDX-License-Identifier: MIT
//
// ALLPOWERS protocol types and codecs. This module intentionally has no
// dependency on ESPHome, ESP-IDF or the BLE stack.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome::allpowers_ble::protocol {

static constexpr size_t MIN_FRAME_LENGTH = 8;
static constexpr size_t MIN_STATUS_PACKET_LENGTH = 16;
static constexpr size_t MIN_SETTINGS_PACKET_LENGTH = 14;
static constexpr size_t PAYLOAD_LENGTH_OFFSET = 5;
static constexpr size_t COMMAND_OFFSET = 6;
static constexpr uint8_t STATUS_COMMAND = 0x01;
static constexpr uint8_t SETTINGS_STATUS_COMMAND = 0x03;
static constexpr uint8_t DEVICE_NAME_COMMAND = 0x35;
static constexpr size_t MAX_DEVICE_NAME_LENGTH = 96;

static constexpr uint8_t SETTINGS_ECO_MASK = 1U << 0U;
static constexpr uint8_t SETTINGS_WORK_MODE_MASK = 0x06U;
static constexpr uint8_t SETTINGS_WORK_MODE_SHIFT = 1U;
static constexpr uint8_t SETTINGS_CAR_CHARGER_MASK = 1U << 4U;
static constexpr std::array<uint8_t, 4> ECO_SHUTDOWN_HOURS{{1, 2, 4, 6}};
static constexpr std::array<uint8_t, 3> WORK_MODES{{0, 1, 2}};

enum class ParseError : uint8_t {
  NONE = 0,
  SHORT_FRAME,
  INVALID_HEADER,
  INCONSISTENT_LENGTH,
  INVALID_CHECKSUM,
  WRONG_COMMAND,
  INCOMPLETE_STATUS,
  INCOMPLETE_SETTINGS,
  INVALID_DEVICE_NAME_LENGTH,
  INVALID_UTF8,
};

struct StatusData {
  uint8_t status{0};
  uint8_t soc{0};
  uint16_t input_power{0};
  uint16_t output_power{0};
  uint16_t remaining_minutes{0};
  bool dc_on{false};
  bool ac_on{false};
  bool light_on{false};
  uint8_t ac_frequency_hz{50};
};

struct SettingsData {
  uint8_t flags{0};
  uint8_t eco_time{0};
  bool eco_enabled{false};
  uint8_t work_mode{0};
  bool car_charger_enabled{false};
  std::string hardware_version;
  std::string firmware_version;
};

struct OutputState {
  bool dc_on{false};
  bool ac_on{false};
  bool light_on{false};
};

using ControlFrame = std::array<uint8_t, 9>;
using SettingsFrame = std::array<uint8_t, 10>;
using StatusRequestFrame = std::array<uint8_t, 12>;

ParseError validate_frame(const uint8_t *data, size_t length);
ParseError parse_status(const uint8_t *data, size_t length, StatusData *status);
ParseError parse_settings(const uint8_t *data, size_t length, SettingsData *settings);
ParseError parse_device_name(const uint8_t *data, size_t length, std::string *name);
const char *parse_error_message(ParseError error);

bool valid_utf8(const uint8_t *data, size_t length);
bool normalize_station_name(const std::string &name, std::string *normalized_name);
std::string format_version(uint8_t encoded_version);

ControlFrame make_control_frame(const OutputState &state);
SettingsFrame make_settings_frame(uint8_t settings_flags, uint8_t eco_time);
std::vector<uint8_t> make_device_name_frame(const std::string &name);
const StatusRequestFrame &status_request_frame();

}  // namespace esphome::allpowers_ble::protocol
