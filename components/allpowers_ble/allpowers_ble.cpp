// SPDX-License-Identifier: MIT
//
// ESPHome BLE component for compatible ALLPOWERS power stations.
// BLE protocol mapping and command logic are derived from:
// https://github.com/madninjaskillz/allpowers-ble

#include "allpowers_ble.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <inttypes.h>
#include <limits>
#include <string>
#include <vector>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::allpowers_ble {

static const char *const TAG = "allpowers_ble";

void AllpowersBLE::setup() {
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(false);
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(false);
  this->publish_controls_available_(false);
  this->publish_settings_available_(false);
  this->set_protocol_error_(false);
}

void AllpowersBLE::dump_config() {
  ESP_LOGCONFIG(TAG, "ALLPOWERS BLE power station:");
  char uuid_buffer[espbt::UUID_STR_LEN];
  this->service_uuid_.to_str(uuid_buffer);
  ESP_LOGCONFIG(TAG, "  Service UUID: %s", uuid_buffer);
  ESP_LOGCONFIG(TAG, "  Notify characteristic: 0x%04X", NOTIFY_UUID);
  ESP_LOGCONFIG(TAG, "  Write characteristic: 0x%04X", WRITE_UUID);
  ESP_LOGCONFIG(TAG, "  Stale timeout: %" PRIu32 " ms", this->stale_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Status keepalive interval: %" PRIu32 " ms", this->keepalive_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Connection watchdog timeout: %" PRIu32 " ms", this->watchdog_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Settings keepalive: %s", YESNO(this->settings_keepalive_enabled_));
  ESP_LOGCONFIG(TAG, "  Settings keepalive interval: %" PRIu32 " ms", this->settings_keepalive_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Experimental device-name command: %s", YESNO(this->experimental_device_name_enabled_));
}

void AllpowersBLE::set_settings_keepalive_enabled(bool enabled) {
  if (this->settings_keepalive_enabled_ == enabled)
    return;
  this->settings_keepalive_enabled_ = enabled;
  if (!enabled)
    this->initial_settings_keepalive_pending_ = false;
  this->last_settings_keepalive_ms_ = millis();
}

void AllpowersBLE::set_settings_keepalive_interval(uint32_t interval_ms) {
  if (this->settings_keepalive_interval_ms_ == interval_ms)
    return;
  this->settings_keepalive_interval_ms_ = interval_ms;
  this->last_settings_keepalive_ms_ = millis();
}

void AllpowersBLE::loop() {
  const uint32_t now = millis();

  // The GATT link can remain open when notifications stop. Telemetry and the
  // settings snapshot expire independently because their commands have
  // different payloads and safety requirements.
  if (this->stale_timeout_ms_ != 0 && this->data_fresh_ && this->last_valid_packet_ms_ != 0 &&
      now - this->last_valid_packet_ms_ > this->stale_timeout_ms_) {
    this->data_fresh_ = false;
    this->have_status_ = false;
    this->last_valid_packet_ms_ = 0;
    if (this->data_valid_binary_sensor_ != nullptr)
      this->data_valid_binary_sensor_->publish_state(false);
    this->publish_controls_available_(false);
    this->invalidate_data_entities_();
    ESP_LOGW(TAG, "No valid ALLPOWERS status packet received within the configured timeout");
  }

  if (this->stale_timeout_ms_ != 0 && this->settings_fresh_ && this->last_settings_packet_ms_ != 0 &&
      now - this->last_settings_packet_ms_ > this->stale_timeout_ms_) {
    this->settings_fresh_ = false;
    this->have_settings_ = false;
    this->last_settings_packet_ms_ = 0;
    this->publish_settings_available_(false);
    this->invalidate_settings_entities_();
    ESP_LOGW(TAG, "No valid ALLPOWERS settings packet received within the configured timeout");
  }

  if (this->disconnect_requested_)
    return;

  if (this->forced_reconnect_pending_) {
    this->request_forced_reconnect_(this->forced_reconnect_reason_ != nullptr ? this->forced_reconnect_reason_
                                                                              : "BLE session failure");
    return;
  }

  if (this->node_state != espbt::ClientState::ESTABLISHED)
    return;

  if (!this->connection_health_active_)
    return;

  // A station may already be close to its inactivity cutoff before ESPHome
  // establishes GATT. Send the first opt-in settings keepalive as soon as this
  // connection has supplied a real command-0x03 snapshot instead of waiting a
  // full periodic interval. Defer the write to loop() rather than issuing it
  // from the ESP-IDF notification callback.
  if (this->initial_settings_keepalive_pending_ && this->settings_keepalive_enabled_ && this->have_settings_snapshot_) {
    this->initial_settings_keepalive_pending_ = false;
    if (!this->send_settings_frame_("initial settings keepalive")) {
      this->request_forced_reconnect_("initial settings keepalive could not be queued");
      return;
    }
    this->last_status_request_ms_ = now;
    if (this->device_name_query_active_)
      this->next_device_name_query_ms_ = now + DEVICE_NAME_QUERY_INITIAL_DELAY_MS;
    return;
  }

  // The initial status request and optional device-name queries both use FFF2.
  // Space them so writes are not submitted back-to-back during GATT setup.
  // Retry only within this connection and stop immediately after a valid 0x35
  // response; this covers a lost first response without polling indefinitely.
  if (this->device_name_query_active_ && static_cast<int32_t>(now - this->next_device_name_query_ms_) >= 0) {
    this->device_name_query_attempts_++;
    ESP_LOGD(TAG, "Requesting Bluetooth device name (attempt %u/%u)",
             static_cast<unsigned>(this->device_name_query_attempts_),
             static_cast<unsigned>(DEVICE_NAME_QUERY_MAX_ATTEMPTS));
    this->request_device_name_query_();
    if (this->device_name_query_attempts_ >= DEVICE_NAME_QUERY_MAX_ATTEMPTS) {
      this->device_name_query_active_ = false;
    } else {
      this->next_device_name_query_ms_ = now + DEVICE_NAME_QUERY_RETRY_INTERVAL_MS;
    }
  }

  // A valid packet of any supported or unsupported command proves that the
  // GATT link still transports protocol traffic. If none arrives, tear down
  // the apparently connected link so BLEClient auto-connect can rediscover it.
  if (now - this->last_protocol_packet_ms_ >= this->watchdog_timeout_ms_) {
    this->request_forced_reconnect_("no valid protocol packet within watchdog timeout");
    return;
  }

  // Some R600 firmware appears to close an otherwise healthy GATT session
  // after roughly ten minutes unless it receives a control/settings command.
  // The ordinary status request above does not reset that station-side timer.
  // When explicitly enabled, resend the last complete settings snapshot just
  // before the observed cutoff. The snapshot is learned from command 0x03 and
  // is never manufactured from defaults.
  if (this->settings_keepalive_enabled_ &&
      now - this->last_settings_keepalive_ms_ >= this->settings_keepalive_interval_ms_) {
    this->last_settings_keepalive_ms_ = now;
    if (!this->have_settings_snapshot_) {
      ESP_LOGW(TAG, "Skipping settings keepalive: no settings snapshot has been received in this connection");
    } else if (!this->send_settings_frame_("settings keepalive")) {
      this->request_forced_reconnect_("settings keepalive could not be queued");
      return;
    } else {
      // Avoid submitting the periodic status request back-to-back when both
      // intervals expire in the same loop iteration (9 minutes is exactly 27
      // times the default 20-second status cadence).
      this->last_status_request_ms_ = now;
      return;
    }
  }

  // This observed request asks the station to resume its periodic status
  // broadcast. Sending it on a fixed cadence is cheaper than reconnecting and
  // mirrors the connection-health behavior independently implemented here
  // from the documented allpowers-companion behavior.
  if (now - this->last_status_request_ms_ >= this->keepalive_interval_ms_) {
    this->last_status_request_ms_ = now;
    if (!this->send_status_request_())
      this->request_forced_reconnect_("periodic status request could not be queued");
  }
}

void AllpowersBLE::reset_connection_state_() {
  // Drop every handle and protocol-derived state together. A later reconnect
  // must rediscover GATT and receive fresh status/settings notifications
  // before the corresponding controls are exposed again.
  this->have_status_ = false;
  this->data_fresh_ = false;
  this->last_valid_packet_ms_ = 0;
  this->have_settings_ = false;
  this->settings_fresh_ = false;
  this->have_settings_snapshot_ = false;
  this->last_settings_packet_ms_ = 0;
  this->last_protocol_packet_ms_ = 0;
  this->last_status_request_ms_ = 0;
  this->last_settings_keepalive_ms_ = 0;
  this->connection_health_active_ = false;
  this->forced_reconnect_pending_ = false;
  this->forced_reconnect_reason_ = nullptr;
  this->disconnect_requested_ = false;
  this->device_name_query_active_ = false;
  this->initial_settings_keepalive_pending_ = false;
  this->device_name_query_attempts_ = 0;
  this->next_device_name_query_ms_ = 0;
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(false);
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(false);
  this->publish_controls_available_(false);
  this->publish_settings_available_(false);
  this->invalidate_data_entities_();
  this->invalidate_settings_entities_();
  if (this->device_name_text_ != nullptr)
    this->device_name_text_->clear_state();
}

void AllpowersBLE::on_transport_connected_() {
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(true);
}

void AllpowersBLE::on_transport_disconnected_() { this->reset_connection_state_(); }

void AllpowersBLE::on_transport_ready_() {
  this->start_connection_health_();
  if (this->experimental_device_name_enabled_ && this->device_name_text_ != nullptr) {
    this->device_name_query_active_ = true;
    this->device_name_query_attempts_ = 0;
    this->next_device_name_query_ms_ = millis() + DEVICE_NAME_QUERY_INITIAL_DELAY_MS;
  }
}

void AllpowersBLE::on_transport_notification_(const uint8_t *data, uint16_t length) {
  this->process_notification_(data, length);
}

void AllpowersBLE::on_transport_error_(const char *reason, bool reconnect) {
  // A failed discovery or BLE transaction invalidates every control snapshot
  // immediately. The actual disconnect is deferred to loop() because ESP-IDF
  // callbacks must not tear down their own GATT session.
  this->reset_connection_state_();
  this->set_protocol_error_(true);
  if (reconnect)
    this->schedule_forced_reconnect_(reason);
}

void AllpowersBLE::process_notification_(const uint8_t *data, uint16_t length) {
  ESP_LOGV(TAG, "Notification (%u bytes): %s", length, format_hex_pretty(data, length).c_str());

  if (this->packet_length_sensor_ != nullptr)
    this->packet_length_sensor_->publish_state(length);

  const protocol::ParseError validation = protocol::validate_frame(data, length);
  if (validation != protocol::ParseError::NONE) {
    ESP_LOGW(TAG, "Ignoring invalid notification: %s", protocol::parse_error_message(validation));
    this->set_protocol_error_(true);
    return;
  }

  // Track every structurally valid frame, not just telemetry. This prevents a
  // watchdog reconnect while another command family proves the link is alive.
  this->last_protocol_packet_ms_ = millis();

  switch (data[protocol::COMMAND_OFFSET]) {
    case protocol::STATUS_COMMAND: {
      protocol::StatusData status;
      const protocol::ParseError result = protocol::parse_status(data, length, &status);
      if (result != protocol::ParseError::NONE) {
        ESP_LOGW(TAG, "Ignoring invalid status notification: %s", protocol::parse_error_message(result));
        this->set_protocol_error_(true);
        return;
      }
      this->process_status_(status);
      break;
    }
    case protocol::SETTINGS_STATUS_COMMAND: {
      protocol::SettingsData settings;
      const protocol::ParseError result = protocol::parse_settings(data, length, &settings);
      if (result != protocol::ParseError::NONE) {
        ESP_LOGW(TAG, "Ignoring invalid settings notification: %s", protocol::parse_error_message(result));
        this->set_protocol_error_(true);
        return;
      }
      this->process_settings_(settings);
      break;
    }
    case protocol::DEVICE_NAME_COMMAND: {
      if (!this->experimental_device_name_enabled_ || this->device_name_text_ == nullptr)
        break;
      std::string name;
      const protocol::ParseError result = protocol::parse_device_name(data, length, &name);
      if (result != protocol::ParseError::NONE) {
        ESP_LOGW(TAG, "Ignoring invalid device-name response: %s", protocol::parse_error_message(result));
        std::string stored_name;
        this->update_station_name("", "invalid command 0x35 response", &stored_name);
        if (!stored_name.empty())
          this->device_name_text_->publish_state(stored_name);
        this->set_protocol_error_(true);
        return;
      }
      this->process_device_name_(name);
      break;
    }
    default:
      // Other valid command families exist in newer stations. They are not a
      // protocol fault and must not be interpreted as telemetry by offset.
      ESP_LOGV(TAG, "Ignoring unsupported notification command 0x%02X", data[protocol::COMMAND_OFFSET]);
      this->set_protocol_error_(false);
      break;
  }
}

void AllpowersBLE::process_device_name_(const std::string &name) {
  if (name.empty()) {
    std::string stored_name;
    this->update_station_name("", "empty command 0x35 response", &stored_name);
    if (!stored_name.empty())
      this->device_name_text_->publish_state(stored_name);
    this->set_protocol_error_(false);
    return;
  }

  std::string normalized_name;
  const bool received_valid_name = this->update_station_name(name, "command 0x35", &normalized_name);
  if (!normalized_name.empty())
    this->device_name_text_->publish_state(normalized_name);
  if (!received_valid_name) {
    this->set_protocol_error_(false);
    return;
  }

  this->device_name_query_active_ = false;
  this->set_protocol_error_(false);
}

bool AllpowersBLE::update_station_name(const std::string &name, const char *source, std::string *normalized_name) {
  std::string normalized;
  if (!protocol::normalize_station_name(name, &normalized)) {
    ESP_LOGD(TAG, "Ignoring invalid or unavailable station name from %s", source);
    if (this->station_name_text_sensor_ != nullptr)
      this->station_name_text_sensor_->publish_stored(this->get_station_address(), normalized_name);
    return false;
  }

  if (normalized_name != nullptr)
    *normalized_name = normalized;
  if (this->station_name_text_sensor_ != nullptr)
    this->station_name_text_sensor_->store_and_publish(normalized, this->get_station_address(), source);
  return true;
}

void AllpowersBLE::process_status_(const protocol::StatusData &status) {
  this->dc_on_ = status.dc_on;
  this->ac_on_ = status.ac_on;
  this->light_on_ = status.light_on;

  if (this->soc_sensor_ != nullptr)
    this->soc_sensor_->publish_state(status.soc);
  if (this->input_power_sensor_ != nullptr)
    this->input_power_sensor_->publish_state(status.input_power);
  if (this->output_power_sensor_ != nullptr)
    this->output_power_sensor_->publish_state(status.output_power);
  if (this->remaining_time_sensor_ != nullptr)
    this->remaining_time_sensor_->publish_state(status.remaining_minutes);
  if (this->status_byte_sensor_ != nullptr)
    this->status_byte_sensor_->publish_state(status.status);

  // Experimental field from unmerged upstream PR #2: status bit 2 selects
  // 50 Hz when clear and 60 Hz when set. It is exposed explicitly as experimental.
  if (this->ac_frequency_sensor_ != nullptr)
    this->ac_frequency_sensor_->publish_state(status.ac_frequency_hz);

  if (this->ac_output_binary_sensor_ != nullptr)
    this->ac_output_binary_sensor_->publish_state(this->ac_on_);
  if (this->dc_output_binary_sensor_ != nullptr)
    this->dc_output_binary_sensor_->publish_state(this->dc_on_);
  if (this->light_binary_sensor_ != nullptr)
    this->light_binary_sensor_->publish_state(this->light_on_);
  if (this->charging_binary_sensor_ != nullptr)
    this->charging_binary_sensor_->publish_state(status.input_power > 0);
  if (this->discharging_binary_sensor_ != nullptr)
    this->discharging_binary_sensor_->publish_state(status.output_power > 0);

  this->have_status_ = true;
  this->data_fresh_ = true;
  this->last_valid_packet_ms_ = millis();
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(true);
  this->publish_controls_available_(this->controls_available_());
  this->set_protocol_error_(false);
  this->publish_switch_states_();
}

void AllpowersBLE::process_settings_(const protocol::SettingsData &settings) {
  this->settings_flags_ = settings.flags;
  this->eco_time_ = settings.eco_time;
  this->have_settings_ = true;
  this->settings_fresh_ = true;
  this->have_settings_snapshot_ = true;
  this->last_settings_packet_ms_ = millis();

  this->publish_settings_states_();
  if (this->hardware_version_text_sensor_ != nullptr)
    this->hardware_version_text_sensor_->publish_state(settings.hardware_version);
  if (this->firmware_version_text_sensor_ != nullptr)
    this->firmware_version_text_sensor_->publish_state(settings.firmware_version);
  this->publish_settings_available_(this->settings_available_());
  this->set_protocol_error_(false);

  ESP_LOGD(TAG,
           "Settings: flags=0x%02X, ECO=%s, ECO timeout=%u, work mode=%u, car charger=%s, hardware=%s, "
           "firmware=%s",
           this->settings_flags_, settings.eco_enabled ? "ON" : "OFF", this->eco_time_,
           static_cast<unsigned>(settings.work_mode), settings.car_charger_enabled ? "ON" : "OFF",
           settings.hardware_version.c_str(), settings.firmware_version.c_str());
}

void AllpowersBLE::publish_switch_states_() {
  if (this->ac_switch_ != nullptr)
    this->ac_switch_->publish_state(this->ac_on_);
  if (this->dc_switch_ != nullptr)
    this->dc_switch_->publish_state(this->dc_on_);
  if (this->light_switch_ != nullptr)
    this->light_switch_->publish_state(this->light_on_);
}

void AllpowersBLE::publish_settings_states_() {
  const bool eco_enabled = (this->settings_flags_ & protocol::SETTINGS_ECO_MASK) != 0;
  if (this->eco_mode_binary_sensor_ != nullptr)
    this->eco_mode_binary_sensor_->publish_state(eco_enabled);
  if (this->eco_switch_ != nullptr)
    this->eco_switch_->publish_state(eco_enabled);
  if (this->car_charger_switch_ != nullptr)
    this->car_charger_switch_->publish_state((this->settings_flags_ & protocol::SETTINGS_CAR_CHARGER_MASK) != 0);
  if (this->eco_shutdown_time_select_ != nullptr)
    this->eco_shutdown_time_select_->publish_hours(this->eco_time_);
  if (this->work_mode_select_ != nullptr) {
    const uint8_t work_mode =
        (this->settings_flags_ & protocol::SETTINGS_WORK_MODE_MASK) >> protocol::SETTINGS_WORK_MODE_SHIFT;
    this->work_mode_select_->publish_mode(work_mode);
  }
}

bool AllpowersBLE::controls_available_() const {
  return this->transport_ready_() && this->have_status_ && this->data_fresh_;
}

bool AllpowersBLE::settings_available_() const {
  return this->transport_ready_() && this->have_settings_ && this->settings_fresh_;
}

bool AllpowersBLE::request_output(OutputType output, bool state) {
  // The protocol has no independent AC/DC/light command. Every write carries
  // the complete output bitmap, so changing one bit is safe only when the
  // latest confirmed bitmap is both available and fresh.
  if (!this->controls_available_()) {
    ESP_LOGW(TAG, "Ignoring output command: ALLPOWERS controls are unavailable until BLE data is valid");
    return false;
  }

  // Temporarily update the local bitmap to build the combined command. If the
  // GATT write cannot be queued, roll back to the last confirmed snapshot.
  const bool old_ac = this->ac_on_;
  const bool old_dc = this->dc_on_;
  const bool old_light = this->light_on_;

  switch (output) {
    case OutputType::AC:
      this->ac_on_ = state;
      break;
    case OutputType::DC:
      this->dc_on_ = state;
      break;
    case OutputType::LIGHT:
      this->light_on_ = state;
      break;
  }

  if (this->send_control_frame_()) {
    // This is an optimistic UI update only. ESP-IDF accepting the write does
    // not prove that the station applied it; the next notification remains
    // authoritative and will overwrite these values if necessary.
    this->publish_switch_states_();
    return true;
  }

  this->ac_on_ = old_ac;
  this->dc_on_ = old_dc;
  this->light_on_ = old_light;
  this->publish_switch_states_();
  return false;
}

bool AllpowersBLE::request_eco_mode(bool state) {
  // The settings command contains more than the ECO bit. Refuse the write
  // until command 0x03 has supplied a fresh complete snapshot, then modify
  // only bit 0 and preserve every other setting verbatim.
  if (!this->settings_available_()) {
    ESP_LOGW(TAG, "Ignoring ECO command: ALLPOWERS settings are unavailable until a settings frame is received");
    return false;
  }

  const bool current_state = (this->settings_flags_ & protocol::SETTINGS_ECO_MASK) != 0;
  if (current_state == state) {
    // Avoid a redundant write and the confirmation beep produced by the station.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_flags = this->settings_flags_;
  if (state) {
    this->settings_flags_ |= protocol::SETTINGS_ECO_MASK;
  } else {
    this->settings_flags_ &= static_cast<uint8_t>(~protocol::SETTINGS_ECO_MASK);
  }

  if (this->send_settings_frame_()) {
    // Optimistic state is useful for immediate UI feedback. The next command
    // 0x03 notification remains authoritative and can correct it.
    this->publish_settings_states_();
    return true;
  }

  this->settings_flags_ = old_flags;
  this->publish_settings_states_();
  return false;
}

bool AllpowersBLE::request_eco_shutdown_time(uint8_t hours) {
  if (std::find(protocol::ECO_SHUTDOWN_HOURS.begin(), protocol::ECO_SHUTDOWN_HOURS.end(), hours) ==
      protocol::ECO_SHUTDOWN_HOURS.end()) {
    ESP_LOGE(TAG, "Ignoring unsupported ECO shutdown time: %u hours", static_cast<unsigned>(hours));
    return false;
  }

  // Byte 8 shares the same command with every settings flag. Reuse the fresh
  // snapshot gate so changing the timeout cannot reset settings that are not
  // yet exposed by this component.
  if (!this->settings_available_()) {
    ESP_LOGW(TAG, "Ignoring ECO shutdown time command: ALLPOWERS settings are unavailable until a settings frame is "
                  "received");
    return false;
  }

  if (this->eco_time_ == hours) {
    // Avoid a redundant command because accepted settings writes produce an
    // audible confirmation on the R600.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_eco_time = this->eco_time_;
  this->eco_time_ = hours;

  if (this->send_settings_frame_()) {
    // The optimistic value is replaced by the next command-0x03 report, which
    // remains authoritative if the station rejects or normalizes the request.
    this->publish_settings_states_();
    return true;
  }

  this->eco_time_ = old_eco_time;
  this->publish_settings_states_();
  return false;
}

bool AllpowersBLE::request_work_mode(uint8_t mode) {
  if (std::find(protocol::WORK_MODES.begin(), protocol::WORK_MODES.end(), mode) == protocol::WORK_MODES.end()) {
    ESP_LOGE(TAG, "Ignoring unsupported work mode: %u", static_cast<unsigned>(mode));
    return false;
  }

  // Work mode occupies bits 1-2 of the shared settings bitmap. Start from the
  // latest complete snapshot so ECO, car/DC, AC mode, self-use and reserved
  // bits are not altered as a side effect.
  if (!this->settings_available_()) {
    ESP_LOGW(TAG, "Ignoring work mode command: ALLPOWERS settings are unavailable until a settings frame is received");
    return false;
  }

  const uint8_t current_mode =
      (this->settings_flags_ & protocol::SETTINGS_WORK_MODE_MASK) >> protocol::SETTINGS_WORK_MODE_SHIFT;
  if (current_mode == mode) {
    // Avoid a redundant write and the audible acknowledgement produced by the
    // R600 for accepted settings commands.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_flags = this->settings_flags_;
  this->settings_flags_ =
      static_cast<uint8_t>((this->settings_flags_ & static_cast<uint8_t>(~protocol::SETTINGS_WORK_MODE_MASK)) |
                           static_cast<uint8_t>(mode << protocol::SETTINGS_WORK_MODE_SHIFT));

  if (this->send_settings_frame_()) {
    // The next command-0x03 notification remains authoritative. This component
    // intentionally changes only the work-mode bits; it does not reproduce the
    // official app's separate buzzer command when Mute Mode is selected.
    this->publish_settings_states_();
    return true;
  }

  this->settings_flags_ = old_flags;
  this->publish_settings_states_();
  return false;
}

bool AllpowersBLE::request_car_charger(bool state) {
  // The R600 reports the automotive 12 V socket in bit 4 of the shared
  // settings bitmap. Preserve the other settings and the ECO timeout exactly
  // as last reported by command 0x03.
  if (!this->settings_available_()) {
    ESP_LOGW(TAG,
             "Ignoring car charger command: ALLPOWERS settings are unavailable until a settings frame is received");
    return false;
  }

  const bool current_state = (this->settings_flags_ & protocol::SETTINGS_CAR_CHARGER_MASK) != 0;
  if (current_state == state) {
    // Accepted settings writes produce an audible acknowledgement, so avoid a
    // command that cannot change the confirmed state.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_flags = this->settings_flags_;
  if (state) {
    this->settings_flags_ |= protocol::SETTINGS_CAR_CHARGER_MASK;
  } else {
    this->settings_flags_ &= static_cast<uint8_t>(~protocol::SETTINGS_CAR_CHARGER_MASK);
  }

  if (this->send_settings_frame_()) {
    // ESP-IDF accepting the write is not confirmation from the station. The
    // next command-0x03 notification remains authoritative.
    this->publish_settings_states_();
    return true;
  }

  this->settings_flags_ = old_flags;
  this->publish_settings_states_();
  return false;
}

bool AllpowersBLE::request_device_name(const std::string &name) {
  if (!this->experimental_device_name_enabled_) {
    ESP_LOGW(TAG, "Ignoring device-name command: enable_experimental_device_name is false");
    return false;
  }
  if (!this->transport_ready_()) {
    ESP_LOGW(TAG, "Ignoring device-name command: BLE client is not ready");
    return false;
  }
  if (name.empty() || name.size() > protocol::MAX_DEVICE_NAME_LENGTH ||
      !protocol::valid_utf8(reinterpret_cast<const uint8_t *>(name.data()), name.size())) {
    ESP_LOGW(TAG, "Ignoring device-name command: name must be valid UTF-8 and 1-%u bytes",
             static_cast<unsigned>(protocol::MAX_DEVICE_NAME_LENGTH));
    return false;
  }
  return this->send_device_name_frame_(name);
}

bool AllpowersBLE::request_device_name_query_() { return this->send_device_name_frame_(""); }

bool AllpowersBLE::send_status_request_() {
  // Observed connection/status-subscription request used by
  // allpowers-companion. It is a fixed station-facing command rather than the
  // XOR-framed notification envelope, so reproduce the bytes exactly.
  protocol::StatusRequestFrame frame = protocol::status_request_frame();
  return this->write_transport_frame_(frame.data(), frame.size(), "status request");
}

void AllpowersBLE::start_connection_health_() {
  const uint32_t now = millis();
  this->connection_health_active_ = true;
  this->forced_reconnect_pending_ = false;
  this->forced_reconnect_reason_ = nullptr;
  this->disconnect_requested_ = false;
  this->last_protocol_packet_ms_ = now;
  this->last_status_request_ms_ = now;
  this->last_settings_keepalive_ms_ = now;
  this->initial_settings_keepalive_pending_ = this->settings_keepalive_enabled_;

  // Request the first status broadcast immediately after notification setup.
  // Defer forced disconnect to loop() if the ESP-IDF write cannot be queued,
  // avoiding GATT teardown from inside the registration callback.
  if (!this->send_status_request_())
    this->schedule_forced_reconnect_("initial status request could not be queued");
}

bool AllpowersBLE::send_settings_keepalive_now() {
  if (!this->transport_ready_() || this->disconnect_requested_) {
    ESP_LOGW(TAG, "Cannot send settings keepalive now: BLE client is not ready");
    return false;
  }
  if (!this->have_settings_snapshot_) {
    ESP_LOGW(TAG, "Cannot send settings keepalive now: no settings snapshot has been received in this connection");
    return false;
  }

  this->initial_settings_keepalive_pending_ = false;
  if (!this->send_settings_frame_("manual settings keepalive")) {
    this->schedule_forced_reconnect_("manual settings keepalive could not be queued");
    return false;
  }

  // send_settings_frame_ resets this timer when automatic keepalive is on.
  // Assign it unconditionally so a later enable starts from this manual send.
  const uint32_t now = millis();
  this->last_settings_keepalive_ms_ = now;
  this->last_status_request_ms_ = now;
  return true;
}

void AllpowersBLE::schedule_forced_reconnect_(const char *reason) {
  if (this->disconnect_requested_)
    return;
  this->forced_reconnect_pending_ = true;
  this->forced_reconnect_reason_ = reason;
}

void AllpowersBLE::request_forced_reconnect_(const char *reason) {
  if (this->disconnect_requested_)
    return;
  this->disconnect_requested_ = true;
  this->forced_reconnect_pending_ = false;
  this->forced_reconnect_reason_ = nullptr;
  ESP_LOGW(TAG, "ALLPOWERS BLE connection appears stale (%s); forcing reconnect", reason);
  this->disconnect_transport_();
}

bool AllpowersBLE::send_device_name_frame_(const std::string &name) {
  std::vector<uint8_t> frame = protocol::make_device_name_frame(name);
  return !frame.empty() && this->write_transport_frame_(frame.data(), frame.size(),
                                                        name.empty() ? "device-name query" : "device-name update");
}

bool AllpowersBLE::send_control_frame_() {
  // Exact control frame and check-byte calculation from allpowers-ble. The
  // final byte is reproduced from upstream behavior; it is not treated as a
  // generally understood checksum algorithm.
  protocol::ControlFrame frame = protocol::make_control_frame({this->dc_on_, this->ac_on_, this->light_on_});
  const bool queued = this->write_transport_frame_(frame.data(), frame.size(), "output control");
  if (queued && this->settings_keepalive_enabled_)
    this->last_settings_keepalive_ms_ = millis();
  return queued;
}

bool AllpowersBLE::send_settings_frame_(const char *description) {
  // Command 0x02 always carries the settings bitmap and ECO timeout together.
  // Callers update one field in the shared snapshot before using this builder.
  protocol::SettingsFrame frame = protocol::make_settings_frame(this->settings_flags_, this->eco_time_);
  const bool queued = this->write_transport_frame_(frame.data(), frame.size(), description);
  if (queued && this->settings_keepalive_enabled_)
    this->last_settings_keepalive_ms_ = millis();
  return queued;
}

void AllpowersBLE::invalidate_data_entities_() {
  // Numeric ESPHome sensors use NaN to publish an unknown state. Binary
  // sensors support explicit invalidation, so both entity types correctly
  // become unknown in Home Assistant instead of retaining stale telemetry.
  const float unavailable = std::numeric_limits<float>::quiet_NaN();

  if (this->soc_sensor_ != nullptr)
    this->soc_sensor_->publish_state(unavailable);
  if (this->input_power_sensor_ != nullptr)
    this->input_power_sensor_->publish_state(unavailable);
  if (this->output_power_sensor_ != nullptr)
    this->output_power_sensor_->publish_state(unavailable);
  if (this->remaining_time_sensor_ != nullptr)
    this->remaining_time_sensor_->publish_state(unavailable);
  if (this->ac_frequency_sensor_ != nullptr)
    this->ac_frequency_sensor_->publish_state(unavailable);
  if (this->status_byte_sensor_ != nullptr)
    this->status_byte_sensor_->publish_state(unavailable);
  if (this->packet_length_sensor_ != nullptr)
    this->packet_length_sensor_->publish_state(unavailable);

  if (this->ac_output_binary_sensor_ != nullptr)
    this->ac_output_binary_sensor_->invalidate_state();
  if (this->dc_output_binary_sensor_ != nullptr)
    this->dc_output_binary_sensor_->invalidate_state();
  if (this->light_binary_sensor_ != nullptr)
    this->light_binary_sensor_->invalidate_state();
  if (this->charging_binary_sensor_ != nullptr)
    this->charging_binary_sensor_->invalidate_state();
  if (this->discharging_binary_sensor_ != nullptr)
    this->discharging_binary_sensor_->invalidate_state();
}

void AllpowersBLE::invalidate_settings_entities_() {
  // Settings Available is the authoritative availability signal for native
  // controls. Clear confirmed state as well so a reconnect cannot present a
  // previous timeout as the current device setting.
  if (this->eco_mode_binary_sensor_ != nullptr)
    this->eco_mode_binary_sensor_->invalidate_state();
  if (this->eco_shutdown_time_select_ != nullptr)
    this->eco_shutdown_time_select_->clear_state();
  if (this->work_mode_select_ != nullptr)
    this->work_mode_select_->clear_state();
}

void AllpowersBLE::publish_controls_available_(bool state) {
  if (this->controls_available_binary_sensor_ != nullptr)
    this->controls_available_binary_sensor_->publish_state(state);
}

void AllpowersBLE::publish_settings_available_(bool state) {
  if (this->settings_available_binary_sensor_ != nullptr)
    this->settings_available_binary_sensor_->publish_state(state);
}

void AllpowersBLE::set_protocol_error_(bool state) {
  if (this->protocol_error_binary_sensor_ != nullptr)
    this->protocol_error_binary_sensor_->publish_state(state);
}

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
