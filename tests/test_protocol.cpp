// SPDX-License-Identifier: MIT
//
// Native protocol tests: compile without ESPHome, ESP-IDF or BLE headers.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
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

void test_invalid_frames() {
  constexpr std::array<uint8_t, 16> BAD_CHECKSUM{
      {0xA5, 0x65, 0xB1, 0x00, 0x01, 0x08, 0x01, 0x13, 0x64, 0x01, 0x2C, 0x00, 0xFA, 0x00, 0x78, 0x00}};
  assert(protocol::validate_frame(nullptr, 0) == protocol::ParseError::SHORT_FRAME);
  assert(protocol::validate_frame(BAD_CHECKSUM.data(), BAD_CHECKSUM.size()) == protocol::ParseError::INVALID_CHECKSUM);
}

void test_control_frames() {
  expect_bytes(protocol::make_control_frame({false, false, false}),
               std::array<uint8_t, 9>{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x00, 0x71}});
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

  std::vector<uint8_t> response = protocol::make_device_name_frame("Taller");
  std::swap(response[2], response[3]);
  response.back() = xor_bytes(response.data(), response.size() - 1);
  std::string name;
  assert(protocol::parse_device_name(response.data(), response.size(), &name) == protocol::ParseError::NONE);
  assert(name == "Taller");
  assert(protocol::make_device_name_frame(std::string(97, 'a')).empty());
}

void test_station_name_normalization() {
  std::string normalized;
  assert(protocol::normalize_station_name("  R600 Garaje  ", &normalized));
  assert(normalized == "R600 Garaje");
  assert(!protocol::normalize_station_name(" Unknown ", &normalized));
  assert(!protocol::normalize_station_name(std::string("bad\0name", 8), &normalized));
}

void test_status_request() {
  expect_bytes(protocol::status_request_frame(),
               std::array<uint8_t, 12>{{0xA5, 0x65, 0xB1, 0x00, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}});
}

}  // namespace

int main() {
  test_status_parser();
  test_settings_parser();
  test_invalid_frames();
  test_control_frames();
  test_settings_frame();
  test_device_name_codec();
  test_station_name_normalization();
  test_status_request();
  return 0;
}
