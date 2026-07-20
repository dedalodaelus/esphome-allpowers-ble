#!/usr/bin/env python3
"""Protocol regression tests derived from madninjaskillz/allpowers-ble."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Status:
    dc_on: bool
    ac_on: bool
    light_on: bool
    frequency_hz_experimental: int
    soc_percent: int
    input_w: int
    output_w: int
    remaining_min: int


def parse_status(data: bytes) -> Status:
    """Decode only the status fields verified by the upstream implementation."""

    if len(data) < 15:
        raise ValueError("status packet must contain at least 15 bytes")
    if data[:2] != b"\xa5\x65":
        raise ValueError("unknown frame header")

    status = data[7]
    return Status(
        dc_on=bool(status & (1 << 0)),
        ac_on=bool(status & (1 << 1)),
        light_on=bool(status & (1 << 4)),
        frequency_hz_experimental=60 if status & (1 << 2) else 50,
        soc_percent=data[8],
        input_w=int.from_bytes(data[9:11], "big"),
        output_w=int.from_bytes(data[11:13], "big"),
        remaining_min=int.from_bytes(data[13:15], "big"),
    )


def make_control_frame(*, dc_on: bool, ac_on: bool, light_on: bool) -> bytes:
    """Reproduce the upstream combined-output command for regression testing."""

    status = 0
    if dc_on:
        status |= 1 << 0
    if ac_on:
        status |= 1 << 1
    if light_on:
        # Commands use bit 5 for the light, while notifications report it on
        # bit 4. Keeping this asymmetry in the test protects against an easy
        # but incompatible "cleanup" of the production implementation.
        status |= 1 << 5

    check = (113 - status + (4 if ac_on else 0)) & 0xFF
    return bytes((0xA5, 0x65, 0x00, 0xB1, 0x01, 0x01, 0x00, status, check))


def test_status_parser() -> None:
    # 16-byte example matching the offsets implemented upstream.
    packet = bytes.fromhex("A5 65 B1 00 01 08 01 13 64 01 2C 00 FA 00 78 00")
    parsed = parse_status(packet)
    assert parsed.dc_on
    assert parsed.ac_on
    assert parsed.light_on
    assert parsed.frequency_hz_experimental == 50
    assert parsed.soc_percent == 100
    assert parsed.input_w == 300
    assert parsed.output_w == 250
    assert parsed.remaining_min == 120


def test_short_and_unknown_frames_are_rejected() -> None:
    for packet in (b"", b"\xa5\x65" + bytes(12), b"\x00\x65" + bytes(13)):
        try:
            parse_status(packet)
        except ValueError:
            pass
        else:
            raise AssertionError(f"packet should have been rejected: {packet.hex()}")


def test_all_control_combinations() -> None:
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


if __name__ == "__main__":
    test_status_parser()
    test_short_and_unknown_frames_are_rejected()
    test_all_control_combinations()
    print("Protocol tests passed")
