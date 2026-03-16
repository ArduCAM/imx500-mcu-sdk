from __future__ import annotations

from camera_serial_stream_common.spi_output_adapter import adapt_spi_jpeg_metadata_to_networks


def adapt_metadata_to_networks(parsed_frame):
    return adapt_spi_jpeg_metadata_to_networks(parsed_frame)
