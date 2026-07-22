// SPDX-License-Identifier: MIT
//
// ESPHome BLE component for compatible ALLPOWERS power stations.
// BLE protocol mapping and command logic are derived from:
// https://github.com/madninjaskillz/allpowers-ble

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>

namespace esphome::allpowers_ble {

namespace espbt = esphome::esp32_ble_tracker;

enum class OutputType : uint8_t { AC = 0, DC = 1, LIGHT = 2 };

class AllpowersBLESwitch;
class AllpowersBLEEcoSwitch;
class AllpowersBLECarChargerSwitch;
class AllpowersBLEEcoShutdownTimeSelect;
class AllpowersBLEWorkModeSelect;
class AllpowersBLEDeviceNameText;

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
  void set_experimental_device_name_enabled(bool enabled) { this->experimental_device_name_enabled_ = enabled; }

  void set_soc_sensor(sensor::Sensor *sensor) { this->soc_sensor_ = sensor; }
  void set_input_power_sensor(sensor::Sensor *sensor) { this->input_power_sensor_ = sensor; }
  void set_output_power_sensor(sensor::Sensor *sensor) { this->output_power_sensor_ = sensor; }
  void set_remaining_time_sensor(sensor::Sensor *sensor) { this->remaining_time_sensor_ = sensor; }
  void set_ac_frequency_sensor(sensor::Sensor *sensor) { this->ac_frequency_sensor_ = sensor; }
  void set_status_byte_sensor(sensor::Sensor *sensor) { this->status_byte_sensor_ = sensor; }
  void set_packet_length_sensor(sensor::Sensor *sensor) { this->packet_length_sensor_ = sensor; }

  void set_hardware_version_text_sensor(text_sensor::TextSensor *sensor) {
    this->hardware_version_text_sensor_ = sensor;
  }
  void set_firmware_version_text_sensor(text_sensor::TextSensor *sensor) {
    this->firmware_version_text_sensor_ = sensor;
  }

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
  void set_car_charger_switch(AllpowersBLECarChargerSwitch *sw) { this->car_charger_switch_ = sw; }
  void set_eco_shutdown_time_select(AllpowersBLEEcoShutdownTimeSelect *select) {
    this->eco_shutdown_time_select_ = select;
  }
  void set_work_mode_select(AllpowersBLEWorkModeSelect *select) { this->work_mode_select_ = select; }
  void set_device_name_text(AllpowersBLEDeviceNameText *text) { this->device_name_text_ = text; }

  bool request_output(OutputType output, bool state);
  bool request_eco_mode(bool state);
  bool request_eco_shutdown_time(uint8_t hours);
  bool request_work_mode(uint8_t mode);
  bool request_car_charger(bool state);
  bool request_device_name(const std::string &name);

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
  static constexpr uint8_t DEVICE_NAME_COMMAND = 0x35;
  static constexpr size_t MAX_DEVICE_NAME_LENGTH = 96;

  // Verified offsets in the status notification format used by the upstream
  // implementation. Keeping them named avoids scattering protocol magic
  // numbers through the parser.
  static constexpr size_t STATUS_OFFSET = 7;
  static constexpr size_t SOC_OFFSET = 8;
  static constexpr size_t INPUT_POWER_OFFSET = 9;
  static constexpr size_t OUTPUT_POWER_OFFSET = 11;
  static constexpr size_t REMAINING_TIME_OFFSET = 13;

  // The settings notification and write command share the settings bitmap and
  // ECO timeout fields. Retaining both raw values lets each exposed setting
  // use one read-modify-write path without overwriting fields that remain
  // intentionally unmanaged.
  static constexpr size_t SETTINGS_FLAGS_OFFSET = 7;
  static constexpr size_t SETTINGS_ECO_TIME_OFFSET = 8;
  static constexpr size_t SETTINGS_HARDWARE_VERSION_OFFSET = 11;
  static constexpr size_t SETTINGS_FIRMWARE_VERSION_OFFSET = 12;
  static constexpr uint8_t SETTINGS_ECO_MASK = 1U << 0U;
  static constexpr uint8_t SETTINGS_WORK_MODE_MASK = 0x06U;
  static constexpr uint8_t SETTINGS_WORK_MODE_SHIFT = 1U;
  static constexpr uint8_t SETTINGS_CAR_CHARGER_MASK = 1U << 4U;

  static constexpr uint8_t STATUS_DC_MASK = 1U << 0U;
  static constexpr uint8_t STATUS_AC_MASK = 1U << 1U;
  static constexpr uint8_t STATUS_FREQUENCY_MASK = 1U << 2U;
  static constexpr uint8_t STATUS_LIGHT_MASK = 1U << 4U;

  static constexpr uint8_t CONTROL_DC_MASK = 1U << 0U;
  static constexpr uint8_t CONTROL_AC_MASK = 1U << 1U;
  static constexpr uint8_t CONTROL_LIGHT_MASK = 1U << 5U;

  static std::string format_version_(uint8_t encoded_version);
  bool validate_notification_(const uint8_t *data, uint16_t length) const;
  void process_notification_(const uint8_t *data, uint16_t length);
  void process_status_notification_(const uint8_t *data, uint16_t length);
  void process_settings_notification_(const uint8_t *data, uint16_t length);
  void process_device_name_notification_(const uint8_t *data, uint16_t length);
  bool request_device_name_query_();
  bool send_device_name_frame_(const std::string &name);
  bool valid_utf8_(const uint8_t *data, size_t length) const;
  bool send_control_frame_();
  bool send_settings_frame_();
  bool write_frame_(uint8_t *data, size_t length, const char *description);
  bool controls_available_() const;
  bool settings_available_() const;
  void publish_switch_states_();
  void publish_settings_states_();
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
  bool experimental_device_name_enabled_{false};

  // Settings writes are read-modify-write operations. A fresh command-0x03
  // notification must establish these raw values before ECO mode, its shutdown
  // time, work mode or the independent car-charger output can be changed.
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

  text_sensor::TextSensor *hardware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};

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
  AllpowersBLECarChargerSwitch *car_charger_switch_{nullptr};
  AllpowersBLEEcoShutdownTimeSelect *eco_shutdown_time_select_{nullptr};
  AllpowersBLEWorkModeSelect *work_mode_select_{nullptr};
  AllpowersBLEDeviceNameText *device_name_text_{nullptr};
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

class AllpowersBLECarChargerSwitch final : public switch_::Switch {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;

  AllpowersBLE *parent_{nullptr};
};

class AllpowersBLEEcoShutdownTimeSelect final : public select::Select {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }
  void publish_hours(uint8_t hours);
  void clear_state() { this->set_has_state(false); }

 protected:
  void control(size_t index) override;

  AllpowersBLE *parent_{nullptr};
};

class AllpowersBLEWorkModeSelect final : public select::Select {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }
  void publish_mode(uint8_t mode);
  void clear_state() { this->set_has_state(false); }

 protected:
  void control(size_t index) override;

  AllpowersBLE *parent_{nullptr};
};

class AllpowersBLEDeviceNameText final : public text::Text {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }
  void clear_state() { this->set_has_state(false); }

 protected:
  void control(const std::string &value) override;

  AllpowersBLE *parent_{nullptr};
};

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
