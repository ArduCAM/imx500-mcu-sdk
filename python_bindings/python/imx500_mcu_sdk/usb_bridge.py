"""USB CDC transport backend for the IMX500 MCU SDK Python bindings."""

from __future__ import annotations

import re
import struct
import sys
import time
import zlib
from dataclasses import dataclass

import serial
from serial.tools import list_ports
from serial.tools.list_ports_common import ListPortInfo

from . import _sdk


MAGIC = 0x47524255
VERSION = 1
VID = 0x2ECA
PID = 0x5021
MAX_REQUEST_PAYLOAD = 4096

REQ = struct.Struct("<IHHIIIII")
RSP = struct.Struct("<IHHIIIIIII")

CMD_PING = 1
CMD_GET_STATUS = 2
CMD_I2C_READ = 10
CMD_I2C_WRITE = 11
CMD_SPI_GET_STATUS = 20
CMD_SPI_WRITE = 22
CMD_SPI_READ = 23

RESULT_OK = 1
WINDOWS_COM_PORT_RE = re.compile(r"COM(\d+)$", re.IGNORECASE)


class UsbBridgeError(RuntimeError):
    """Raised when the USB bridge returns an error or malformed response."""


@dataclass(frozen=True)
class BridgeResponse:
    result: int
    value0: int
    value1: int
    value2: int
    payload_size: int


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _windows_bridge_port_sort_key(port: ListPortInfo) -> tuple[int, int, str]:
    """Sort Windows COM ports numerically, with non-COM names last."""

    device = str(getattr(port, "device", "") or "")
    com_match = WINDOWS_COM_PORT_RE.fullmatch(device)
    if com_match:
        return 0, int(com_match.group(1)), device.casefold()
    return 1, sys.maxsize, device.casefold()


def find_bridge_port() -> str:
    """Return the CDC port used by the binary bridge."""

    matches = [
        port for port in list_ports.comports() if port.vid == VID and port.pid == PID
    ]
    if not matches:
        raise UsbBridgeError("No IMX500 USB CDC bridge port found")
    if sys.platform == "win32":
        return min(matches, key=_windows_bridge_port_sort_key).device
    matches.sort(key=lambda port: port.device)
    return matches[-1].device


class UsbBridgeTransport:
    """Host-side USB transport that implements the SDK I2C/SPI callbacks."""

    def __init__(
        self,
        port: str | None = None,
        *,
        baudrate: int = 115200,
        timeout: float = 60.0,
    ) -> None:
        self.port = port or find_bridge_port()
        self.seq = 1
        self.serial = serial.Serial(self.port, baudrate=baudrate, timeout=timeout)
        time.sleep(0.1)
        self.serial.reset_input_buffer()
        self.serial.reset_output_buffer()

    def close(self) -> None:
        self.serial.close()

    def register_with_sdk(self) -> None:
        """Register this USB transport as the active SDK I2C/SPI driver."""

        _sdk.register_i2c_driver(
            self.i2c_write,
            self.i2c_read,
            self.sleep_ms,
            self.sleep_us,
        )
        _sdk.register_spi_driver(self.spi_write, self.spi_read)

    def request(
        self,
        command: int,
        *,
        target: int = 0,
        arg0: int = 0,
        payload: bytes = b"",
    ) -> tuple[BridgeResponse, bytes]:
        seq = self.seq
        self.seq += 1
        payload = bytes(payload)
        header = REQ.pack(
            MAGIC,
            command,
            VERSION,
            seq,
            target & 0xFFFFFFFF,
            arg0 & 0xFFFFFFFF,
            len(payload),
            crc32(payload) if payload else 0,
        )
        self.serial.write(header + payload)

        raw = self.serial.read(RSP.size)
        if len(raw) != RSP.size:
            raise UsbBridgeError(
                f"response header timeout on {self.port}: "
                f"received {len(raw)}/{RSP.size} bytes. "
                "If the device exposes two serial ports, the selected port may "
                "be wrong; retry with --port COMx to select the other port explicitly."
            )

        (
            magic,
            rsp_command,
            version,
            rsp_seq,
            result,
            value0,
            value1,
            value2,
            payload_size,
            payload_crc,
        ) = RSP.unpack(raw)
        if (
            magic != MAGIC
            or rsp_command != command
            or version != VERSION
            or rsp_seq != seq
        ):
            raise UsbBridgeError(
                "response framing mismatch: "
                f"magic=0x{magic:08x} command={rsp_command} "
                f"version={version} seq={rsp_seq}"
            )

        data = self.serial.read(payload_size)
        if len(data) != payload_size:
            raise UsbBridgeError(f"payload timeout: {len(data)}/{payload_size}")
        if payload_size and crc32(data) != payload_crc:
            raise UsbBridgeError("response payload CRC mismatch")

        return BridgeResponse(result, value0, value1, value2, payload_size), data

    def ping(self) -> BridgeResponse:
        response, _ = self.request(CMD_PING)
        return response

    def status(self) -> BridgeResponse:
        response, _ = self.request(CMD_GET_STATUS)
        return response

    def _require_ok(self, response: BridgeResponse) -> None:
        if response.result != RESULT_OK:
            raise UsbBridgeError(f"bridge command failed: result={response.result}")

    def i2c_write(self, addr: int, value: int, size: int) -> int:
        payload = int(value & ((1 << (size * 8)) - 1)).to_bytes(size, "little")
        response, _ = self.request(
            CMD_I2C_WRITE,
            target=addr,
            arg0=size,
            payload=payload,
        )
        self._require_ok(response)
        return size

    def i2c_read(self, addr: int, size: int) -> tuple[int, int]:
        response, payload = self.request(CMD_I2C_READ, target=addr, arg0=size)
        self._require_ok(response)
        return size, int.from_bytes(payload, "little")

    def spi_write(self, data: bytes) -> int:
        payload = bytes(data)
        total = 0
        for offset in range(0, len(payload), MAX_REQUEST_PAYLOAD):
            chunk = payload[offset : offset + MAX_REQUEST_PAYLOAD]
            response, _ = self.request(CMD_SPI_WRITE, payload=chunk)
            self._require_ok(response)
            total += response.value0
        return total

    def spi_read(self, size: int) -> bytes:
        response, payload = self.request(CMD_SPI_READ, arg0=size)
        self._require_ok(response)
        return payload

    @staticmethod
    def sleep_ms(ms: int) -> None:
        time.sleep(ms / 1000.0)

    @staticmethod
    def sleep_us(us: int) -> None:
        time.sleep(us / 1_000_000.0)


def connect_usb_bridge(
    port: str | None = None,
    *,
    baudrate: int = 115200,
    timeout: float = 60.0,
    register: bool = True,
) -> UsbBridgeTransport:
    """Open the USB bridge and optionally register it with the SDK."""

    transport = UsbBridgeTransport(port, baudrate=baudrate, timeout=timeout)
    if register:
        transport.register_with_sdk()
    return transport
