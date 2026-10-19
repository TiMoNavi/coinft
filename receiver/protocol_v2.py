"""Framing and decoding for the revised Teensy/CoinFT stream.

The dual-sensor packet is fixed at 58 bytes::

    00 00 | uint32_le sequence | uint32_le teensy_time_us |
    uint16_le left[12] | uint16_le right[12]

The current revised firmware emits a bare 24-byte payload when only one
sensor is active.  That mode is supported explicitly because it has no
reliable byte-level resynchronization.  A future single-sensor stream can use
``single-v2`` (header, sequence, time, and one payload) without changing the
calibration or receiver layers.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
import time
from typing import Any


HEADER = b"\x00\x00"
CHANNELS = 12
BYTES_PER_SENSOR = CHANNELS * 2
UINT32_MOD = 2**32


class CoinFTProtocolError(ValueError):
    """Raised when a complete byte sequence cannot be decoded."""


class CoinFTPacketTimeout(TimeoutError):
    """Raised when a packet is not completed before the read deadline."""


@dataclass(frozen=True)
class CoinFTPacket:
    """One decoded sample from one or two physical CoinFTs."""

    sequence_id: int | None
    teensy_time_us: int | None
    sensors: tuple[tuple[int, ...], ...]
    raw_packet: bytes
    wire_format: str

    @property
    def sensor_count(self) -> int:
        return len(self.sensors)


def _read_exact(serial_port: Any, count: int, deadline: float) -> bytes:
    chunks: list[bytes] = []
    remaining = count
    while remaining:
        if time.monotonic() >= deadline:
            raise CoinFTPacketTimeout(f"timed out reading {count} bytes")
        chunk = serial_port.read(remaining)
        if chunk:
            chunks.append(bytes(chunk))
            remaining -= len(chunk)
    return b"".join(chunks)


def _find_header(serial_port: Any, deadline: float) -> int:
    previous_zero = False
    skipped = 0
    while time.monotonic() < deadline:
        value = serial_port.read(1)
        if not value:
            continue
        if value == b"\x00":
            if previous_zero:
                return skipped
            previous_zero = True
            continue
        skipped += 1 + int(previous_zero)
        previous_zero = False
    raise CoinFTPacketTimeout("no complete CoinFT header found")


def _decode_sensor_payload(payload: bytes, count: int) -> tuple[tuple[int, ...], ...]:
    expected = count * BYTES_PER_SENSOR
    if len(payload) != expected:
        raise CoinFTProtocolError(f"expected {expected} payload bytes, got {len(payload)}")
    values = struct.unpack("<" + "H" * (count * CHANNELS), payload)
    return tuple(
        tuple(int(value) for value in values[index * CHANNELS : (index + 1) * CHANNELS])
        for index in range(count)
    )


def decode_packet(packet: bytes, wire_format: str) -> CoinFTPacket:
    """Decode a complete packet according to an explicit wire format."""

    if wire_format == "dual-v2":
        expected = len(HEADER) + 8 + 2 * BYTES_PER_SENSOR
        if len(packet) != expected or packet[:2] != HEADER:
            raise CoinFTProtocolError(f"dual-v2 packet must be {expected} bytes")
        sequence_id, teensy_time_us = struct.unpack("<II", packet[2:10])
        sensors = _decode_sensor_payload(packet[10:], 2)
        return CoinFTPacket(sequence_id, teensy_time_us, sensors, packet, wire_format)

    if wire_format == "single-v2":
        expected = len(HEADER) + 8 + BYTES_PER_SENSOR
        if len(packet) != expected or packet[:2] != HEADER:
            raise CoinFTProtocolError(f"single-v2 packet must be {expected} bytes")
        sequence_id, teensy_time_us = struct.unpack("<II", packet[2:10])
        sensors = _decode_sensor_payload(packet[10:], 1)
        return CoinFTPacket(sequence_id, teensy_time_us, sensors, packet, wire_format)

    if wire_format == "single-framed24":
        expected = len(HEADER) + BYTES_PER_SENSOR
        if len(packet) != expected or packet[:2] != HEADER:
            raise CoinFTProtocolError(f"single-framed24 packet must be {expected} bytes")
        sensors = _decode_sensor_payload(packet[2:], 1)
        return CoinFTPacket(None, None, sensors, packet, wire_format)

    if wire_format == "single-legacy24":
        if len(packet) != BYTES_PER_SENSOR:
            raise CoinFTProtocolError(f"single-legacy24 packet must be {BYTES_PER_SENSOR} bytes")
        sensors = _decode_sensor_payload(packet, 1)
        return CoinFTPacket(None, None, sensors, packet, wire_format)

    raise CoinFTProtocolError(f"unsupported wire format: {wire_format}")


class CoinFTStreamParser:
    """Read configured CoinFT packets from a pyserial-like object."""

    FORMATS = {"dual-v2", "single-v2", "single-framed24", "single-legacy24"}

    def __init__(self, wire_format: str = "dual-v2") -> None:
        if wire_format not in self.FORMATS:
            raise ValueError(f"unsupported CoinFT wire format: {wire_format}")
        self.wire_format = wire_format
        self.skipped_bytes = 0

    @property
    def sensor_count(self) -> int:
        return 2 if self.wire_format == "dual-v2" else 1

    def read_packet(self, serial_port: Any, timeout_s: float) -> CoinFTPacket:
        deadline = time.monotonic() + float(timeout_s)
        if self.wire_format == "single-legacy24":
            raw = _read_exact(serial_port, BYTES_PER_SENSOR, deadline)
            return decode_packet(raw, self.wire_format)

        skipped = _find_header(serial_port, deadline)
        self.skipped_bytes += skipped
        body_lengths = {
            "dual-v2": 8 + 2 * BYTES_PER_SENSOR,
            "single-v2": 8 + BYTES_PER_SENSOR,
            "single-framed24": BYTES_PER_SENSOR,
        }
        raw = HEADER + _read_exact(serial_port, body_lengths[self.wire_format], deadline)
        return decode_packet(raw, self.wire_format)


def sequence_gap(previous: int | None, current: int | None) -> tuple[int, bool]:
    """Return dropped packets and whether the sequence moved backwards."""

    if previous is None or current is None:
        return 0, False
    expected = (previous + 1) % UINT32_MOD
    if current == expected:
        return 0, False
    gap = (current - expected) % UINT32_MOD
    if gap < 1_000_000:
        return int(gap), False
    return 0, True
