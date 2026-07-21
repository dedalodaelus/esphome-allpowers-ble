// SPDX-License-Identifier: MIT
//
// ESPHome BLE component for compatible ALLPOWERS power stations.
// BLE protocol mapping and command logic are derived from:
// https://github.com/madninjaskillz/allpowers-ble

#include "allpowers_ble.h"

#include <algorithm>
#include <array>
#include <inttypes.h>
#include <limits>

#include <esp_err.h>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::allpowers_ble {

static const char *const TAG = "allpowers_ble";
static constexpr std::array<uint8_t, 4> ECO_SHUTDOWN_HOURS{{1, 2, 4, 6}};
static constexpr std::array<uint8_t, 3> WORK_MODES{{0, 1, 2}};

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
}

void AllpowersBLE::loop() {
  if (this->stale_timeout_ms_ == 0)
    return;

  const uint32_t now = millis();

  // The GATT link can remain open when notifications stop. Telemetry and the
  // settings snapshot expire independently because their commands have
  // different payloads and safety requirements.
  if (this->data_fresh_ && this->last_valid_packet_ms_ != 0 &&
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

  if (this->settings_fresh_ && this->last_settings_packet_ms_ != 0 &&
      now - this->last_settings_packet_ms_ > this->stale_timeout_ms_) {
    this->settings_fresh_ = false;
    this->have_settings_ = false;
    this->last_settings_packet_ms_ = 0;
    this->publish_settings_available_(false);
    this->invalidate_settings_entities_();
    ESP_LOGW(TAG, "No valid ALLPOWERS settings packet received within the configured timeout");
  }
}

void AllpowersBLE::reset_connection_state_() {
  // Drop every handle and protocol-derived state together. A later reconnect
  // must rediscover GATT and receive fresh status/settings notifications
  // before the corresponding controls are exposed again.
  this->notify_handle_ = 0;
  this->write_handle_ = 0;
  this->write_properties_ = static_cast<esp_gatt_char_prop_t>(0);
  this->have_status_ = false;
  this->data_fresh_ = false;
  this->last_valid_packet_ms_ = 0;
  this->have_settings_ = false;
  this->settings_fresh_ = false;
  this->last_settings_packet_ms_ = 0;
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(false);
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(false);
  this->publish_controls_available_(false);
  this->publish_settings_available_(false);
  this->invalidate_data_entities_();
  this->invalidate_settings_entities_();
}

void AllpowersBLE::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  (void) gattc_if;

  // Expected setup sequence:
  //   OPEN -> service discovery -> locate FFF1/FFF2 -> register FFF1 notify.
  // The component becomes operational only after a valid status frame, even
  // though BLEClientNode reaches ESTABLISHED when GATT setup has completed.
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK || param->open.status == ESP_GATT_ALREADY_OPEN) {
        if (this->connected_binary_sensor_ != nullptr)
          this->connected_binary_sensor_->publish_state(true);
      }
      break;

    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->reset_connection_state_();
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *notify_characteristic =
          this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(NOTIFY_UUID));
      auto *write_characteristic =
          this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(WRITE_UUID));

      if (notify_characteristic == nullptr || write_characteristic == nullptr) {
        ESP_LOGE(TAG, "Required ALLPOWERS GATT characteristics FFF1/FFF2 were not found under the configured service");
        this->set_protocol_error_(true);
        this->publish_controls_available_(false);
        this->publish_settings_available_(false);
        // Mark this BLEClientNode's discovery phase as complete so ESPHome is
        // not left waiting indefinitely. Diagnostics still report failure and
        // controls remain unavailable because no valid handles/status exist.
        this->node_state = espbt::ClientState::ESTABLISHED;
        break;
      }

      this->notify_handle_ = notify_characteristic->handle;
      this->write_handle_ = write_characteristic->handle;
      this->write_properties_ = write_characteristic->properties;

      if ((notify_characteristic->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) == 0 &&
          (notify_characteristic->properties & ESP_GATT_CHAR_PROP_BIT_INDICATE) == 0) {
        ESP_LOGE(TAG, "Characteristic FFF1 has neither notify nor indicate property");
        this->set_protocol_error_(true);
        this->publish_controls_available_(false);
        this->publish_settings_available_(false);
        // See the discovery-failure note above: ESTABLISHED here means this
        // node finished setup, not that the ALLPOWERS protocol is usable.
        this->node_state = espbt::ClientState::ESTABLISHED;
        break;
      }

      const esp_err_t status = esp_ble_gattc_register_for_notify(
          this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), this->notify_handle_);
      if (status != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gattc_register_for_notify failed: %s", esp_err_to_name(status));
        this->set_protocol_error_(true);
        this->publish_controls_available_(false);
        this->publish_settings_available_(false);
        // Finish node setup without exposing controls. A future reconnect can
        // retry discovery and notification registration from a clean state.
        this->node_state = espbt::ClientState::ESTABLISHED;
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "Notification registration failed, status=%d", param->reg_for_notify.status);
        this->set_protocol_error_(true);
        this->publish_controls_available_(false);
        this->publish_settings_available_(false);
      } else {
        ESP_LOGI(TAG, "Subscribed to ALLPOWERS notifications");
      }
      // Notification registration completes this BLEClientNode's setup. The
      // first valid status notification is still required before writes.
      this->node_state = espbt::ClientState::ESTABLISHED;
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == this->notify_handle_)
        this->process_notification_(param->notify.value, param->notify.value_len);
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->write_handle_ && param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "ALLPOWERS write failed, status=%d", param->write.status);
        this->set_protocol_error_(true);
      }
      break;

    default:
      break;
  }
}

bool AllpowersBLE::validate_notification_(const uint8_t *data, uint16_t length) const {
  if (length < MIN_FRAME_LENGTH) {
    ESP_LOGW(TAG, "Ignoring short notification: expected at least %u bytes, received %u",
             static_cast<unsigned>(MIN_FRAME_LENGTH), length);
    return false;
  }

  if (data[0] != 0xA5 || data[1] != 0x65) {
    ESP_LOGW(TAG, "Ignoring notification with unknown header: %02X %02X", data[0], data[1]);
    return false;
  }

  // The official application validates notifications as an eight-byte frame
  // envelope plus the payload length declared in byte 5. The final byte is the
  // XOR of every preceding byte, so XOR across the complete frame must be zero.
  const size_t expected_length = MIN_FRAME_LENGTH + data[PAYLOAD_LENGTH_OFFSET];
  if (expected_length != length) {
    ESP_LOGW(TAG, "Ignoring notification with inconsistent length: declared %u bytes, received %u",
             static_cast<unsigned>(expected_length), length);
    return false;
  }

  uint8_t checksum = 0;
  for (size_t index = 0; index < length; index++)
    checksum ^= data[index];
  if (checksum != 0) {
    ESP_LOGW(TAG, "Ignoring notification with invalid XOR checksum");
    return false;
  }

  return true;
}

void AllpowersBLE::process_notification_(const uint8_t *data, uint16_t length) {
  ESP_LOGV(TAG, "Notification (%u bytes): %s", length, format_hex_pretty(data, length).c_str());

  if (this->packet_length_sensor_ != nullptr)
    this->packet_length_sensor_->publish_state(length);

  if (!this->validate_notification_(data, length)) {
    this->set_protocol_error_(true);
    return;
  }

  switch (data[COMMAND_OFFSET]) {
    case STATUS_COMMAND:
      this->process_status_notification_(data, length);
      break;
    case SETTINGS_STATUS_COMMAND:
      this->process_settings_notification_(data, length);
      break;
    default:
      // Other valid command families exist in newer stations. They are not a
      // protocol fault and must not be interpreted as telemetry by offset.
      ESP_LOGV(TAG, "Ignoring unsupported notification command 0x%02X", data[COMMAND_OFFSET]);
      this->set_protocol_error_(false);
      break;
  }
}

void AllpowersBLE::process_status_notification_(const uint8_t *data, uint16_t length) {
  if (length < MIN_STATUS_PACKET_LENGTH) {
    ESP_LOGW(TAG, "Ignoring short status notification: expected at least %u bytes, received %u",
             static_cast<unsigned>(MIN_STATUS_PACKET_LENGTH), length);
    this->set_protocol_error_(true);
    return;
  }

  // Verified status fields from the upstream parser:
  //   byte 7      output/status bitmap
  //   byte 8      battery state of charge, percent
  //   bytes 9-10  total input power, big-endian watts
  //   bytes 11-12 total output power, big-endian watts
  //   bytes 13-14 estimated remaining time, big-endian minutes
  const uint8_t status = data[STATUS_OFFSET];
  const uint8_t soc = data[SOC_OFFSET];
  const uint16_t input_power = (static_cast<uint16_t>(data[INPUT_POWER_OFFSET]) << 8) | data[INPUT_POWER_OFFSET + 1];
  const uint16_t output_power = (static_cast<uint16_t>(data[OUTPUT_POWER_OFFSET]) << 8) | data[OUTPUT_POWER_OFFSET + 1];
  const uint16_t remaining_minutes =
      (static_cast<uint16_t>(data[REMAINING_TIME_OFFSET]) << 8) | data[REMAINING_TIME_OFFSET + 1];

  this->dc_on_ = (status & STATUS_DC_MASK) != 0;
  this->ac_on_ = (status & STATUS_AC_MASK) != 0;
  this->light_on_ = (status & STATUS_LIGHT_MASK) != 0;

  if (this->soc_sensor_ != nullptr)
    this->soc_sensor_->publish_state(soc);
  if (this->input_power_sensor_ != nullptr)
    this->input_power_sensor_->publish_state(input_power);
  if (this->output_power_sensor_ != nullptr)
    this->output_power_sensor_->publish_state(output_power);
  if (this->remaining_time_sensor_ != nullptr)
    this->remaining_time_sensor_->publish_state(remaining_minutes);
  if (this->status_byte_sensor_ != nullptr)
    this->status_byte_sensor_->publish_state(status);

  // Experimental field from unmerged upstream PR #2: status bit 2 selects
  // 50 Hz when clear and 60 Hz when set. It is exposed explicitly as experimental.
  if (this->ac_frequency_sensor_ != nullptr)
    this->ac_frequency_sensor_->publish_state((status & STATUS_FREQUENCY_MASK) != 0 ? 60.0f : 50.0f);

  if (this->ac_output_binary_sensor_ != nullptr)
    this->ac_output_binary_sensor_->publish_state(this->ac_on_);
  if (this->dc_output_binary_sensor_ != nullptr)
    this->dc_output_binary_sensor_->publish_state(this->dc_on_);
  if (this->light_binary_sensor_ != nullptr)
    this->light_binary_sensor_->publish_state(this->light_on_);
  if (this->charging_binary_sensor_ != nullptr)
    this->charging_binary_sensor_->publish_state(input_power > 0);
  if (this->discharging_binary_sensor_ != nullptr)
    this->discharging_binary_sensor_->publish_state(output_power > 0);

  this->have_status_ = true;
  this->data_fresh_ = true;
  this->last_valid_packet_ms_ = millis();
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(true);
  this->publish_controls_available_(this->controls_available_());
  this->set_protocol_error_(false);
  this->publish_switch_states_();
}

void AllpowersBLE::process_settings_notification_(const uint8_t *data, uint16_t length) {
  if (length < MIN_SETTINGS_PACKET_LENGTH) {
    ESP_LOGW(TAG, "Ignoring short settings notification: expected at least %u bytes, received %u",
             static_cast<unsigned>(MIN_SETTINGS_PACKET_LENGTH), length);
    this->set_protocol_error_(true);
    return;
  }

  // Command 0x03 reports the complete settings bitmap and ECO timeout. Store
  // both raw fields so each supported setting can be changed without resetting
  // AC mode, self-use or reserved bits.
  this->settings_flags_ = data[SETTINGS_FLAGS_OFFSET];
  this->eco_time_ = data[SETTINGS_ECO_TIME_OFFSET];
  this->have_settings_ = true;
  this->settings_fresh_ = true;
  this->last_settings_packet_ms_ = millis();

  this->publish_settings_states_();
  this->publish_settings_available_(this->settings_available_());
  this->set_protocol_error_(false);

  ESP_LOGD(TAG, "Settings: flags=0x%02X, ECO=%s, ECO timeout=%u, work mode=%u, car charger=%s", this->settings_flags_,
           (this->settings_flags_ & SETTINGS_ECO_MASK) != 0 ? "ON" : "OFF", this->eco_time_,
           static_cast<unsigned>((this->settings_flags_ & SETTINGS_WORK_MODE_MASK) >> SETTINGS_WORK_MODE_SHIFT),
           (this->settings_flags_ & SETTINGS_CAR_CHARGER_MASK) != 0 ? "ON" : "OFF");
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
  const bool eco_enabled = (this->settings_flags_ & SETTINGS_ECO_MASK) != 0;
  if (this->eco_mode_binary_sensor_ != nullptr)
    this->eco_mode_binary_sensor_->publish_state(eco_enabled);
  if (this->eco_switch_ != nullptr)
    this->eco_switch_->publish_state(eco_enabled);
  if (this->car_charger_switch_ != nullptr)
    this->car_charger_switch_->publish_state((this->settings_flags_ & SETTINGS_CAR_CHARGER_MASK) != 0);
  if (this->eco_shutdown_time_select_ != nullptr)
    this->eco_shutdown_time_select_->publish_hours(this->eco_time_);
  if (this->work_mode_select_ != nullptr) {
    const uint8_t work_mode = (this->settings_flags_ & SETTINGS_WORK_MODE_MASK) >> SETTINGS_WORK_MODE_SHIFT;
    this->work_mode_select_->publish_mode(work_mode);
  }
}

bool AllpowersBLE::controls_available_() const {
  return this->node_state == espbt::ClientState::ESTABLISHED && this->write_handle_ != 0 && this->have_status_ &&
         this->data_fresh_;
}

bool AllpowersBLE::settings_available_() const {
  return this->node_state == espbt::ClientState::ESTABLISHED && this->write_handle_ != 0 && this->have_settings_ &&
         this->settings_fresh_;
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

  const bool current_state = (this->settings_flags_ & SETTINGS_ECO_MASK) != 0;
  if (current_state == state) {
    // Avoid a redundant write and the confirmation beep produced by the station.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_flags = this->settings_flags_;
  if (state) {
    this->settings_flags_ |= SETTINGS_ECO_MASK;
  } else {
    this->settings_flags_ &= static_cast<uint8_t>(~SETTINGS_ECO_MASK);
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
  if (std::find(ECO_SHUTDOWN_HOURS.begin(), ECO_SHUTDOWN_HOURS.end(), hours) == ECO_SHUTDOWN_HOURS.end()) {
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
  if (std::find(WORK_MODES.begin(), WORK_MODES.end(), mode) == WORK_MODES.end()) {
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

  const uint8_t current_mode = (this->settings_flags_ & SETTINGS_WORK_MODE_MASK) >> SETTINGS_WORK_MODE_SHIFT;
  if (current_mode == mode) {
    // Avoid a redundant write and the audible acknowledgement produced by the
    // R600 for accepted settings commands.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_flags = this->settings_flags_;
  this->settings_flags_ =
      static_cast<uint8_t>((this->settings_flags_ & static_cast<uint8_t>(~SETTINGS_WORK_MODE_MASK)) |
                           static_cast<uint8_t>(mode << SETTINGS_WORK_MODE_SHIFT));

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

  const bool current_state = (this->settings_flags_ & SETTINGS_CAR_CHARGER_MASK) != 0;
  if (current_state == state) {
    // Accepted settings writes produce an audible acknowledgement, so avoid a
    // command that cannot change the confirmed state.
    this->publish_settings_states_();
    return true;
  }

  const uint8_t old_flags = this->settings_flags_;
  if (state) {
    this->settings_flags_ |= SETTINGS_CAR_CHARGER_MASK;
  } else {
    this->settings_flags_ &= static_cast<uint8_t>(~SETTINGS_CAR_CHARGER_MASK);
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

bool AllpowersBLE::send_control_frame_() {
  // Exact control frame and check-byte calculation from allpowers-ble. The
  // final byte is reproduced from upstream behavior; it is not treated as a
  // generally understood checksum algorithm.
  std::array<uint8_t, 9> frame{{0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, 0x00, 0x71}};
  if (this->dc_on_)
    frame[7] |= CONTROL_DC_MASK;
  if (this->ac_on_)
    frame[7] |= CONTROL_AC_MASK;
  // Upstream writes light on bit 5, although notifications report it on bit 4.
  if (this->light_on_)
    frame[7] |= CONTROL_LIGHT_MASK;

  frame[8] = static_cast<uint8_t>(113U - frame[7]);
  if (this->ac_on_)
    frame[8] = static_cast<uint8_t>(frame[8] + 4U);

  return this->write_frame_(frame.data(), frame.size(), "output control");
}

bool AllpowersBLE::send_settings_frame_() {
  // Command 0x02 always carries the settings bitmap and ECO timeout together.
  // Callers update one field in the shared snapshot before using this builder.
  std::array<uint8_t, 10> frame{
      {0xA5, 0x65, 0x00, 0xB1, 0x01, 0x02, 0x02, this->settings_flags_, this->eco_time_, 0x00}};
  uint8_t checksum = 0;
  for (size_t index = 0; index < frame.size() - 1; index++)
    checksum ^= frame[index];
  frame.back() = checksum;

  return this->write_frame_(frame.data(), frame.size(), "settings");
}

bool AllpowersBLE::write_frame_(uint8_t *data, size_t length, const char *description) {
  if (this->node_state != espbt::ClientState::ESTABLISHED || this->write_handle_ == 0) {
    ESP_LOGW(TAG, "Cannot write ALLPOWERS %s frame: BLE client is not ready", description);
    return false;
  }

  esp_gatt_write_type_t write_type;
  if ((this->write_properties_ & ESP_GATT_CHAR_PROP_BIT_WRITE) != 0) {
    write_type = ESP_GATT_WRITE_TYPE_RSP;
  } else if ((this->write_properties_ & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) != 0) {
    write_type = ESP_GATT_WRITE_TYPE_NO_RSP;
  } else {
    ESP_LOGE(TAG, "Characteristic FFF2 does not advertise a writable property");
    this->set_protocol_error_(true);
    return false;
  }

  ESP_LOGD(TAG, "Writing %s frame: %s", description, format_hex_pretty(data, length).c_str());
  const esp_err_t result =
      esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_,
                               static_cast<uint16_t>(length), data, write_type, ESP_GATT_AUTH_REQ_NONE);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gattc_write_char failed: %s", esp_err_to_name(result));
    this->set_protocol_error_(true);
    return false;
  }
  return true;
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

void AllpowersBLEEcoShutdownTimeSelect::publish_hours(uint8_t hours) {
  const auto option = std::find(ECO_SHUTDOWN_HOURS.begin(), ECO_SHUTDOWN_HOURS.end(), hours);
  if (option == ECO_SHUTDOWN_HOURS.end()) {
    // Preserve unknown protocol values in the parent snapshot, but do not
    // claim that one of the four verified UI options is active.
    this->clear_state();
    return;
  }
  this->publish_state(static_cast<size_t>(option - ECO_SHUTDOWN_HOURS.begin()));
}

void AllpowersBLEEcoShutdownTimeSelect::control(size_t index) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS ECO shutdown time select has no parent component");
    return;
  }
  if (index >= ECO_SHUTDOWN_HOURS.size()) {
    ESP_LOGE(TAG, "Invalid ECO shutdown time option index: %u", static_cast<unsigned>(index));
    return;
  }
  this->parent_->request_eco_shutdown_time(ECO_SHUTDOWN_HOURS[index]);
}

void AllpowersBLEWorkModeSelect::publish_mode(uint8_t mode) {
  const auto option = std::find(WORK_MODES.begin(), WORK_MODES.end(), mode);
  if (option == WORK_MODES.end()) {
    // Protocol value 3 is reserved by the two-bit field. Keep it in the parent
    // snapshot for future writes, but do not present it as a known mode.
    this->clear_state();
    return;
  }
  this->publish_state(static_cast<size_t>(option - WORK_MODES.begin()));
}

void AllpowersBLEWorkModeSelect::control(size_t index) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "ALLPOWERS work mode select has no parent component");
    return;
  }
  if (index >= WORK_MODES.size()) {
    ESP_LOGE(TAG, "Invalid work mode option index: %u", static_cast<unsigned>(index));
    return;
  }
  this->parent_->request_work_mode(WORK_MODES[index]);
}

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
