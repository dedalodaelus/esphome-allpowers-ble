// SPDX-License-Identifier: MIT
//
// Native protocol tests: compile without ESPHome, ESP-IDF or BLE headers.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "allpowers_ble_protocol.h"

namespace protocol = esphome::allpowers_ble::protocol;

namespace {

uint8_t xor_bytes(const uint8_t *data, size_t length) {
  uint8_t checksum = 0;
  for (size_t index = 0; index < length; index++)
    checksum ^= data[index];
  return checksum;
}

std::vector<uint8_t> make_notification(uint8_t command, std::initializer_list<uint8_t> payload) {
  std::vector<uint8_t> frame{
      0xA5, 0x65, 0xB1, 0x00, 0x01, static_cast<uint8_t>(payload.size()), command,
  };
  frame.insert(frame.end(), payload);
  frame.push_back(xor_bytes(frame.data(), frame.size()));
  return frame;
}

template<size_t N> void expect_bytes(const std::array<uint8_t, N> &actual, const std::array<uint8_t, N> &expected) {
  assert(actual == expected);
}

void test_status_parser() {
  constexpr std::array<uint8_t, 16> PACKET{
      {0xA5, 0x65, 0xB1, 0x00, 0x01, 0x08, 0x01, 0x13, 0x64, 0x01, 0x2C, 0x00, 0xFA, 0x00, 0x78, 0xA1}};
  protocol::StatusData status;
  assert(protocol::parse_status(PACKET.data(), PACKET.size(), &status) == protocol::ParseError::NONE);
  assert(status.dc_on && status.ac_on && status.light_on);
  assert(status.ac_frequency_hz == 50);
  assert(status.soc == 100);
  assert(status.input_power == 300);
  assert(status.output_power == 250);
  assert(status.remaining_minutes == 120);
}

void test_status_boundaries() {
  const std::vector<uint8_t> packet =
      make_notification(protocol::STATUS_COMMAND, {0x04, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF});
  protocol::StatusData status;
  assert(protocol::parse_status(packet.data(), packet.size(), &status) == protocol::ParseError::NONE);
  assert(!status.dc_on && !status.ac_on && !status.light_on);
  assert(status.ac_frequency_hz == 60);
  assert(status.soc == 0);
  assert(status.input_power == 65535);
  assert(status.output_power == 0);
  assert(status.remaining_minutes == 65535);
}

void test_settings_parser() {
  constexpr std::array<uint8_t, 14> PACKET{
      {0xA5, 0x65, 0xB1, 0x00, 0x01, 0x06, 0x03, 0x37, 0x04, 0x00, 0x5A, 0x12, 0x34, 0x3A}};
  protocol::SettingsData settings;
  assert(protocol::parse_settings(PACKET.data(), PACKET.size(), &settings) == protocol::ParseError::NONE);
  assert(settings.flags == 0x37);
  assert(settings.eco_time == 4);
  assert(settings.eco_enabled);
  assert(settings.work_mode == 3);
  assert(settings.car_charger_enabled);
  assert(settings.hardware_version == "1.2");
  assert(settings.firmware_version == "3.4");
}

void test_version_formatter() {
  assert(protocol::format_version(0x01) == "0.1");
  assert(protocol::format_version(0x10) == "1.0");
  assert(protocol::format_version(0xAF) == "0xAF");
}

void test_invalid_frames() {
  assert(protocol::validate_frame(nullptr, 0) == protocol::ParseError::SHORT_FRAME);

  const std::array<uint8_t, 2> short_frame{{0xA5, 0x65}};
  assert(protocol::validate_frame(short_frame.data(), short_frame.size()) == protocol::ParseError::SHORT_FRAME);

  std::vector<uint8_t> frame =
      make_notification(protocol::STATUS_COMMAND, {0x13, 0x64, 0x01, 0x2C, 0x00, 0xFA, 0x00, 0x78});
  frame[0] = 0x00;
  assert(protocol::validate_frame(frame.data(), frame.size()) == protocol::ParseError::INVALID_HEADER);

  frame = make_notification(protocol::STATUS_COMMAND, {0x13, 0x64, 0x01, 0x2C, 0x00, 0xFA, 0x00, 0x78});
  frame[protocol::PAYLOAD_LENGTH_OFFSET]--;
  assert(protocol::validate_frame(frame.data(), frame.size()) == protocol::ParseError::INCONSISTENT_LENGTH);

  frame = make_notification(protocol::STATUS_COMMAND, {0x13, 0x64, 0x01, 0x2C, 0x00, 0xFA, 0x00, 0x78});
  frame.back() ^= 0x01;
  assert(protocol::validate_frame(frame.data(), frame.size()) == protocol::ParseError::INVALID_CHECKSUM);
}

void test_command_and_payload_errors() {
  protocol::StatusData status;
  protocol::SettingsData settings;

  const std::vector<uint8_t> settings_packet =
      make_notification(protocol::SETTINGS_STATUS_COMMAND, {0x37, 0x04, 0x00, 0x5A, 0x12, 0x34});
  assert(protocol::parse_status(settings_packet.data(), settings_packet.size(), &status) ==
         protocol::ParseError::WRONG_COMMAND);

  const std::vector<uint8_t> incomplete_status =
      make_notification(protocol::STATUS_COMMAND, {0x13, 0x64, 0x01, 0x2C, 0x00, 0xFA, 0x00});
  assert(protocol::parse_status(incomplete_status.data(), incomplete_status.size(), &status) ==
         protocol::ParseError::INCOMPLETE_STATUS);

  const std::vector<uint8_t> incomplete_settings =
      make_notification(protocol::SETTINGS_STATUS_COMMAND, {0x37, 0x04, 0x00, 0x5A, 0x12});
  assert(protocol::parse_settings(incomplete_settings.data(), incomplete_settings.size(), &settings) ==
         protocol::ParseError::INCOMPLETE_SETTINGS);
}

void test_control_frames() {
  expect_bytes(protocol::make_control_frame({false, false, false}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x00, 0x71}});
  expect_bytes(protocol::make_control_frame({true, false, false}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x01, 0x70}});
  expect_bytes(protocol::make_control_frame({false, true, false}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x02, 0x73}});
  expect_bytes(protocol::make_control_frame({true, true, false}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x03, 0x72}});
  expect_bytes(protocol::make_control_frame({false, false, true}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x20, 0x51}});
  expect_bytes(protocol::make_control_frame({true, false, true}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x21, 0x50}});
  expect_bytes(protocol::make_control_frame({false, true, true}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x22, 0x53}});
  expect_bytes(protocol::make_control_frame({true, true, true}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x23, 0x52}});
}

void test_settings_frame() {
  const protocol::SettingsFrame frame = protocol::make_settings_frame(0x37, 0x04);
  expect_bytes(frame, std::array<uint8_t, 10>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x02, 0x02, 0x37, 0x04, 0x43}});
  assert(xor_bytes(frame.data(), frame.size()) == 0);
}

void test_device_name_codec() {
  const std::vector<uint8_t> query = protocol::make_device_name_frame("");
  const std::vector<uint8_t> expected_query{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x00, 0x35, 0x45};
  assert(query == expected_query);

  const std::string station_name = u8"Estación ⚡";
  std::vector<uint8_t> response = protocol::make_device_name_frame(station_name);
  std::swap(response[2], response[3]);
  response.back() = xor_bytes(response.data(), response.size() - 1);
  std::string name;
  assert(protocol::parse_device_name(response.data(), response.size(), &name) == protocol::ParseError::NONE);
  assert(name == station_name);

  const std::string maximum_name(protocol::MAX_DEVICE_NAME_LENGTH, 'A');
  const std::vector<uint8_t> maximum_frame = protocol::make_device_name_frame(maximum_name);
  assert(maximum_frame.size() == protocol::MIN_FRAME_LENGTH + protocol::MAX_DEVICE_NAME_LENGTH);
  assert(maximum_frame[protocol::PAYLOAD_LENGTH_OFFSET] == protocol::MAX_DEVICE_NAME_LENGTH);
  assert(protocol::make_device_name_frame(std::string(97, 'a')).empty());

  const std::string invalid_utf8("\xC0\xAF", 2);
  assert(protocol::make_device_name_frame(invalid_utf8).empty());
  const std::vector<uint8_t> invalid_utf8_response = make_notification(protocol::DEVICE_NAME_COMMAND, {0xC0, 0xAF});
  assert(protocol::parse_device_name(invalid_utf8_response.data(), invalid_utf8_response.size(), &name) ==
         protocol::ParseError::INVALID_UTF8);

  const std::vector<uint8_t> wrong_command = make_notification(protocol::STATUS_COMMAND, {});
  assert(protocol::parse_device_name(wrong_command.data(), wrong_command.size(), &name) ==
         protocol::ParseError::WRONG_COMMAND);
}

void test_station_name_normalization() {
  std::string normalized;
  assert(protocol::normalize_station_name("  R600 Garaje  ", &normalized));
  assert(normalized == "R600 Garaje");
  constexpr std::array<const char *, 16> INVALID_NAMES{{
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
  for (const char *invalid_name : INVALID_NAMES)
    assert(!protocol::normalize_station_name(invalid_name, &normalized));

  assert(!protocol::normalize_station_name("   ", &normalized));
  assert(!protocol::normalize_station_name(std::string("bad\0name", 8), &normalized));
  assert(!protocol::normalize_station_name(std::string(protocol::MAX_DEVICE_NAME_LENGTH + 1, 'A'), &normalized));
}

void test_status_request() {
  expect_bytes(protocol::status_request_frame(),
               std::array<uint8_t, 12>{{0xA5, 0x65, 0xB1, 0x00, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}});
}

}  // namespace

int main() {
  test_status_parser();
  test_status_boundaries();
  test_settings_parser();
  test_version_formatter();
  test_invalid_frames();
  test_command_and_payload_errors();
  test_control_frames();
  test_settings_frame();
  test_device_name_codec();
  test_station_name_normalization();
  test_status_request();
  return 0;
}
