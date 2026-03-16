from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ModelInfo:
    model_dir: Path
    values: dict[str, str]

    @property
    def name(self) -> str:
        return self.model_dir.name

    @property
    def input_width(self) -> int:
        return int(self.values["inputTensorWidth"])

    @property
    def input_height(self) -> int:
        return int(self.values["inputTensorHeight"])

    @property
    def ap_param_size(self) -> int:
        return int(self.values["apParamSize"])

    @property
    def output_tensor_count(self) -> int:
        return int(self.values["outputTensorNum"])

    @property
    def output_bytes(self) -> int:
        total = 0
        for idx in range(1, self.output_tensor_count + 1):
            total += int(self.values[f"outputTensorDimSize{idx}"]) * int(
                self.values[f"outputTensorBytesPerElement{idx}"]
            )
        return total

    def estimate_payload_limit(self, jpeg_budget_bytes: int) -> int:
        jpeg_budget_bytes = max(4096, _align_up(jpeg_budget_bytes, 1024))
        return 12 + self.ap_param_size + jpeg_budget_bytes + 12 + self.output_bytes


def _align_up(value: int, base: int) -> int:
    return ((value + base - 1) // base) * base


def load_model_info(model_dir: Path) -> ModelInfo:
    info_path = model_dir / "network_info.txt"
    values: dict[str, str] = {}
    for line in info_path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return ModelInfo(model_dir=model_dir, values=values)
