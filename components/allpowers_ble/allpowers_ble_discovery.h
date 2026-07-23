// SPDX-License-Identifier: MIT
//
// Pure GATT discovery decisions. Keeping this state independent from ESP-IDF
// makes every terminal discovery path testable without BLE hardware.

#pragma once

#include <cstdint>

namespace esphome::allpowers_ble {

enum class DiscoveryResult : uint8_t {
  READY_TO_SUBSCRIBE = 0,
  SUBSCRIBED,
  MISSING_NOTIFY_CHARACTERISTIC,
  MISSING_WRITE_CHARACTERISTIC,
  NOTIFY_UNSUPPORTED,
  WRITE_UNSUPPORTED,
  NOTIFICATION_REGISTRATION_QUEUE_FAILED,
  NOTIFICATION_REGISTRATION_FAILED,
};

constexpr DiscoveryResult evaluate_characteristics(bool notify_found, bool write_found, bool notify_supported,
                                                   bool write_supported) {
  if (!notify_found)
    return DiscoveryResult::MISSING_NOTIFY_CHARACTERISTIC;
  if (!write_found)
    return DiscoveryResult::MISSING_WRITE_CHARACTERISTIC;
  if (!notify_supported)
    return DiscoveryResult::NOTIFY_UNSUPPORTED;
  if (!write_supported)
    return DiscoveryResult::WRITE_UNSUPPORTED;
  return DiscoveryResult::READY_TO_SUBSCRIBE;
}

constexpr DiscoveryResult evaluate_notification_registration(bool queued, bool completed_successfully) {
  if (!queued)
    return DiscoveryResult::NOTIFICATION_REGISTRATION_QUEUE_FAILED;
  return completed_successfully ? DiscoveryResult::SUBSCRIBED : DiscoveryResult::NOTIFICATION_REGISTRATION_FAILED;
}

constexpr bool discovery_failed(DiscoveryResult result) {
  return result != DiscoveryResult::READY_TO_SUBSCRIBE && result != DiscoveryResult::SUBSCRIBED;
}

constexpr const char *discovery_result_message(DiscoveryResult result) {
  switch (result) {
    case DiscoveryResult::READY_TO_SUBSCRIBE:
      return "GATT characteristics are usable";
    case DiscoveryResult::SUBSCRIBED:
      return "notification subscription established";
    case DiscoveryResult::MISSING_NOTIFY_CHARACTERISTIC:
      return "required FFF1 notify characteristic was not found";
    case DiscoveryResult::MISSING_WRITE_CHARACTERISTIC:
      return "required FFF2 write characteristic was not found";
    case DiscoveryResult::NOTIFY_UNSUPPORTED:
      return "FFF1 has no notify or indicate property";
    case DiscoveryResult::WRITE_UNSUPPORTED:
      return "FFF2 has no writable property";
    case DiscoveryResult::NOTIFICATION_REGISTRATION_QUEUE_FAILED:
      return "notification registration could not be queued";
    case DiscoveryResult::NOTIFICATION_REGISTRATION_FAILED:
      return "notification registration failed";
  }
  return "unknown GATT discovery failure";
}

}  // namespace esphome::allpowers_ble
