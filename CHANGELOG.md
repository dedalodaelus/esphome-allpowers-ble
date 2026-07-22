# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
for published releases.

## [Unreleased]

### Added

- ECO mode state and control using the R600 settings command, gated by a fresh
  command-`0x03` settings snapshot.
- ECO shutdown-time state and control for the verified 1, 2, 4 and 6 hour values.
- Work mode state and control for Mute, Standard and Fast modes.
- Independent car charger/12 V automotive socket state and control using the
  verified settings bitmap.
- Power-station hardware and firmware version diagnostics from command `0x03`.
- Separate `Settings Available` readiness and confirmed `ECO Mode Status`
  entities for safe Home Assistant controls.
- Experimental, disabled-by-default command-`0x35` Bluetooth name query and
  update with UTF-8, byte-length and response validation.
- Read-only Bluetooth advertised-name diagnostic captured passively without
  requiring or enabling the experimental rename command.
- Bounded pre-connection advertisement window that tries to capture a fresh BLE
  local name without allowing a missing name to block the connection.
- Configurable connection-health keepalive (20 seconds by default) that requests
  a fresh status broadcast immediately after subscribing and periodically.
- Configurable 45-second connection watchdog that recycles an apparently
  connected GATT link after valid protocol traffic stops.
- Experimental, disabled-by-default keepalive (due to beep) that resends the
  last station-reported settings snapshot every 9 minutes by default.
- Persistent Home Assistant controls for enabling keepalive
  and changing its interval from 1 to 9 minutes without reflashing.
- Initial settings keepalive after every newly established BLE session, sent
  as soon as that session provides a real settings snapshot, plus a manual
  Home Assistant button for a one-shot resend.
- Flash-backed station name shared by BLE advertisements and command `0x35`,
  with invalid-name filtering, write deduplication and MAC-bound invalidation.

### Changed

- Split notification handling by protocol command so settings reports cannot be
  misinterpreted as telemetry and valid unsupported commands are ignored safely.
- Regression tests covering notification envelopes, status/settings parsing,
  all AC/DC/light output combinations, ECO read-modify-write preservation,
  every supported ECO shutdown-time value, all verified work modes, and both
  car-charger states.
- Reuse ESPHome's native auto-connect lifecycle after watchdog-initiated
  disconnects instead of maintaining a second reconnect scheduler.
- Rebuild the BLE session after notification-subscription or asynchronous GATT
  write failures instead of leaving an unusable link marked as established.
- Retry the optional command-`0x35` name query up to three times per connection
  and cancel the remaining attempts after a valid response.

### Fixed

- Preserve charging mode, AC mode, car/DC port, self-use, reserved bits and the
  current ECO timeout when toggling ECO.
- Preserve the complete settings bitmap when changing ECO shutdown time and
  reject values not exposed by the official R600 application.
- Preserve ECO, car/DC, AC mode, self-use, reserved bits and timeout when
  changing work mode; reject the reserved two-bit value.
- Disable the experimental car charger/12 V automotive socket entity by default
  until reliable control has been confirmed on hardware.

## [0.1.0] - 2026/07/20

### Added

- External ESPHome component for ALLPOWERS power stations that use the BLE
  protocol implemented by the upstream `madninjaskillz/allpowers-ble` project.
- BLE status parsing for battery level, total input power, total output power,
  estimated remaining time, output states, and protocol diagnostics.
- Control of AC output, DC output, and the integrated light using the verified
  combined output command.
- Experimental AC-frequency reporting for devices that expose the corresponding
  status bit.
- Configurable GATT service UUID, stale-data timeout, scan parameters, initial
  connection behavior, and RSSI update interval.
- Persistent connection control with `Disabled`, `Searching`, and `Connected`
  states.
- Automatic reconnection while the connection control is enabled, and immediate
  disconnection with reconnection disabled when it is turned off.
- Separate Home Assistant subdevice grouping for the power station when the
  ESP32 also hosts unrelated entities.
- Reusable remote ESPHome package and generic examples for dedicated nodes and
  Bluetooth Proxy nodes.
- Optional Home Assistant template switches with dynamic availability.
- Protocol documentation, compatibility matrix, BLE capture instructions,
  migration notes, contribution guidelines, and third-party attribution.
- Regression tests covering status parsing, invalid frames, and all AC/DC/light
  output combinations.
- GitHub Actions validation for Python linting, C++ formatting and linting,
  YAML linting, protocol tests, and ESP32/ESP32-S3 firmware compilation.

### Changed

- Generalized the original R600-specific component and package names to
  `allpowers_ble` so compatibility is expressed by protocol rather than by a
  single product model.
- Improved entity names and diagnostic categories for clearer presentation in
  Home Assistant.
- Replaced protocol magic numbers with named offsets and bit-mask constants.
- Expanded comments and docstrings to explain protocol decisions, connection
  state, safety invariants, GATT setup, and ESPHome code-generation details
  without commenting trivial operations.
- Aligned Python and C++ formatting and lint configuration with ESPHome project
  conventions.
- Made `allpowers_mac` a required package variable instead of using a
  syntactically valid placeholder address.
- Refactored CI into one code-quality job and an ESP32/ESP32-S3 build matrix.
- Improved the optional Home Assistant wrappers with block-level unique IDs,
  predictable default entity IDs, and additional availability checks.

### Fixed

- Declared the Python code-generation enum as a scoped C++ enum so generated
  references use `OutputType::AC`, `OutputType::DC`, and `OutputType::LIGHT`.
- Corrected `uint32_t` log formatting by using the portable `PRIu32` macro.
- Reject short or unrecognized BLE notification frames before reading fixed
  offsets.
- Invalidate telemetry and binary-state entities when BLE disconnects or data
  becomes stale, causing Home Assistant to show unknown values instead of stale
  measurements.
- Block AC, DC, and light commands until the BLE link is established and a fresh
  complete status frame has established the current combined output bitmap.
- Prevent commands requested while disconnected from being deferred and applied
  unexpectedly after reconnection.
- Preserve the last confirmed output bitmap when a GATT write cannot be queued.
- Prevent manual BLE disconnection from being immediately undone by automatic
  reconnection.
- Preserve charging mode, AC mode, car/DC port, self-use, reserved bits and the
  current ECO timeout when toggling ECO.
- Preserve the complete settings bitmap when changing ECO shutdown time and
  reject values not exposed by the official R600 application.
- Preserve ECO, car/DC, AC mode, self-use, reserved bits and timeout when
  changing work mode; reject the reserved two-bit value.

### Security

- Avoid storing installation-specific MAC addresses, IP addresses, credentials,
  or Home Assistant secrets in the public examples.
- Document that the optional ESPHome web server should use digest authentication
  rather than basic authentication.

[Unreleased]: https://github.com/dedalodaelus/esphome-allpowers-ble/commits/devel
