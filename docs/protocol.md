# Protocol notes

This project does not claim independent reverse engineering of the complete ALLPOWERS protocol.
The implemented field offsets and control-frame logic are derived from the public
[`madninjaskillz/allpowers-ble`](https://github.com/madninjaskillz/allpowers-ble) project.

The upstream implementation uses one common parser and command encoder rather than separate model
profiles. This component therefore supports devices that reproduce that packet format; it does not
assume that every ALLPOWERS model is compatible.

## GATT layout

- Service: `FFF0`
- Notifications: `FFF1`
- Writes: `FFF2`

The service UUID remains configurable because firmware or hardware revisions may differ.

## Status fields

For status notifications of at least 15 bytes:

| Offset | Meaning |
|---|---|
| Byte 7, bit 0 | DC output state |
| Byte 7, bit 1 | AC output state |
| Byte 7, bit 4 | Light state |
| Byte 8 | Battery state of charge, percent |
| Bytes 9-10 | Total input power, big-endian watts |
| Bytes 11-12 | Total output power, big-endian watts |
| Bytes 13-14 | Estimated remaining time, big-endian minutes |

Short packets and packets without the `A5 65` header are rejected safely.

## Output control frame

```text
A5 65 00 B1 01 01 00 SS CC
```

`SS` contains the combined AC, DC and light states. `CC` reproduces the calculation used by the
upstream implementation. It is not presented as a general checksum algorithm.

Only combinations supported by the upstream implementation are generated. Unknown commands are
not guessed.

## Experimental field

AC frequency is based on an unmerged upstream contribution and is exposed as an experimental,
disabled-by-default diagnostic entity.

## Not implemented

The public protocol information used by this project does not verify commands or fields for:

- ECO mode
- Charging mode or charging limits
- USB output as an independent channel
- Battery temperature, voltage or current
- Internal alarms and error codes
- Remaining energy in watt-hours

These features require repeatable BLE captures from the official application and model-specific
validation.
