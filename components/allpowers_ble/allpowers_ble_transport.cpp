// SPDX-License-Identifier: MIT

#include "allpowers_ble_transport.h"

#include <esp_err.h>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::allpowers_ble {

static const char *const TAG = "allpowers_ble.transport";

void AllpowersBLETransport::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                                esp_ble_gattc_cb_param_t *param) {
  (void) gattc_if;
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK || param->open.status == ESP_GATT_ALREADY_OPEN)
        this->on_transport_connected_();
      break;

    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->reset_transport_state_();
      this->on_transport_disconnected_();
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *notify_characteristic =
          this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(NOTIFY_UUID));
      auto *write_characteristic =
          this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(WRITE_UUID));

      const bool notify_found = notify_characteristic != nullptr;
      const bool write_found = write_characteristic != nullptr;
      const bool notify_supported =
          notify_found &&
          (notify_characteristic->properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE)) != 0;
      const bool write_supported =
          write_found &&
          (write_characteristic->properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) != 0;
      const DiscoveryResult discovery =
          evaluate_characteristics(notify_found, write_found, notify_supported, write_supported);
      if (discovery_failed(discovery)) {
        this->fail_discovery_(discovery);
        break;
      }

      this->notify_handle_ = notify_characteristic->handle;
      this->write_handle_ = write_characteristic->handle;
      this->write_properties_ = write_characteristic->properties;

      const esp_err_t status = esp_ble_gattc_register_for_notify(
          this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), this->notify_handle_);
      if (status != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gattc_register_for_notify failed: %s", esp_err_to_name(status));
        this->fail_discovery_(evaluate_notification_registration(false, false));
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "Notification registration failed, status=%d", param->reg_for_notify.status);
        this->fail_discovery_(evaluate_notification_registration(true, false));
      } else {
        this->node_state = espbt::ClientState::ESTABLISHED;
        ESP_LOGI(TAG, "Subscribed to ALLPOWERS notifications");
        this->on_transport_ready_();
      }
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == this->notify_handle_)
        this->on_transport_notification_(param->notify.value, param->notify.value_len);
      break;

    case ESP_GATTC_WRITE_CHAR_EVT:
      if (param->write.handle == this->write_handle_ && param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "ALLPOWERS write failed, status=%d", param->write.status);
        this->on_transport_error_("GATT write failed", true);
      }
      break;

    default:
      break;
  }
}

bool AllpowersBLETransport::transport_ready_() const {
  return this->node_state == espbt::ClientState::ESTABLISHED && this->notify_handle_ != 0 && this->write_handle_ != 0;
}

bool AllpowersBLETransport::write_transport_frame_(uint8_t *data, size_t length, const char *description) {
  if (!this->transport_ready_()) {
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
    this->on_transport_error_("FFF2 is not writable", false);
    return false;
  }

  ESP_LOGD(TAG, "Writing %s frame: %s", description, format_hex_pretty(data, length).c_str());
  const esp_err_t result =
      esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_,
                               static_cast<uint16_t>(length), data, write_type, ESP_GATT_AUTH_REQ_NONE);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "esp_ble_gattc_write_char failed: %s", esp_err_to_name(result));
    this->on_transport_error_("GATT write could not be queued", false);
    return false;
  }
  return true;
}

void AllpowersBLETransport::disconnect_transport_() { this->parent()->disconnect(); }

void AllpowersBLETransport::fail_discovery_(DiscoveryResult result) {
  const char *reason = discovery_result_message(result);
  ESP_LOGE(TAG, "Unusable ALLPOWERS GATT session: %s; scheduling reconnect", reason);
  this->reset_transport_state_();
  this->on_transport_error_(reason, true);
}

void AllpowersBLETransport::reset_transport_state_() {
  this->notify_handle_ = 0;
  this->write_handle_ = 0;
  this->write_properties_ = static_cast<esp_gatt_char_prop_t>(0);
}

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
