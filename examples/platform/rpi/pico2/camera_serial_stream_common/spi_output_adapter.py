from __future__ import annotations

from .metadata_parser import NNContext, ParsedFrame


def adapt_spi_jpeg_metadata_to_networks(parsed_frame: ParsedFrame) -> list[NNContext]:
    if not parsed_frame.networks:
        raise ValueError("Parsed frame does not contain any network metadata")
    return parsed_frame.networks
