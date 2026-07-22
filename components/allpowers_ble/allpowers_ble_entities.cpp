// SPDX-License-Identifier: MIT

#include "allpowers_ble.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::allpowers_ble {

static const char *const TAG = "allpowers_ble.entities";
static constexpr float MIN_SETTINGS_KEEPALIVE_MINUTES = 1.0f;
static constexpr float MAX_SETTINGS_KEEPALIVE_MINUTES = 9.0f;
static constexpr float MILLISECONDS_PER_MINUTE = 60000.0f;

void AllpowersBLESwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Allpowers switch has no parent component");
    return;
  }
  this->parent_->request_output(this->output_type_, state);
}

void AllpowersBLEEcoSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS ECO switch has no parent component");
    return;
  }
  this->parent_->request_eco_mode(state);
}

void AllpowersBLECarChargerSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS car charger switch has no parent component");
    return;
  }
  this->parent_->request_car_charger(state);
}

void AllpowersBLESettingsKeepaliveSwitch::setup() {
  const optional<bool> restored_state = this->get_initial_state();
  const bool enabled = restored_state.value_or(this->parent_->get_settings_keepalive_enabled());
  this->parent_->set_settings_keepalive_enabled(enabled);
  this->publish_state(enabled);
}

void AllpowersBLESettingsKeepaliveSwitch::write_state(bool state) {
  this->parent_->set_settings_keepalive_enabled(state);
  this->publish_state(state);
}

void AllpowersBLESettingsKeepaliveIntervalNumber::setup() {
  this->preference_ = this->make_entity_preference<float>();

  float minutes;
  if (!this->preference_.load(&minutes) || !std::isfinite(minutes) || minutes < MIN_SETTINGS_KEEPALIVE_MINUTES ||
      minutes > MAX_SETTINGS_KEEPALIVE_MINUTES) {
    minutes = static_cast<float>(this->parent_->get_settings_keepalive_interval()) / MILLISECONDS_PER_MINUTE;
  }

  this->parent_->set_settings_keepalive_interval(static_cast<uint32_t>(minutes * MILLISECONDS_PER_MINUTE));
  this->publish_state(minutes);
}

void AllpowersBLESettingsKeepaliveIntervalNumber::control(float value) {
  if (this->has_state() && value == this->state)
    return;
  this->parent_->set_settings_keepalive_interval(static_cast<uint32_t>(value * MILLISECONDS_PER_MINUTE));
  this->preference_.save(&value);
  this->publish_state(value);
}

void AllpowersBLESettingsKeepaliveButton::press_action() { this->parent_->send_settings_keepalive_now(); }

void AllpowersBLEStationNameTextSensor::setup() {
  this->preference_ = this->make_entity_preference<StationNamePreference>(PREFERENCE_VERSION);
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "Station-name text sensor has no parent component");
    return;
  }

  StationNamePreference stored{};
  if (!this->preference_.load(&stored)) {
    this->publish_state("");
    return;
  }

  const uint64_t current_address = this->parent_->get_station_address();
  if (stored.station_address != current_address) {
    if (stored.name_length != 0) {
      StationNamePreference cleared{};
      cleared.station_address = current_address;
      if (this->preference_.save(&cleared)) {
        ESP_LOGI(TAG, "Cleared stored station name because the configured BLE MAC address changed");
      } else {
        ESP_LOGW(TAG, "Failed to clear stored station name after the configured BLE MAC address changed");
      }
    }
    this->stored_station_address_ = current_address;
    this->stored_name_.clear();
    this->publish_state("");
    return;
  }

  this->stored_station_address_ = stored.station_address;
  if (stored.name_length == 0) {
    this->publish_state("");
    return;
  }
  if (stored.name_length > STORED_NAME_CAPACITY) {
    ESP_LOGW(TAG, "Ignoring invalid stored station-name length");
    this->stored_name_.clear();
    this->publish_state("");
    return;
  }

  std::string restored_name(stored.name, stored.name_length);
  std::string normalized_name;
  if (!protocol::normalize_station_name(restored_name, &normalized_name)) {
    ESP_LOGW(TAG, "Ignoring invalid station name stored in flash");
    this->stored_name_.clear();
    this->publish_state("");
    return;
  }

  this->stored_name_ = normalized_name;
  this->publish_state(normalized_name);
}

void AllpowersBLEStationNameTextSensor::store_and_publish(const std::string &name, uint64_t station_address,
                                                          const char *source) {
  if (station_address == 0) {
    ESP_LOGW(TAG, "Cannot persist station name from %s without a configured BLE MAC address", source);
    this->publish_state(name);
    return;
  }

  if (this->stored_station_address_ == station_address && this->stored_name_ == name) {
    if (!this->has_state() || this->state != name)
      this->publish_state(name);
    return;
  }

  StationNamePreference stored{};
  stored.station_address = station_address;
  stored.name_length = static_cast<uint8_t>(name.size());
  std::copy(name.begin(), name.end(), stored.name);
  if (!this->preference_.save(&stored)) {
    ESP_LOGW(TAG, "Failed to persist station name received from %s", source);
  } else {
    this->stored_station_address_ = station_address;
    this->stored_name_ = name;
  }

  this->publish_state(name);
}

bool AllpowersBLEStationNameTextSensor::publish_stored(uint64_t station_address, std::string *stored_name) {
  if (stored_name != nullptr)
    stored_name->clear();
  if (station_address == 0 || this->stored_station_address_ != station_address || this->stored_name_.empty())
    return false;

  if (stored_name != nullptr)
    *stored_name = this->stored_name_;
  if (!this->has_state() || this->state != this->stored_name_)
    this->publish_state(this->stored_name_);
  return true;
}

void AllpowersBLEEcoShutdownTimeSelect::publish_hours(uint8_t hours) {
  const auto option = std::find(protocol::ECO_SHUTDOWN_HOURS.begin(), protocol::ECO_SHUTDOWN_HOURS.end(), hours);
  if (option == protocol::ECO_SHUTDOWN_HOURS.end()) {
    this->clear_state();
    return;
  }
  this->publish_state(static_cast<size_t>(option - protocol::ECO_SHUTDOWN_HOURS.begin()));
}

void AllpowersBLEEcoShutdownTimeSelect::control(size_t index) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS ECO shutdown time select has no parent component");
    return;
  }
  if (index >= protocol::ECO_SHUTDOWN_HOURS.size()) {
    ESP_LOGE(TAG, "Invalid ECO shutdown time option index: %u", static_cast<unsigned>(index));
    return;
  }
  this->parent_->request_eco_shutdown_time(protocol::ECO_SHUTDOWN_HOURS[index]);
}

void AllpowersBLEWorkModeSelect::publish_mode(uint8_t mode) {
  const auto option = std::find(protocol::WORK_MODES.begin(), protocol::WORK_MODES.end(), mode);
  if (option == protocol::WORK_MODES.end()) {
    this->clear_state();
    return;
  }
  this->publish_state(static_cast<size_t>(option - protocol::WORK_MODES.begin()));
}

void AllpowersBLEWorkModeSelect::control(size_t index) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS work mode select has no parent component");
    return;
  }
  if (index >= protocol::WORK_MODES.size()) {
    ESP_LOGE(TAG, "Invalid work mode option index: %u", static_cast<unsigned>(index));
    return;
  }
  this->parent_->request_work_mode(protocol::WORK_MODES[index]);
}

void AllpowersBLEDeviceNameText::control(const std::string &value) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS device-name text has no parent component");
    return;
  }
  this->parent_->request_device_name(value);
}

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
