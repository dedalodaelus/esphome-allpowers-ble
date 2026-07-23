// SPDX-License-Identifier: MIT
//
// BLE/GATT transport for the ALLPOWERS protocol. Protocol bytes are forwarded
// to the component without interpreting command payload fields.

#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#include "allpowers_ble_discovery.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>

namespace esphome::allpowers_ble {

namespace espbt = esphome::esp32_ble_tracker;

class AllpowersBLETransport : public ble_client::BLEClientNode {
 public:
  void set_service_uuid16(uint16_t uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_uint16(uuid); }
  void set_service_uuid32(uint32_t uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_uint32(uuid); }
  void set_service_uuid128(const uint8_t *uuid) { this->service_uuid_ = espbt::ESPBTUUID::from_raw(uuid); }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

 protected:
  static constexpr uint16_t NOTIFY_UUID = 0xFFF1;
  static constexpr uint16_t WRITE_UUID = 0xFFF2;

  bool transport_ready_() const;
  bool write_transport_frame_(uint8_t *data, size_t length, const char *description);
  void disconnect_transport_();
  void reset_transport_state_();

  virtual void on_transport_connected_() {}
  virtual void on_transport_disconnected_() {}
  virtual void on_transport_ready_() {}
  virtual void on_transport_notification_(const uint8_t *data, uint16_t length) {}
  virtual void on_transport_error_(const char *reason, bool reconnect) {}

  espbt::ESPBTUUID service_uuid_{espbt::ESPBTUUID::from_uint16(0xFFF0)};

 private:
  void fail_discovery_(DiscoveryResult result);

  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};
  esp_gatt_char_prop_t write_properties_{};
};

}  // namespace esphome::allpowers_ble

#endif  // USE_ESP32
