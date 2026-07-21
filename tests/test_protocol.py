"""Protocol regression tests that do not require ESPHome or BLE hardware."""

from dataclasses import dataclass

FRAME_MIN_LENGTH = 8
STATUS_COMMAND = 0x01
SETTINGS_STATUS_COMMAND = 0x03
SETTINGS_WRITE_COMMAND = 0x02
ECO_MODE_MASK = 0x01
ECO_SHUTDOWN_HOURS = (1, 2, 4, 6)


@dataclass(frozen=True)
class Status:
    """Fields exposed from a command-0x01 status notification."""

    dc_on: bool
    ac_on: bool
    light_on: bool
    frequency_hz_experimental: int
    soc_percent: int
    input_w: int
    output_w: int
    remaining_min: int


@dataclass(frozen=True)
class SettingsStatus:
    """Raw settings snapshot required for safe read-modify-write commands."""

    flags: int
    eco_time: int
    eco_enabled: bool


def xor_bytes(data: bytes | bytearray) -> int:
    """Return the XOR of all bytes in *data*."""

    checksum = 0
    for value in data:
        checksum ^= value
    return checksum


def validate_notification(packet: bytes) -> None:
    """Validate the envelope used by notifications from the official app."""

    if len(packet) < FRAME_MIN_LENGTH:
        raise ValueError("short packet")
    if packet[0:2] != b"\xa5\x65":
        raise ValueError("unknown header")
    if len(packet) != FRAME_MIN_LENGTH + packet[5]:
        raise ValueError("inconsistent payload length")
    if xor_bytes(packet) != 0:
        raise ValueError("invalid XOR checksum")


def parse_status(packet: bytes) -> Status:
    """Parse a command-0x01 telemetry notification."""

    validate_notification(packet)
    if packet[6] != STATUS_COMMAND or len(packet) < 16:
        raise ValueError("not a complete status packet")

    status = packet[7]
    return Status(
        dc_on=bool(status & 0x01),
        ac_on=bool(status & 0x02),
        light_on=bool(status & 0x10),
        frequency_hz_experimental=60 if status & 0x04 else 50,
        soc_percent=packet[8],
        input_w=int.from_bytes(packet[9:11], "big"),
        output_w=int.from_bytes(packet[11:13], "big"),
        remaining_min=int.from_bytes(packet[13:15], "big"),
    )


def parse_settings(packet: bytes) -> SettingsStatus:
    """Parse a command-0x03 settings notification."""

    validate_notification(packet)
    if packet[6] != SETTINGS_STATUS_COMMAND or len(packet) < 14:
        raise ValueError("not a complete settings packet")

    flags = packet[7]
    return SettingsStatus(
        flags=flags,
        eco_time=packet[8],
        eco_enabled=bool(flags & ECO_MODE_MASK),
    )


def make_control_frame(*, dc_on: bool, ac_on: bool, light_on: bool) -> bytes:
    """Reproduce the existing upstream AC/DC/light command."""

    flags = 0
    if dc_on:
        flags |= 0x01
    if ac_on:
        flags |= 0x02
    if light_on:
        flags |= 0x20

    checksum = 113 - flags
    if ac_on:
        checksum += 4
    return bytes((0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, flags, checksum))


def make_settings_frame(*, settings_flags: int, eco_time: int) -> bytes:
    """Build the shared command-0x02 frame from an updated raw snapshot."""

    frame = bytearray(
        (
            0xA5,
            0x65,
            0x00,
            0xB1,
            0x01,
            0x02,
            SETTINGS_WRITE_COMMAND,
            settings_flags,
            eco_time,
        )
    )
    frame.append(xor_bytes(frame))
    return bytes(frame)


def make_eco_mode_frame(
    *, settings_flags: int, eco_time: int, eco_enabled: bool
) -> bytes:
    """Change only the ECO bit while preserving the rest of the snapshot."""

    flags = (
        settings_flags | ECO_MODE_MASK
        if eco_enabled
        else settings_flags & ~ECO_MODE_MASK
    )
    return make_settings_frame(settings_flags=flags, eco_time=eco_time)


def make_eco_shutdown_time_frame(*, settings_flags: int, hours: int) -> bytes:
    """Change only the verified ECO timeout byte."""

    if hours not in ECO_SHUTDOWN_HOURS:
        raise ValueError("unsupported ECO shutdown time")
    return make_settings_frame(settings_flags=settings_flags, eco_time=hours)


def test_status_parser() -> None:
    """Decode the known command-0x01 field offsets and a valid checksum."""

    packet = bytes.fromhex("A5 65 B1 00 01 08 01 13 64 01 2C 00 FA 00 78 A1")
    parsed = parse_status(packet)
    assert parsed.dc_on
    assert parsed.ac_on
    assert parsed.light_on
    assert parsed.frequency_hz_experimental == 50
    assert parsed.soc_percent == 100
    assert parsed.input_w == 300
    assert parsed.output_w == 250
    assert parsed.remaining_min == 120


def test_settings_parser() -> None:
    """Retain the complete bitmap while exposing the ECO bit."""

    packet = bytes.fromhex("A5 65 B1 00 01 06 03 37 04 00 5A 12 34 3A")
    parsed = parse_settings(packet)
    assert parsed.flags == 0x37
    assert parsed.eco_time == 4
    assert parsed.eco_enabled


def test_invalid_notifications_are_rejected() -> None:
    """Reject malformed envelopes before reading command-specific offsets."""

    packets = (
        b"",
        b"\xa5\x65" + bytes(12),
        b"\x00\x65" + bytes(14),
        bytes.fromhex("A5 65 B1 00 01 08 01 13 64 01 2C 00 FA 00 78 00"),
    )
    for packet in packets:
        try:
            validate_notification(packet)
        except ValueError:
            pass
        else:
            raise AssertionError(f"packet should have been rejected: {packet.hex()}")


def test_all_control_combinations() -> None:
    """Keep the existing AC/DC/light command stable."""

    expected = {
        (False, False, False): "A56500B10101000071",
        (True, False, False): "A56500B10101000170",
        (False, True, False): "A56500B10101000273",
        (True, True, False): "A56500B10101000372",
        (False, False, True): "A56500B10101002051",
        (True, False, True): "A56500B10101002150",
        (False, True, True): "A56500B10101002253",
        (True, True, True): "A56500B10101002352",
    }
    for (dc_on, ac_on, light_on), expected_hex in expected.items():
        actual = make_control_frame(dc_on=dc_on, ac_on=ac_on, light_on=light_on)
        assert actual.hex().upper() == expected_hex


def test_eco_write_preserves_unmanaged_settings() -> None:
    """Toggle bit 0 without changing mode bits, reserved bits or ECO time."""

    enabled = make_eco_mode_frame(settings_flags=0x36, eco_time=4, eco_enabled=True)
    disabled = make_eco_mode_frame(settings_flags=0x37, eco_time=4, eco_enabled=False)

    assert enabled.hex().upper() == "A56500B1010202370443"
    assert disabled.hex().upper() == "A56500B1010202360442"
    assert enabled[7] & ~ECO_MODE_MASK == 0x36
    assert disabled[7] & ~ECO_MODE_MASK == 0x36
    assert enabled[8] == disabled[8] == 4
    assert xor_bytes(enabled) == xor_bytes(disabled) == 0


def test_eco_shutdown_time_preserves_settings_bitmap() -> None:
    """Encode every verified timeout without changing any settings bit."""

    expected = {
        1: "A56500B1010202370146",
        2: "A56500B1010202370245",
        4: "A56500B1010202370443",
        6: "A56500B1010202370641",
    }
    for hours, expected_hex in expected.items():
        frame = make_eco_shutdown_time_frame(settings_flags=0x37, hours=hours)
        assert frame.hex().upper() == expected_hex
        assert frame[7] == 0x37
        assert frame[8] == hours
        assert xor_bytes(frame) == 0


def test_unsupported_eco_shutdown_time_is_rejected() -> None:
    """Do not invent protocol values outside the four options in the app."""

    for hours in (0, 3, 5, 7, 255):
        try:
            make_eco_shutdown_time_frame(settings_flags=0x37, hours=hours)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsupported ECO shutdown time accepted: {hours}")


if __name__ == "__main__":
    test_status_parser()
    test_settings_parser()
    test_invalid_notifications_are_rejected()
    test_all_control_combinations()
    test_eco_write_preserves_unmanaged_settings()
    test_eco_shutdown_time_preserves_settings_bitmap()
    test_unsupported_eco_shutdown_time_is_rejected()
    print("Protocol tests passed")
