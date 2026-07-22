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
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "allpowers_ble_protocol.h"
#include "allpowers_ble_transport.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>

namespace esphome::allpowers_ble {

enum class OutputType : uint8_t { AC = 0, DC = 1, LIGHT = 2 };

class AllpowersBLESwitch;
class AllpowersBLEEcoSwitch;
class AllpowersBLECarChargerSwitch;
class AllpowersBLESettingsKeepaliveSwitch;
class AllpowersBLESettingsKeepaliveIntervalNumber;
class AllpowersBLESettingsKeepaliveButton;
class AllpowersBLEStationNameTextSensor;
class AllpowersBLEEcoShutdownTimeSelect;
class AllpowersBLEWorkModeSelect;
class AllpowersBLEDeviceNameText;

class AllpowersBLE final : public Component, public AllpowersBLETransport {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_stale_timeout(uint32_t timeout_ms) { this->stale_timeout_ms_ = timeout_ms; }
  void set_keepalive_interval(uint32_t interval_ms) { this->keepalive_interval_ms_ = interval_ms; }
  void set_watchdog_timeout(uint32_t timeout_ms) { this->watchdog_timeout_ms_ = timeout_ms; }
  void set_settings_keepalive_enabled(bool enabled);
  void set_settings_keepalive_interval(uint32_t interval_ms);
  bool get_settings_keepalive_enabled() const { return this->settings_keepalive_enabled_; }
  uint32_t get_settings_keepalive_interval() const { return this->settings_keepalive_interval_ms_; }
  bool send_settings_keepalive_now();
  void set_experimental_device_name_enabled(bool enabled) { this->experimental_device_name_enabled_ = enabled; }
  uint64_t get_station_address() { return this->parent() == nullptr ? this->address_ : this->parent()->get_address(); }
  bool update_station_name(const std::string &name, const char *source, std::string *normalized_name = nullptr);

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
  void set_station_name_text_sensor(AllpowersBLEStationNameTextSensor *sensor) {
    this->station_name_text_sensor_ = sensor;
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
  static constexpr uint8_t DEVICE_NAME_QUERY_MAX_ATTEMPTS = 3;
  static constexpr uint32_t DEVICE_NAME_QUERY_INITIAL_DELAY_MS = 500;
  static constexpr uint32_t DEVICE_NAME_QUERY_RETRY_INTERVAL_MS = 3000;

  void process_notification_(const uint8_t *data, uint16_t length);
  void process_status_(const protocol::StatusData &status);
  void process_settings_(const protocol::SettingsData &settings);
  void process_device_name_(const std::string &name);
  bool request_device_name_query_();
  bool send_device_name_frame_(const std::string &name);
  bool send_status_request_();
  void start_connection_health_();
  void schedule_forced_reconnect_(const char *reason);
  void request_forced_reconnect_(const char *reason);
  bool send_control_frame_();
  bool send_settings_frame_(const char *description = "settings");
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

  void on_transport_connected_() override;
  void on_transport_disconnected_() override;
  void on_transport_ready_() override;
  void on_transport_notification_(const uint8_t *data, uint16_t length) override;
  void on_transport_error_(const char *reason, bool reconnect) override;

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
  bool have_settings_snapshot_{false};
  uint8_t settings_flags_{0};
  uint8_t eco_time_{0};

  uint32_t stale_timeout_ms_{30000};
  uint32_t keepalive_interval_ms_{20000};
  uint32_t watchdog_timeout_ms_{45000};
  bool settings_keepalive_enabled_{false};
  uint32_t settings_keepalive_interval_ms_{540000};
  uint32_t last_valid_packet_ms_{0};
  uint32_t last_settings_packet_ms_{0};
  uint32_t last_protocol_packet_ms_{0};
  uint32_t last_status_request_ms_{0};
  uint32_t last_settings_keepalive_ms_{0};
  bool connection_health_active_{false};
  bool forced_reconnect_pending_{false};
  const char *forced_reconnect_reason_{nullptr};
  bool disconnect_requested_{false};
  bool device_name_query_active_{false};
  bool initial_settings_keepalive_pending_{false};
  uint8_t device_name_query_attempts_{0};
  uint32_t next_device_name_query_ms_{0};

  sensor::Sensor *soc_sensor_{nullptr};
  sensor::Sensor *input_power_sensor_{nullptr};
  sensor::Sensor *output_power_sensor_{nullptr};
  sensor::Sensor *remaining_time_sensor_{nullptr};
  sensor::Sensor *ac_frequency_sensor_{nullptr};
  sensor::Sensor *status_byte_sensor_{nullptr};
  sensor::Sensor *packet_length_sensor_{nullptr};

  text_sensor::TextSensor *hardware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};
  AllpowersBLEStationNameTextSensor *station_name_text_sensor_{nullptr};

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

class AllpowersBLESettingsKeepaliveSwitch final : public switch_::Switch, public Component {
 public:
  explicit AllpowersBLESettingsKeepaliveSwitch(AllpowersBLE *parent) : parent_(parent) {}

  void setup() override;

 protected:
  void write_state(bool state) override;

  AllpowersBLE *parent_;
};

class AllpowersBLESettingsKeepaliveIntervalNumber final : public number::Number, public Component {
 public:
  explicit AllpowersBLESettingsKeepaliveIntervalNumber(AllpowersBLE *parent) : parent_(parent) {}

  void setup() override;

 protected:
  void control(float value) override;

  AllpowersBLE *parent_;
  ESPPreferenceObject preference_;
};

class AllpowersBLESettingsKeepaliveButton final : public button::Button {
 public:
  explicit AllpowersBLESettingsKeepaliveButton(AllpowersBLE *parent) : parent_(parent) {}

 protected:
  void press_action() override;

  AllpowersBLE *parent_;
};

class AllpowersBLEStationNameTextSensor final : public text_sensor::TextSensor, public Component {
 public:
  void set_parent(AllpowersBLE *parent) { this->parent_ = parent; }
  void setup() override;
  void store_and_publish(const std::string &name, uint64_t station_address, const char *source);
  bool publish_stored(uint64_t station_address, std::string *stored_name = nullptr);

 protected:
  static constexpr uint32_t PREFERENCE_VERSION = 0x53544E01;
  static constexpr size_t STORED_NAME_CAPACITY = protocol::MAX_DEVICE_NAME_LENGTH;

  struct __attribute__((packed)) StationNamePreference {
    uint64_t station_address;
    uint8_t name_length;
    char name[STORED_NAME_CAPACITY];
  };

  static_assert(sizeof(StationNamePreference) == sizeof(uint64_t) + sizeof(uint8_t) + STORED_NAME_CAPACITY,
                "Station-name preference must not contain padding");

  AllpowersBLE *parent_{nullptr};
  ESPPreferenceObject preference_;
  uint64_t stored_station_address_{0};
  std::string stored_name_;
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
