# Migration guide

## Unreleased: power-flow binary sensor terminology

The binary sensors previously exposed as `Battery Charging` and
`Battery Discharging` were derived only from total input and output power. Those
measurements do not prove battery-current direction, especially during
pass-through or UPS-like operation.

The entities are therefore renamed as follows:

| Previous YAML key / package ID | New YAML key / package ID | Meaning |
|---|---|---|
| `charging` / `${allpowers_id}_charging` | `input_power_active` / `${allpowers_id}_input_power_active` | Total measured input power is greater than zero |
| `discharging` / `${allpowers_id}_discharging` | `output_power_active` / `${allpowers_id}_output_power_active` | Total measured output power is greater than zero |

Update Home Assistant automations, dashboards and history references to the new
entity IDs after installing this release. During pass-through operation both
entities may correctly be on at the same time.

No battery-flow estimate is introduced. Calculating input minus output would not
account for conversion losses, auxiliary consumption or measurement location.
