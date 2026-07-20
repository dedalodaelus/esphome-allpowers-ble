# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
for published releases.

## [Unreleased]

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

### Security

- Avoid storing installation-specific MAC addresses, IP addresses, credentials,
  or Home Assistant secrets in the public examples.
- Document that the optional ESPHome web server should use digest authentication
  rather than basic authentication.

[Unreleased]: https://github.com/dedalodaelus/esphome-allpowers-ble/commits/devel
