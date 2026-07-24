# Compatibility

Compatibility is determined by the BLE protocol, not by the product branding.

## Protocol requirements

A compatible device must provide:

- GATT service `FFF0` by default, or another service supplied through `allpowers_service_uuid`
- Notification or indication characteristic `FFF1`
- Writable characteristic `FFF2`
- Status notifications of at least 16 bytes with header `A5 65`
- The field offsets documented in [`protocol.md`](protocol.md)
- The combined AC/DC/light control frame used by the upstream `allpowers-ble` project

## Compatibility matrix

| Model/family | Evidence | Status |
|---|---|---|
| R600 | Primary hardware used while developing this ESPHome port | Tested target; additional firmware revisions welcome |
| S300 | [Explicitly documented by the upstream Home Assistant integration](https://github.com/madninjaskillz/ha-allpowers-ble) | Expected compatible; ESPHome reports welcome |
| Devices advertising as `AP S*` | [Included by the upstream Home Assistant discovery filter](https://github.com/madninjaskillz/ha-allpowers-ble/blob/master/custom_components/allpowers/manifest.json) | Potentially compatible if packet layout matches |
| S500 | [Upstream issue reports an incompatible payload](https://github.com/madninjaskillz/allpowers-ble/issues/1) | Unsupported until a protocol profile is contributed |
| S700 V2 | [Upstream issue reports an incompatible payload](https://github.com/madninjaskillz/allpowers-ble/issues/1) | Unsupported until a protocol profile is contributed |
| Other models | No verified public mapping | Unknown |

## How to report a compatible model

Open the dedicated [Compatibility report](https://github.com/dedalodaelus/esphome-allpowers-ble/issues/new?template=compatibility_report.yml) and include:

1. Exact model, label/hardware revision and station firmware.
2. BLE local name and whether the address is stable or rotates.
3. Component revision, ESPHome version, ESP32 board and framework.
4. Service UUID plus notification/write UUIDs and their properties.
5. At least one complete sanitized notification frame and its capture conditions.
6. Values shown simultaneously by the official application or display and by ESPHome.
7. Initial and final physical state for each control tested, or an explicit telemetry-only result.

A model should not be listed as supported based only on a successful BLE connection.
