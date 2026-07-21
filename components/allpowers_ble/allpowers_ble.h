// SPDX-License-Identifier: MIT
//
// ESPHome BLE component for compatible ALLPOWERS power stations.
// BLE protocol mapping and command logic are derived from:
// https://github.com/madninjaskillz/allpowers-ble

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>

namespace esphome::allpowers_ble {

namespace espbt = esphome::esp32_ble_tracker;

enum class OutputType : uint8_t { AC = 0, DC = 1, LIGHT = 2 };

class AllpowersBLESwitch;
class AllpowersBLEEcoSwitch;

class AllpowersBLE final : public Component, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_service_uuid16(uint16_t uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_uint16(uuid); }
  void set_service_uuid32(uint32_t uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_uint32(uuid); }
  void set_service_uuid128(const uint8_t *uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }
  void set_stale_timeout(uint32_t timeout_ms) { this->stale_timeout_ms_ = timeout_ms; }

  void set_soc_sensor(sensor::Sensor *sensor) { this->soc_sensor_ = sensor; }
  void set_input_power_sensor(sensor::Sensor *sensor) { this->input_power_sensor_ = sensor; }
  void set_output_power_sensor(sensor::Sensor *sensor) { this->output_power_sensor_ = sensor; }
  void set_remaining_time_sensor(sensor::Sensor *sensor) { this->remaining_time_sensor_ = sensor; }
  void set_ac_frequency_sensor(sensor::Sensor *sensor) { this->ac_frequency_sensor_ = sensor; }
  void set_status_byte_sensor(sensor::Sensor *sensor) { this->status_byte_sensor_ = sensor; }
  void set_packet_length_sensor(sensor::Sensor *sensor) { this->packet_length_sensor_ = sensor; }

  void set_connected_binary_sensor(binary_sensor::BinarySensor *sensor) { this->connected_binary_sensor_ = sensor; }
  void set_data_valid_binary_sensor(binary_sensor::BinarySensor *sensor) { this->data_valid_binary_sensor_ = sensor; }
  void set_controls_available_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->controls_available_binary_sensor_ = sensor;
  }
  void set_settings_available_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->settings_available_binary_sensor_ = sensor;
  }
  void set_ac_output_binary_sensor(binary_sensor::BinarySensor *sensor) { this->ac_output_binary_sensor_ = sensor; }
  void set_dc_output_binary_sensor(binary_sensor::BinarySensor *sensor) { this->dc_output_binary_sensor_ = sensor; }
  void set_eco_mode_binary_sensor(binary_sensor::BinarySensor *sensor) { this->eco_mode_binary_sensor_ = sensor; }
  void set_light_binary_sensor(binary_sensor::BinarySensor *sensor) { this->light_binary_sensor_ = sensor; }
  void set_charging_binary_sensor(binary_sensor::BinarySensor *sensor) { this->charging_binary_sensor_ = sensor; }
  void set_discharging_binary_sensor(binary_sensor::BinarySensor *sensor) { this->discharging_binary_sensor_ = sensor; }
  void set_protocol_error_binary_sensor(binary_sensor::BinarySensor *sensor) {
    this->protocol_error_binary_sensor_ = sensor;
  }

  void set_ac_switch(AllpowersBLESwitch *sw) { this->ac_switch_ = sw; }
  void set_dc_switch(AllpowersBLESwitch *sw) { this->dc_switch_ = sw; }
  void set_light_switch(AllpowersBLESwitch *sw) { this->light_switch_ = sw; }
  void set_eco_switch(AllpowersBLEEcoSwitch *sw) { this->eco_switch_ = sw; }

  bool request_output(OutputType output, bool state);
  bool request_eco_mode(bool state);

 protected:
  static constexpr uint16_t NOTIFY_UUID = 0xFFF1;
  static constexpr uint16_t WRITE_UUID = 0xFFF2;
  static constexpr size_t MIN_FRAME_LENGTH = 8;
  static constexpr size_t MIN_STATUS_PACKET_LENGTH = 16;
  static constexpr size_t MIN_SETTINGS_PACKET_LENGTH = 14;

  static constexpr size_t PAYLOAD_LENGTH_OFFSET = 5;
  static constexpr size_t COMMAND_OFFSET = 6;
  static constexpr uint8_t STATUS_COMMAND = 0x01;
  static constexpr uint8_t SETTINGS_STATUS_COMMAND = 0x03;

  // Verified offsets in the status notification format used by the upstream
  // implementation. Keeping them named avoids scattering protocol magic
  // numbers through the parser.
  static constexpr size_t STATUS_OFFSET = 7;
  static constexpr size_t SOC_OFFSET = 8;
  static constexpr size_t INPUT_POWER_OFFSET = 9;
  static constexpr size_t OUTPUT_POWER_OFFSET = 11;
  static constexpr size_t REMAINING_TIME_OFFSET = 13;

  // The settings notification and write command share the settings bitmap and
  // ECO timeout fields. Only ECO is exposed today; retaining the complete raw
  // values allows future controls to be added without changing the safety
  // invariant or overwriting fields this component does not manage yet.
  static constexpr size_t SETTINGS_FLAGS_OFFSET = 7;
  static constexpr size_t SETTINGS_ECO_TIME_OFFSET = 8;
  static constexpr uint8_t SETTINGS_ECO_MASK = 1U << 0U;

  static constexpr uint8_t STATUS_DC_MASK = 1U << 0U;
  static constexpr uint8_t STATUS_AC_MASK = 1U << 1U;
  static constexpr uint8_t STATUS_FREQUENCY_MASK = 1U << 2U;
  static constexpr uint8_t STATUS_LIGHT_MASK = 1U << 4U;

  static constexpr uint8_t CONTROL_DC_MASK = 1U << 0U;
  static constexpr uint8_t CONTROL_AC_MASK = 1U << 1U;
  static constexpr uint8_t CONTROL_LIGHT_MASK = 1U << 5U;

  bool validate_notification_(const uint8_t *data, uint16_t length) const;
  void process_notification_(const uint8_t *data, uint16_t length);
  void process_status_notification_(const uint8_t *data, uint16_t length);
  void process_settings_notification_(const uint8_t *data, uint16_t length);
  bool send_control_frame_();
  bool send_settings_frame_();
  bool write_frame_(uint8_t *data, size_t length, const char *description);
  bool controls_available_() const;
  bool settings_available_() const;
  void publish_switch_states_();
  void publish_eco_state_();
  void invalidate_data_entities_();
  void invalidate_settings_entities_();
  void publish_controls_available_(bool state);
  void publish_settings_available_(bool state);
  void set_protocol_error_(bool state);
  void reset_connection_state_();

  espbt::ESPBTUUID service_uuid_{espbt::ESPBTUUID::from_uint16(0xFFF0)};
  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};
  esp_gatt_char_prop_t write_properties_{};

  // A BLE link alone is not enough to make output control safe. have_status_
  // means a complete frame established the current AC/DC/light bitmap;
  // data_fresh_ means that snapshot has not exceeded stale_timeout_ms_.
  bool have_status_{false};
  bool data_fresh_{false};
  bool ac_on_{false};
  bool dc_on_{false};
  bool light_on_{false};

  // Settings writes are read-modify-write operations. A fresh command-0x03
  // notification must establish these raw values before ECO can be changed.
  bool have_settings_{false};
  bool settings_fresh_{false};
  uint8_t settings_flags_{0};
  uint8_t eco_time_{0};

  uint32_t stale_timeout_ms_{30000};
  uint32_t last_valid_packet_ms_{0};
  uint32_t last_settings_packet_ms_{0};

  sensor::Sensor *soc_sensor_{nullptr};
  sensor::Sensor *input_power_sensor_{nullptr};
  sensor::Sensor *output_power_sensor_{nullptr};
  sensor::Sensor *remaining_time_sensor_{nullptr};
  sensor::Sensor *ac_frequency_sensor_{nullptr};
  sensor::Sensor *status_byte_sensor_{nullptr};
  sensor::Sensor *packet_length_sensor_{nullptr};

  binary_sensor::BinarySensor *connected_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *data_valid_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *controls_available_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *settings_available_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *ac_output_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *dc_output_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *eco_mode_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *light_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *charging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *discharging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *protocol_error_binary_sensor_{nullptr};

  AllpowersBLESwitch *ac_switch_{nullptr};
  AllpowersBLESwitch *dc_switch_{nullptr};
  AllpowersBLESwitch *light_switch_{nullptr};
  AllpowersBLEEcoSwitch *eco_switch_{nullptr};
};

class AllpowersBLESwitch final : public switch_::Switch {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }
  void set_output_type(OutputType output_type) { this->output_type_ = output_type; }

 protected:
  void write_state(bool state) override;

  AllpowersBLE *parent_{nullptr};
  OutputType output_type_{OutputType::AC};
};

class AllpowersBLEEcoSwitch final : public switch_::Switch {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;

  AllpowersBLE *parent_{nullptr};
};

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
