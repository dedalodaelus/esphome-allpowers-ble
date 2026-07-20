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

void AllpowersBLE::setup() {
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(false);
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(false);
  this->publish_controls_available_(false);
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
  if (!this->data_fresh_ || this->stale_timeout_ms_ == 0 || this->last_valid_packet_ms_ == 0)
    return;

  // The GATT link can remain open even when the station stops publishing
  // telemetry. Expire the last snapshot so Home Assistant does not present
  // stale values or allow writes based on an obsolete output bitmap.
  if (millis() - this->last_valid_packet_ms_ > this->stale_timeout_ms_) {
    this->data_fresh_ = false;
    this->have_status_ = false;
    if (this->data_valid_binary_sensor_ != nullptr)
      this->data_valid_binary_sensor_->publish_state(false);
    this->publish_controls_available_(false);
    this->invalidate_data_entities_();
    ESP_LOGW(TAG, "No valid ALLPOWERS status packet received within the configured timeout");
  }
}

void AllpowersBLE::reset_connection_state_() {
  // Drop every handle and protocol-derived state together. A later reconnect
  // must rediscover GATT and receive a fresh status frame before controls are
  // exposed again.
  this->notify_handle_ = 0;
  this->write_handle_ = 0;
  this->write_properties_ = static_cast<esp_gatt_char_prop_t>(0);
  this->have_status_ = false;
  this->data_fresh_ = false;
  this->last_valid_packet_ms_ = 0;
  if (this->connected_binary_sensor_ != nullptr)
    this->connected_binary_sensor_->publish_state(false);
  if (this->data_valid_binary_sensor_ != nullptr)
    this->data_valid_binary_sensor_->publish_state(false);
  this->publish_controls_available_(false);
  this->invalidate_data_entities_();
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
        ESP_LOGW(TAG, "Control write failed, status=%d", param->write.status);
        this->set_protocol_error_(true);
      }
      break;

    default:
      break;
  }
}

void AllpowersBLE::process_notification_(const uint8_t *data, uint16_t length) {
  ESP_LOGV(TAG, "Notification (%u bytes): %s", length, format_hex_pretty(data, length).c_str());

  if (this->packet_length_sensor_ != nullptr)
    this->packet_length_sensor_->publish_state(length);

  // The upstream implementation indexes bytes 7..14 directly. A known R600 V2.0
  // report includes shorter packets, so all short packets are ignored safely.
  if (length < MIN_STATUS_PACKET_LENGTH) {
    ESP_LOGW(TAG, "Ignoring short notification: expected at least %u bytes, received %u",
             static_cast<unsigned>(MIN_STATUS_PACKET_LENGTH), length);
    this->set_protocol_error_(true);
    return;
  }

  // All frames documented in the upstream source/captures start with A5 65.
  if (data[0] != 0xA5 || data[1] != 0x65) {
    ESP_LOGW(TAG, "Ignoring notification with unknown header: %02X %02X", data[0], data[1]);
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
  // protocol_error describes current protocol health rather than a latched
  // historical fault. Any later valid frame clears transient parse/write
  // errors; logs retain the original diagnostic details.
  this->set_protocol_error_(false);

  this->publish_switch_states_();
}

void AllpowersBLE::publish_switch_states_() {
  if (this->ac_switch_ != nullptr)
    this->ac_switch_->publish_state(this->ac_on_);
  if (this->dc_switch_ != nullptr)
    this->dc_switch_->publish_state(this->dc_on_);
  if (this->light_switch_ != nullptr)
    this->light_switch_->publish_state(this->light_on_);
}

bool AllpowersBLE::controls_available_() const {
  return this->node_state == espbt::ClientState::ESTABLISHED && this->write_handle_ != 0 && this->have_status_ &&
         this->data_fresh_;
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

bool AllpowersBLE::send_control_frame_() {
  if (this->node_state != espbt::ClientState::ESTABLISHED || this->write_handle_ == 0) {
    ESP_LOGW(TAG, "Cannot write ALLPOWERS control frame: BLE client is not ready");
    return false;
  }

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

  ESP_LOGD(TAG, "Writing control frame: %s", format_hex_pretty(frame.data(), frame.size()).c_str());
  const esp_err_t result =
      esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_,
                               static_cast<uint16_t>(frame.size()), frame.data(), write_type, ESP_GATT_AUTH_REQ_NONE);
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

void AllpowersBLE::publish_controls_available_(bool state) {
  if (this->controls_available_binary_sensor_ != nullptr)
    this->controls_available_binary_sensor_->publish_state(state);
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

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
