"""Protocol regression tests that do not require ESPHome or BLE hardware."""

from dataclasses import dataclass

FRAME_MIN_LENGTH = 8
STATUS_COMMAND = 0x01
SETTINGS_STATUS_COMMAND = 0x03
SETTINGS_WRITE_COMMAND = 0x02
ECO_MODE_MASK = 0x01
ECO_SHUTDOWN_HOURS = (1, 2, 4, 6)
WORK_MODE_MASK = 0x06
WORK_MODE_SHIFT = 1
WORK_MODES = (0, 1, 2)
CAR_CHARGER_MASK = 0x10
DEVICE_NAME_COMMAND = 0x35
MAX_DEVICE_NAME_LENGTH = 96


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
    work_mode: int
    car_charger_enabled: bool
    hardware_version: str
    firmware_version: str


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


def format_version(encoded_version: int) -> str:
    """Mirror the station-version conversion used by the component."""

    major = encoded_version >> 4
    minor = encoded_version & 0x0F
    if major > 9 or minor > 9:
        return f"0x{encoded_version:02X}"
    return f"{major}.{minor}"


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
        work_mode=(flags & WORK_MODE_MASK) >> WORK_MODE_SHIFT,
        car_charger_enabled=bool(flags & CAR_CHARGER_MASK),
        hardware_version=format_version(packet[11]),
        firmware_version=format_version(packet[12]),
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


def make_device_name_frame(name: str = "") -> bytes:
    """Build the command-0x35 query/update frame found in the official app."""

    payload = name.encode("utf-8")
    if len(payload) > MAX_DEVICE_NAME_LENGTH or (name and not payload):
        raise ValueError("device name must contain 1-96 UTF-8 bytes")
    frame = bytearray((0xA5, 0x65, 0x00, 0xB1, 0x01, len(payload), DEVICE_NAME_COMMAND))
    frame.extend(payload)
    frame.append(xor_bytes(frame))
    return bytes(frame)


def parse_device_name(packet: bytes) -> str:
    """Validate and decode a non-empty command-0x35 response."""

    validate_notification(packet)
    if packet[6] != DEVICE_NAME_COMMAND or not 1 <= packet[5] <= MAX_DEVICE_NAME_LENGTH:
        raise ValueError("not a device-name response")
    return packet[7:-1].decode("utf-8")


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


def make_work_mode_frame(*, settings_flags: int, eco_time: int, mode: int) -> bytes:
    """Change only the two verified work-mode bits."""

    if mode not in WORK_MODES:
        raise ValueError("unsupported work mode")
    flags = (settings_flags & ~WORK_MODE_MASK) | (mode << WORK_MODE_SHIFT)
    return make_settings_frame(settings_flags=flags, eco_time=eco_time)


def make_car_charger_frame(
    *, settings_flags: int, eco_time: int, enabled: bool
) -> bytes:
    """Change only the verified car-charger bit."""

    flags = (
        settings_flags | CAR_CHARGER_MASK
        if enabled
        else settings_flags & ~CAR_CHARGER_MASK
    )
    return make_settings_frame(settings_flags=flags, eco_time=eco_time)


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
    assert parsed.work_mode == 3
    assert parsed.car_charger_enabled
    assert parsed.hardware_version == "1.2"
    assert parsed.firmware_version == "3.4"


def test_version_formatter_preserves_unexpected_encoding() -> None:
    """Expose non-decimal nibbles as raw data rather than a false version."""

    assert format_version(0x01) == "0.1"
    assert format_version(0x10) == "1.0"
    assert format_version(0xAF) == "0xAF"


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


def test_work_mode_preserves_other_settings() -> None:
    """Encode all app modes without changing unrelated settings fields."""

    expected = {
        0: "A56500B1010202F9048D",
        1: "A56500B1010202FB048F",
        2: "A56500B1010202FD0489",
    }
    for mode, expected_hex in expected.items():
        frame = make_work_mode_frame(settings_flags=0xFF, eco_time=4, mode=mode)
        assert frame.hex().upper() == expected_hex
        assert (frame[7] & WORK_MODE_MASK) >> WORK_MODE_SHIFT == mode
        assert frame[7] & ~WORK_MODE_MASK == 0xF9
        assert frame[8] == 4
        assert xor_bytes(frame) == 0


def test_car_charger_preserves_other_settings() -> None:
    """Toggle bit 4 without changing other settings or the ECO timeout."""

    enabled = make_car_charger_frame(settings_flags=0xA6, eco_time=4, enabled=True)
    disabled = make_car_charger_frame(settings_flags=0xB6, eco_time=4, enabled=False)

    assert enabled.hex().upper() == "A56500B1010202B604C2"
    assert disabled.hex().upper() == "A56500B1010202A604D2"
    assert enabled[7] & ~CAR_CHARGER_MASK == 0xA6
    assert disabled[7] & ~CAR_CHARGER_MASK == 0xA6
    assert enabled[8] == disabled[8] == 4
    assert xor_bytes(enabled) == xor_bytes(disabled) == 0


def test_device_name_query_and_utf8_update() -> None:
    """Match the app's empty query and UTF-8 byte-counted update frames."""

    query = make_device_name_frame()
    update = make_device_name_frame("R600 Garaje")
    unicode_update = make_device_name_frame("Estaci\u00f3n \u26a1")

    assert query.hex().upper() == "A56500B101003545"
    assert update.hex().upper() == "A56500B1010B355236303020476172616A6530"
    assert unicode_update[5] == len("Estaci\u00f3n \u26a1".encode("utf-8"))
    assert xor_bytes(query) == xor_bytes(update) == xor_bytes(unicode_update) == 0


def test_device_name_response_and_limits() -> None:
    """Decode a confirmed response and reject names above the app's limit."""

    response = bytearray(make_device_name_frame("Taller"))
    response[2], response[3] = response[3], response[2]
    response[-1] = xor_bytes(response[:-1])
    assert parse_device_name(bytes(response)) == "Taller"

    try:
        make_device_name_frame("\u00e1" * 49)
    except ValueError:
        pass
    else:
        raise AssertionError("device name above 96 UTF-8 bytes was accepted")


def test_unsupported_work_mode_is_rejected() -> None:
    """Reject the reserved two-bit value and values outside the field."""

    for mode in (-1, 3, 4, 255):
        try:
            make_work_mode_frame(settings_flags=0x37, eco_time=4, mode=mode)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsupported work mode accepted: {mode}")


if __name__ == "__main__":
    test_status_parser()
    test_settings_parser()
    test_invalid_notifications_are_rejected()
    test_all_control_combinations()
    test_eco_write_preserves_unmanaged_settings()
    test_eco_shutdown_time_preserves_settings_bitmap()
    test_unsupported_eco_shutdown_time_is_rejected()
    test_work_mode_preserves_other_settings()
    test_car_charger_preserves_other_settings()
    test_device_name_query_and_utf8_update()
    test_device_name_response_and_limits()
    test_unsupported_work_mode_is_rejected()
    print("Protocol tests passed")
