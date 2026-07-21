# Protocol notes

This project does not claim independent reverse engineering of the complete ALLPOWERS protocol.
The original telemetry offsets and AC/DC/light command are derived from the public
[`madninjaskillz/allpowers-ble`](https://github.com/madninjaskillz/allpowers-ble) project.
The ECO settings format was cross-checked against the official Android application logic and
repeatable R600 BLE captures supplied by the project maintainer.

## GATT layout used

- Service: `FFF0`
- Notifications: `FFF1`
- Writes: `FFF2`

The service UUID remains configurable because firmware or hardware revisions may differ.

## Notification envelope

Notifications use this envelope:

```text
A5 65 .. .. .. LL CC [LL payload bytes] XX
```

- Byte 5 (`LL`) is the payload length.
- Byte 6 (`CC`) selects the response command.
- Total frame length must equal `8 + LL`.
- `XX` is the XOR of all preceding bytes; XOR across the complete frame is therefore zero.

Valid but unsupported command families are ignored rather than interpreted using telemetry offsets.

## Telemetry notification: command `0x01`

For complete command-`0x01` notifications of at least 16 bytes:

| Offset | Meaning |
|---|---|
| Byte 7, bit 0 | DC output state |
| Byte 7, bit 1 | AC output state |
| Byte 7, bit 4 | Light state |
| Byte 8 | Battery state of charge, percent |
| Bytes 9-10 | Total input power, big-endian watts |
| Bytes 11-12 | Total output power, big-endian watts |
| Bytes 13-14 | Estimated remaining time, big-endian minutes |

## Output control frame

The existing AC/DC/light command has this shape:

```text
A5 65 00 B1 01 01 00 SS CC
```

`SS` contains the combined AC, DC and light states. `CC` reproduces the calculation used by
the upstream implementation. It is not presented as a general checksum algorithm.

## Settings notification: command `0x03`

A complete settings response has at least 14 bytes. The fields needed for ECO are:

| Offset | Meaning |
|---|---|
| Byte 7, bit 0 | ECO enabled |
| Byte 7, bits 1-2 | Charging mode, preserved but not exposed |
| Byte 7, bit 3 | AC mode, preserved but not exposed |
| Byte 7, bit 4 | Car/DC port, preserved but not exposed |
| Byte 7, bit 5 | Self-use mode, preserved but not exposed |
| Byte 7, bits 6-7 | Reserved, preserved verbatim |
| Byte 8 | ECO timeout, preserved but not exposed |
| Bytes 9-10 | Charging-time field, not exposed |
| Bytes 11-12 | Hardware and software versions, not exposed |

The component stores bytes 7 and 8 as a raw settings snapshot. ECO remains unavailable until this
snapshot is received and becomes unavailable again when it is stale or BLE disconnects.

## ECO write: command `0x02`

```text
A5 65 00 B1 01 02 02 SS TT XX
```

- `SS` is copied from the latest settings notification with only bit 0 changed.
- `TT` is the existing ECO timeout copied from that notification.
- `XX` is the XOR of bytes 0 through 8.

This read-modify-write rule prevents an ECO toggle from silently changing charging mode, AC mode,
the car/DC port, self-use mode or reserved bits. The same stored snapshot and frame builder can be
reused when additional settings are implemented later.

## Experimental field

AC frequency is based on an unmerged upstream contribution and is exposed as an experimental,
disabled-by-default diagnostic entity.

## Not implemented

The currently verified implementation does not expose:

- ECO timeout selection
- Charging mode or charging limits
- Car/DC output as an independent control
- Self-use mode
- USB output as an independent channel
- Battery temperature, voltage or current
- Internal alarms and error codes
- Remaining energy in watt-hours

These features should only be added after their state and write behavior are validated on real
hardware. Unknown settings bits must continue to be preserved during every write.
