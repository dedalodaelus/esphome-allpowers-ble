# ESPHome ALLPOWERS BLE

[![Build and Code Quality](https://github.com/dedalodaelus/esphome-allpowers-ble/actions/workflows/validate.yml/badge.svg)](https://github.com/dedalodaelus/esphome-allpowers-ble/actions/workflows/validate.yml)

Experimental ESPHome external component and reusable package for monitoring and controlling
ALLPOWERS portable power stations that use the BLE protocol implemented by
[`madninjaskillz/allpowers-ble`](https://github.com/madninjaskillz/allpowers-ble).

> [!IMPORTANT]
> Compatibility is based on the BLE protocol, not the ALLPOWERS brand name alone. This project does
> **not** claim support for every ALLPOWERS power station. See the compatibility matrix below.

> [!WARNING]
> This is independent community software. It is not affiliated with or endorsed by ALLPOWERS,
> ESPHome, Home Assistant or the upstream project author. Treat output controls as alpha software
> until they have been physically validated on your exact model and firmware.

## Compatibility

The upstream Python library uses one packet layout and the same `FFF1` notification and `FFF2`
write characteristics for every device it handles. It does not contain separate model profiles.
The upstream Home Assistant integration advertises support for the S300 and variants whose BLE name
matches `AP S*`.

| Device or family | Status in this project |
|---|---|
| ALLPOWERS R600 | Primary tested target for this ESPHome port |
| ALLPOWERS S300 | Supported by the upstream implementation; community validation requested |
| Other `AP S*` devices using the same packet format | Expected to work, but must be validated per model |
| S500 and S700 V2 | Reported incompatible with the upstream packet parser; not claimed as supported |
| Other ALLPOWERS models | Unknown until GATT layout and notification frames are confirmed |

A device is compatible only when it exposes the expected service/characteristics and sends the
same status frame format. See [`docs/compatibility.md`](docs/compatibility.md).

## Features

- Battery level
- Total input power
- Total output power
- Estimated remaining runtime
- AC, DC and light state
- AC, DC and light control
- ECO mode state and control on devices that publish the command-`0x03`
  settings notification
- ECO shutdown time selection: 1, 2, 4 or 6 hours
- Work mode selection: Mute, Standard or Fast
- Power-station hardware and firmware version diagnostics
- Independent car charger/12 V automotive socket state and control,
  disabled by default while hardware support is validated
- Experimental Bluetooth device-name query and update using command `0x35`;
  disabled by default and only evidenced by the official app for SOLIX/VOLIX P1800
- Charging and discharging indicators derived from power flow
- BLE connection state: `Disabled`, `Searching` or `Connected`
- Persistent connection control:
  - ON searches until connected and reconnects after link loss
  - OFF disconnects and stops attempting to connect
- Active connection health: requests a fresh status broadcast every 20 seconds and recycles a
  GATT link after 45 seconds without any valid protocol packet
- Telemetry becomes unknown after BLE disconnection or stale data
- Commands are rejected until the BLE link and telemetry are valid
- ECO mode, shutdown-time, work-mode and car-charger commands are rejected until a fresh
  complete settings snapshot is available; every unrelated setting is preserved verbatim
- Power-station entities appear as a separate Home Assistant subdevice
- Optional Bluetooth RSSI and protocol diagnostics
- Multiple instances are supported when each package instance uses a unique `allpowers_id`; optional Home Assistant wrappers also require unique block IDs and station-specific source entity IDs

## Requirements

- ESP32 with BLE client support
- ESPHome `2026.7.0` or newer
- ESP-IDF framework recommended
- Stable BLE MAC address for the power station
- A compatible `FFF0`/`FFF1`/`FFF2` GATT layout and packet format

ESP32-S3 is the recommended general-purpose target. A classic ESP32 also works. ESP32-C6 does not
provide a specific advantage for this protocol.

## Repository layout

```text
components/allpowers_ble/    ESPHome external component
packages/allpowers_ble.yaml  Reusable entities, subdevice and connection package
examples/                    Standalone, Bluetooth Proxy and local examples
tests/                       Protocol regression tests and CI configuration
docs/                        Compatibility, protocol, migration and capture notes
home_assistant/              Optional unavailable-state wrapper switches
```

## Installation from GitHub

```yaml
external_components:
  - source: github://dedalodaelus/esphome-allpowers-ble@main
    components:
      - allpowers_ble

packages:
  allpowers_station:
    url: https://github.com/dedalodaelus/esphome-allpowers-ble
    ref: main
    refresh: 1d
    files:
      - path: packages/allpowers_ble.yaml
        vars:
          allpowers_id: allpowers
          allpowers_name: "ALLPOWERS Power Station"
          allpowers_mac: !secret allpowers_station_mac
```

Add the MAC to your local `secrets.yaml`:

```yaml
allpowers_station_mac: "AA:BB:CC:DD:EE:FF"
```

The public package contains no secret lookups of its own. Passing a local secret through package
variables keeps the device address outside the repository.

Complete configurations are provided in:

- [`examples/minimal.yaml`](examples/minimal.yaml)
- [`examples/bluetooth-proxy.yaml`](examples/bluetooth-proxy.yaml)
- [`examples/local-development.yaml`](examples/local-development.yaml)

## Package variables

| Variable | Default | Purpose |
|---|---|---|
| `allpowers_id` | `allpowers` | Unique ESPHome ID prefix; use letters, numbers and underscores |
| `allpowers_name` | `ALLPOWERS Power Station` | Home Assistant subdevice name |
| `allpowers_mac` | Required | Stable BLE MAC address of the power station |
| `allpowers_service_uuid` | `FFF0` | Configurable service UUID |
| `allpowers_stale_timeout` | `30s` | Time before telemetry becomes unknown |
| `allpowers_keepalive_interval` | `20s` | Interval between status-broadcast refresh requests; minimum `5s` |
| `allpowers_watchdog_timeout` | `45s` | Silence before forcing reconnection; minimum `10s` and longer than keepalive |
| `allpowers_enable_experimental_device_name` | `false` | Opt in to command-`0x35` name query/update |
| `allpowers_connect_at_boot` | `true` | Start persistent searching after boot |
| `allpowers_bootstrap_delay` | `10s` | Delay before initial BLE search |
| `allpowers_scan_interval` | `320ms` | ESP32 BLE scan interval |
| `allpowers_scan_window` | `320ms` | ESP32 BLE scan window |
| `allpowers_scan_active` | `true` | Active BLE scanning |
| `allpowers_rssi_update_interval` | `60s` | Optional RSSI update interval |

## Bluetooth Proxy connection slots

If the same ESP32 also acts as a Home Assistant Bluetooth Proxy, reserve one connection slot per
ALLPOWERS BLE client in addition to the proxy slots:

```yaml
esp32_ble:
  max_connections: 4

bluetooth_proxy:
  active: true
  connection_slots: 3
```

## Entities

### Primary

| Entity | Type |
|---|---|
| Battery Level | Sensor |
| Input Power | Sensor |
| Output Power | Sensor |
| Estimated Time Remaining | Sensor |
| AC Output | Switch |
| DC Output | Switch |
| Light | Switch |
| ECO Mode | Switch |
| ECO Shutdown Time | Select |
| Work Mode | Select |
| Car Charger | Switch |
| Bluetooth Name (Experimental) | Text |
| AC Output Status | Binary sensor |
| DC Output Status | Binary sensor |
| Light Status | Binary sensor |
| ECO Mode Status | Binary sensor |
| Battery Charging | Binary sensor |
| Battery Discharging | Binary sensor |

### Connection and diagnostics

| Entity | Purpose |
|---|---|
| Keep BLE Connected | Desired persistent connection state |
| BLE Connection Status | `Disabled`, `Searching` or `Connected` |
| BLE Connected | Physical BLE link state |
| Telemetry Available | A recent valid status frame exists |
| Controls Available | Output commands can be sent safely |
| Settings Available | A recent settings frame permits safe settings-backed writes |
| BLE Protocol Error | Malformed notification or failed BLE transaction detected |

When the BLE link is disconnected or telemetry expires, numeric and binary telemetry entities are
invalidated. Native ESPHome controls cannot all publish availability independently; `Settings Available`
gates both ECO writes in firmware, and the component clears the select's confirmed value when its settings
snapshot expires.

The package already used ESPHome's native `auto_connect` behavior to reconnect after a real BLE
disconnect. The connection-health layer covers a different failure mode: a GATT link that still appears
connected but has stopped carrying notifications. It sends the observed status request immediately after
notification registration and on the configured cadence. If no structurally valid protocol packet arrives
before `allpowers_watchdog_timeout`, the component closes the link once; the existing ESPHome BLE client
then discovers and reconnects it. The 30-second stale-data invalidation remains independent from the
45-second connection watchdog. A failed notification subscription or asynchronous GATT write also closes
the link from the main loop so ESPHome can rebuild the complete session.

The status request and default timing are behavioral observations from
[`R0b0To/allpowers-companion`](https://github.com/R0b0To/allpowers-companion). That project is GPL-3.0;
this MIT implementation was written independently and does not incorporate its source code. The behavior
has not yet been physically verified on every model, so retain the defaults until device logs demonstrate
a need to tune them.

### Optional Home Assistant unavailable controls

The optional wrapper in
[`home_assistant/allpowers_ble_controls.yaml.example`](home_assistant/allpowers_ble_controls.yaml.example)
creates AC, DC, light and ECO controls with explicit Home Assistant availability. AC, DC and light use
`Controls Available`; ECO uses `Settings Available` because its write must preserve the complete
settings bitmap and current ECO timeout. The wrapper uses confirmed binary sensors for state and calls
the original ESPHome switches to execute commands. The ECO shutdown-time select is provided directly by
ESPHome and is protected by the same `Settings Available` firmware gate.

Copy the example to your Home Assistant packages directory, for example:

```text
/config/packages/allpowers_ble_controls.yaml
```

Enable packages in `configuration.yaml` if necessary:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Replace the source entity IDs in the copied file with those generated by your Home Assistant
instance. Keep the original ESPHome switches enabled because the wrappers call them; they may be
hidden from dashboards, but must not be disabled.

For multiple power stations, use a separate wrapper block for each station and change its block-level
`unique_id`, displayed names, `default_entity_id` values and all referenced source entity IDs. The
block-level ID is combined with each entity ID, preventing collisions between stations.

After editing the file, check the Home Assistant configuration and reload Template entities or restart
Home Assistant. The wrapper is optional and does not replace the connection and command-safety checks
implemented by the ESPHome component.

## Known limitations

Charging limits, self-use mode, independent USB control, voltage, current, temperature, internal alarms
and remaining energy in watt-hours are not exposed. Their settings bits are preserved when ECO mode, its
shutdown time, work mode or the car charger is changed rather than being invented or reset.

ECO stays unavailable on devices that do not publish the compatible command-`0x03` settings frame.
This prevents the generic package from assuming that every station with the ALLPOWERS brand uses the
R600 settings protocol.

AC frequency is experimental and disabled by default. See [`docs/protocol.md`](docs/protocol.md).

Bluetooth renaming is more narrowly supported than the telemetry protocol. The extracted official
Android app encodes command `0x35` as UTF-8 with a 96-byte maximum, but explicitly permits the BLE
rename path only for `SOLIX P1800`/`VOLIX P1800`. There is no evidence in the supplied app that an
R600 accepts the command. Consequently, both the package option
`allpowers_enable_experimental_device_name` and the disabled-by-default Home Assistant text entity
must be enabled deliberately. The entity is non-optimistic: it remains unknown or retains its last
confirmed value unless the station returns a valid command-`0x35` name response.

The separate `BLE Advertised Name` diagnostic is always read-only. It captures the name passively
from advertisements matching `allpowers_mac`, so it does not depend on command `0x35` support or
send any request to the station. Active scanning must remain enabled for the complete advertised
name to be obtained from scan-response data.

## Local validation

Install the pinned dependencies first:

```bash
pip install -r requirements-lint.txt
pip install -r requirements-ci.txt
```

Run the quality checks:

```bash
python -m compileall -q components tests
ruff check components tests
ruff format --check components tests
flake8 components tests
pylint components/allpowers_ble tests/test_protocol.py
clang-format --dry-run --Werror \
  components/allpowers_ble/allpowers_ble.h \
  components/allpowers_ble/allpowers_ble.cpp
cpplint \
  components/allpowers_ble/allpowers_ble.h \
  components/allpowers_ble/allpowers_ble.cpp
yamllint \
  .github/workflows \
  packages \
  examples \
  tests/ci.yaml \
  home_assistant/allpowers_ble_controls.yaml.example
python tests/test_protocol.py
```

Compile both targets used by GitHub Actions:

```bash
esphome \
  -s test_board esp32dev \
  -s test_device_name allpowers-ble-ci-esp32 \
  compile tests/ci.yaml

esphome \
  -s test_board esp32-s3-devkitc-1 \
  -s test_device_name allpowers-ble-ci-esp32-s3 \
  compile tests/ci.yaml
```

GitHub Actions runs the same quality checks and compiles both the classic ESP32 and ESP32-S3 targets.

## Reporting compatibility

Include:

- Exact model and hardware revision
- Firmware version shown by the official application
- BLE local name
- Service and characteristic UUIDs
- ESPHome version
- ESP32 model and framework
- Sanitized logs
- Which readings and controls were physically verified

Never publish Wi-Fi credentials, API encryption keys, OTA passwords or complete Android bug reports.

## Credits and development disclosure

- BLE protocol source and original communication logic:
  [`madninjaskillz/allpowers-ble`](https://github.com/madninjaskillz/allpowers-ble), MIT licensed.
- Initial R600 ESPHome port developed with assistance from OpenAI ChatGPT.
- Generalization and maintenance by [`dedalodaelus`](https://github.com/dedalodaelus).
- Generated and adapted code must be reviewed and validated against real hardware by maintainers and
  contributors.

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## License

MIT. See [`LICENSE`](LICENSE).
