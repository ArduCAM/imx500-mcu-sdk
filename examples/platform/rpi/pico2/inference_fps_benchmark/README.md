# IMX500 Pico 2 Inference FPS Benchmark

This firmware measures inference-frame delivery rate on Pico 2 without parsing,
forwarding, or saving metadata payloads. The model and `network_info` are loaded
from the IMX500 camera module Flash.

Only a 4 KiB scratch buffer is used. Each metadata payload is drained in chunks
while chip select remains asserted, and each chunk overwrites the same buffer.

## Defaults

- SPI clock: 20 MHz
- SPI metadata: JPEG/input tensor plus output tensor
- Requested sensor/inference rate: 30 FPS
- Warmup: 10 frames
- Warmup read retries: 8
- Measurement: 300 frames
- Model source: camera module Flash

The model and matching `network_info` must already be programmed into the
camera module before the benchmark starts.

## Build: JPEG/input tensor plus output tensor (default)

```bash
cmake -S . -B build-jpeg
cmake --build build-jpeg -j
```

Flash `build-jpeg/imx500_inference_fps_benchmark.uf2` to Pico 2 and open its
USB serial port. The firmware runs once and prints a benchmark summary.

## Build: output tensor only

```bash
cmake -S . -B build-output \
  -DBENCHMARK_SPI_FORMAT=OUTPUT_ONLY
cmake --build build-output -j
```

Supported values for `BENCHMARK_SPI_FORMAT` are:

- `OUTPUT_ONLY`: `SPI_METADATA_OUTPUT_TENSOR`
- `JPEG_INPUT_OUTPUT`: `SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR`

## Configuration

The main CMake cache options are:

| Option | Default | Meaning |
| --- | ---: | --- |
| `BENCHMARK_SPI_FORMAT` | `JPEG_INPUT_OUTPUT` | Metadata mode listed above |
| `BENCHMARK_SPI_BAUDRATE_HZ` | `20000000` | Requested Pico SPI clock |
| `BENCHMARK_SENSOR_FPS` | `30` | FPS passed to `imx500_open()` |
| `BENCHMARK_WARMUP_FRAMES` | `10` | Frames discarded before timing |
| `BENCHMARK_WARMUP_RETRIES` | `8` | Failed warmup reads tolerated before aborting |
| `BENCHMARK_MEASURE_FRAMES` | `300` | Frames included in the result |
| `BENCHMARK_MAX_METADATA_BYTES` | `2097152` | Safety limit for a reported payload |

Example with a shorter run:

```bash
cmake -S . -B build-short \
  -DBENCHMARK_MEASURE_FRAMES=100 \
  -DBENCHMARK_SPI_BAUDRATE_HZ=20000000
cmake --build build-short -j
```

## Reported Metrics

- `imx500_open()` and `stream_on()` duration
- First-frame latency
- Inference frame rate based on consecutive completed frames
- Overall measurement wall rate
- Average wait-plus-drain time
- Average/minimum/maximum metadata bytes per frame
- Effective SPI payload throughput

No JPEG, tensor, or raw metadata frame is retained after it has been drained.
