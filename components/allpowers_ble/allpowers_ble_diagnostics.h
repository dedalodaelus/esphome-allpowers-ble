// SPDX-License-Identifier: MIT
//
// Runtime-only BLE diagnostic history. No preference object is used, so
// recording packet failures never writes to flash.

#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace esphome::allpowers_ble {

class ProtocolDiagnostics {
 public:
  void record_error(const std::string &reason, uint32_t uptime_ms) {
    if (this->total_errors_ != std::numeric_limits<uint32_t>::max())
      this->total_errors_++;
    if (this->consecutive_errors_ != std::numeric_limits<uint32_t>::max())
      this->consecutive_errors_++;
    this->last_error_reason_ = reason;
    this->last_error_uptime_ms_ = uptime_ms;
    this->error_latched_ = true;
  }

  void record_success() { this->consecutive_errors_ = 0; }

  // A successfully subscribed GATT session starts with a clear current-error
  // latch. Historical totals and the last failure remain available until the
  // ESP restarts.
  void reset_session() {
    this->consecutive_errors_ = 0;
    this->error_latched_ = false;
  }

  uint32_t total_errors() const { return this->total_errors_; }
  uint32_t consecutive_errors() const { return this->consecutive_errors_; }
  uint32_t last_error_uptime_ms() const { return this->last_error_uptime_ms_; }
  const std::string &last_error_reason() const { return this->last_error_reason_; }
  bool error_latched() const { return this->error_latched_; }

 private:
  uint32_t total_errors_{0};
  uint32_t consecutive_errors_{0};
  uint32_t last_error_uptime_ms_{0};
  std::string last_error_reason_;
  bool error_latched_{false};
};

}  // namespace esphome::allpowers_ble
