# Protocol notes

This project does not claim independent reverse engineering of the complete ALLPOWERS protocol.
The original telemetry offsets and AC/DC/light command are derived from the public
[`madninjaskillz/allpowers-ble`](https://github.com/madninjaskillz/allpowers-ble) project.
The settings format was cross-checked against the official Android application logic and repeatable
R600 BLE captures supplied by the project maintainer.

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

## Status subscription and connection health

After notification registration, the component sends this observed station-facing request:

```text
A5 65 B1 00 01 06 01 00 00 00 00 00
```

`allpowers-companion` describes it as the request that starts or refreshes periodic status broadcasts. It
is not treated as an XOR notification envelope: its bytes are reproduced exactly. The component sends it
once after subscribing and then at `keepalive_interval` (20 seconds by default).

Every structurally valid notification, including a valid but otherwise unsupported command, proves that
the protocol link is alive. If none arrives within `watchdog_timeout` (45 seconds by default), the
component asks ESPHome's BLE client to close the GATT link. The existing `auto_connect` policy then handles
rediscovery and reconnection. A guard prevents repeated disconnect requests while the close event is
pending. Invalid frames deliberately do not reset the watchdog.

Notification-subscription failures and asynchronous GATT write failures also schedule a single disconnect
from the component loop. This avoids performing GATT teardown inside an ESP-IDF callback while ensuring that
ESPHome rediscovers services and characteristic handles instead of retaining an unusable session.

An independent, disabled-by-default settings keepalive is available for R600 firmware that appears to close
an otherwise healthy GATT session after roughly ten minutes. When enabled, the component resends the latest
complete command-`0x03` settings snapshot as command `0x02` every 9 minutes by default. It never constructs
settings from defaults and skips the write when no snapshot has been received during the current connection.
This workaround is separate from the 20-second status request because that request does not appear to reset
the R600's station-side connection timer. A queued output-control or settings write restarts the settings-
keepalive interval, avoiding an unnecessary repeat immediately after a user command. The interval should
remain below the observed disconnect timeout.
Accepted settings writes can produce an audible confirmation, which is why this behavior requires explicit
opt-in and hardware validation.

This behavior was independently implemented from the public behavioral description and observed command
in `R0b0To/allpowers-companion`; no GPL-licensed source code was copied. It supplements rather than
replaces `stale_timeout`: the latter invalidates old entity values, while the watchdog repairs an apparently
connected link that no longer transports valid packets.

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

State of charge is the only telemetry field with a documented semantic range:
values from `0` through `100` are accepted and `101` through `255` are rejected.
No maximum power or remaining-time limit is imposed because the available
protocol evidence does not define one; unknown fields and unverified sentinel
values are therefore preserved rather than guessed.

## Output control frame

The existing AC/DC/light command has this shape:

```text
A5 65 00 B1 01 01 00 SS CC
```

`SS` contains the combined AC, DC and light states. `CC` reproduces the calculation used by
the upstream implementation. It is not presented as a general checksum algorithm.

## Settings notification: command `0x03`

A complete settings response has at least 14 bytes. The implemented settings controls use these fields:

| Offset | Meaning |
|---|---|
| Byte 7, bit 0 | ECO enabled |
| Byte 7, bits 1-2 | Work mode: `0` Mute, `1` Standard, `2` Fast; `3` reserved |
| Byte 7, bit 3 | AC mode, preserved but not exposed |
| Byte 7, bit 4 | Independent car charger/12 V automotive socket |
| Byte 7, bit 5 | Self-use mode, preserved but not exposed |
| Byte 7, bits 6-7 | Reserved, preserved verbatim |
| Byte 8 | ECO shutdown time in hours: `1`, `2`, `4` or `6` |
| Bytes 9-10 | Charging-time field, not exposed |
| Byte 11 | Hardware version |
| Byte 12 | Firmware/software version |

The component stores bytes 7 and 8 as a raw settings snapshot. ECO mode, shutdown-time, work-mode and
car-charger writes remain unavailable until this snapshot is received and become unavailable again when it
is stale or BLE disconnects. Unknown timeout values and the reserved work-mode value are preserved but are not mapped to
verified select options.

The official application interprets bytes 11 and 12 by converting each byte to hexadecimal text and treating
that text as a decimal version divided by ten. The component presents valid decimal nibbles as `major.minor`
(for example, `0x12` becomes `1.2`). A byte containing hexadecimal digits `A` through `F` is exposed as raw
`0xNN` data rather than being converted to a misleading version.

## Settings write: command `0x02`

```text
A5 65 00 B1 01 02 02 SS TT XX
```

- For an ECO mode change, `SS` is copied from the latest settings notification with only bit 0 changed.
- For a shutdown-time change, `TT` is replaced with `1`, `2`, `4` or `6`; `SS` is copied unchanged.
- For a work-mode change, only bits 1-2 of `SS` are replaced with `0`, `1` or `2`.
- For a car-charger change, only bit 4 of `SS` is changed.
- Every field not being changed is copied verbatim from the latest settings notification.
- `XX` is the XOR of bytes 0 through 8.

This read-modify-write rule prevents one settings control from silently changing another, or changing AC mode,
self-use mode or reserved bits. All settings controls use the same stored snapshot and frame builder, which
remains the extension point for additional verified settings.

The official application sends a separate buzzer-control command after selecting Mute Mode. This component
intentionally does not reproduce that unrelated side effect; Work Mode changes only bits 1-2 of the settings
bitmap.

## Experimental device name: command `0x35`

The supplied official Android application contains a separate implementation for SOLIX/VOLIX P1800:

```text
A5 65 00 B1 01 LL 35 [LL UTF-8 bytes] XX
```

- An empty payload queries the stored name.
- A non-empty payload updates it.
- The application truncates at a UTF-8 code-point boundary to at most 96 bytes.
- `XX` is the XOR of all preceding bytes.
- A response uses command `0x35` and carries the current UTF-8 name.

The component validates length, checksum and UTF-8, and publishes only a returned value. It does not
optimistically claim that a write succeeded. After notification subscription it waits 500 ms, then sends at
most three queries, 3 seconds apart. A valid `0x35` response cancels the remaining attempts. The retry budget
is reset for each new GATT connection and is never used as continuous polling.

The read-only station-name sensor uses one validation and persistence path for command `0x35` responses and
BLE advertisements. Valid names are trimmed, limited to 96 UTF-8 bytes and written to preferences only when
they differ from the value already stored for the configured station MAC. Empty names, unavailable-value
placeholders and strings containing control characters leave the stored value published. A changed MAC
invalidates a non-empty stored name before it can be restored; the next valid advertisement or `0x35`
response establishes the new MAC/name pair.

This command is disabled by default because the app gates it to SOLIX/VOLIX P1800; it has not been
demonstrated on the R600 or the other families supported by the telemetry parser. Enabling it on another
model is an explicit hardware experiment, not a compatibility claim.

## Experimental field

AC frequency is based on an unmerged upstream contribution and is exposed as an experimental,
disabled-by-default diagnostic entity.

## Not implemented

The currently verified implementation does not expose:

- Charging limits
- Self-use mode
- USB output as an independent channel
- Battery temperature, voltage or current
- Internal alarms and error codes
- Remaining energy in watt-hours
- Device-name changes on models other than the P1800 family

These features should only be added after their state and write behavior are validated on real
hardware. Unknown settings bits must continue to be preserved during every write.
