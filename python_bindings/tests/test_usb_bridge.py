from __future__ import annotations

from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import Mock, patch

from imx500_mcu_sdk import usb_bridge


def make_port(
    device: str,
    *,
    vid: int = usb_bridge.VID,
    pid: int = usb_bridge.PID,
) -> SimpleNamespace:
    return SimpleNamespace(device=device, vid=vid, pid=pid)


class FindBridgePortTests(TestCase):
    def test_windows_prefers_lower_numeric_com_port(self) -> None:
        ports = [make_port("COM4"), make_port("COM3")]

        with patch.object(usb_bridge.sys, "platform", "win32"), patch.object(
            usb_bridge.list_ports, "comports", return_value=ports
        ):
            self.assertEqual(usb_bridge.find_bridge_port(), "COM3")

    def test_windows_sorts_com_port_numbers_naturally(self) -> None:
        ports = [make_port("COM10"), make_port("COM9")]

        with patch.object(usb_bridge.sys, "platform", "win32"), patch.object(
            usb_bridge.list_ports, "comports", return_value=ports
        ):
            self.assertEqual(usb_bridge.find_bridge_port(), "COM9")

    def test_non_windows_selection_is_unchanged(self) -> None:
        ports = [
            make_port("/dev/cu.usbmodem-camera1"),
            make_port("/dev/cu.usbmodem-camera2"),
        ]

        with patch.object(usb_bridge.sys, "platform", "darwin"), patch.object(
            usb_bridge.list_ports, "comports", return_value=ports
        ):
            self.assertEqual(
                usb_bridge.find_bridge_port(),
                "/dev/cu.usbmodem-camera2",
            )

    def test_ignores_ports_with_other_usb_ids(self) -> None:
        ports = [
            make_port("COM2", vid=0x1234, pid=0x5678),
            make_port("COM3"),
        ]

        with patch.object(usb_bridge.sys, "platform", "win32"), patch.object(
            usb_bridge.list_ports, "comports", return_value=ports
        ):
            self.assertEqual(usb_bridge.find_bridge_port(), "COM3")

    def test_reports_when_bridge_is_missing(self) -> None:
        with patch.object(usb_bridge.list_ports, "comports", return_value=[]):
            with self.assertRaisesRegex(
                usb_bridge.UsbBridgeError,
                "No IMX500 USB CDC bridge port found",
            ):
                usb_bridge.find_bridge_port()


class BridgeRequestTests(TestCase):
    def test_response_timeout_suggests_explicit_port(self) -> None:
        transport = object.__new__(usb_bridge.UsbBridgeTransport)
        transport.port = "COM4"
        transport.seq = 1
        transport.serial = SimpleNamespace(
            write=Mock(),
            read=Mock(return_value=b""),
        )

        with self.assertRaises(usb_bridge.UsbBridgeError) as raised:
            transport.request(usb_bridge.CMD_PING)

        message = str(raised.exception)
        self.assertIn("response header timeout on COM4", message)
        self.assertIn("--port COMx", message)
