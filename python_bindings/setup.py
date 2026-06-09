import os
import re
from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup


ROOT = Path(__file__).resolve().parent
SDK_ROOT = ROOT.parent
GENERATED_INCLUDE_DIR = ROOT / "build" / "generated"
os.chdir(ROOT)


def generate_version_header() -> None:
    cmake_text = (SDK_ROOT / "imx500_mcu_sdk.cmake").read_text(encoding="utf-8")
    match = re.search(
        r"set\(\s*IMX500_MCU_SDK_VERSION_U32\s+([^) \t\r\n]+)",
        cmake_text,
    )
    if not match:
        raise RuntimeError("IMX500_MCU_SDK_VERSION_U32 not found in imx500_mcu_sdk.cmake")

    template = (SDK_ROOT / "version.h.in").read_text(encoding="utf-8")
    header = template.replace("@IMX500_MCU_SDK_VERSION_U32@", match.group(1))

    GENERATED_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)
    (GENERATED_INCLUDE_DIR / "version.h").write_text(header, encoding="utf-8")


generate_version_header()

ext_modules = [
    Pybind11Extension(
        "imx500_mcu_sdk._sdk",
        [
            "python/imx500_mcu_sdk/_binding.cpp",
            "python/imx500_mcu_sdk/ai_driver_py.cpp",
            "../ArducamIMX500SDK.cc",
        ],
        include_dirs=[
            str(SDK_ROOT),
            str(SDK_ROOT / "third_party" / "flatbuffers" / "include"),
            str(GENERATED_INCLUDE_DIR),
        ],
        cxx_std=17,
    ),
]


setup(
    cmdclass={"build_ext": build_ext},
    ext_modules=ext_modules,
)
