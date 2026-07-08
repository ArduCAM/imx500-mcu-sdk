#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ArducamIMX500SDK.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace py = pybind11;

namespace {

std::string g_last_driver_error;

py::object& py_spi_write_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

py::object& py_spi_read_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

py::object& py_i2c_write_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

py::object& py_i2c_read_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

py::object& py_sleep_ms_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

py::object& py_sleep_us_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

py::object& py_printf_obj() {
  static py::object* obj = new py::object();
  return *obj;
}

bool missing_callback(const py::object& obj) {
  return !obj || obj.is_none();
}

void remember_callback_error(const py::error_already_set& error) {
  g_last_driver_error = error.what();
}

int py_spi_write_cb(uint8_t* data, uint32_t len) {
  py::gil_scoped_acquire gil;
  if (missing_callback(py_spi_write_obj())) {
    g_last_driver_error = "SPI write callback is not registered";
    return -1;
  }
  try {
    py::bytes payload(reinterpret_cast<const char*>(data), len);
    return py_spi_write_obj()(payload).cast<int>();
  } catch (const py::error_already_set& error) {
    remember_callback_error(error);
    return -1;
  }
}

int py_spi_read_cb(uint8_t* data, uint32_t len) {
  py::gil_scoped_acquire gil;
  if (missing_callback(py_spi_read_obj())) {
    g_last_driver_error = "SPI read callback is not registered";
    return -1;
  }
  try {
    py::object result = py_spi_read_obj()(len);
    std::string payload;
    if (py::isinstance<py::bytes>(result)) {
      payload = result.cast<std::string>();
    } else if (py::isinstance<py::bytearray>(result)) {
      py::bytes as_bytes(result);
      payload = as_bytes.cast<std::string>();
    } else {
      g_last_driver_error = "SPI read callback must return bytes";
      return -1;
    }
    if (payload.size() > len) {
      g_last_driver_error = "SPI read callback returned too many bytes";
      return -1;
    }
    if (!payload.empty()) {
      std::memcpy(data, payload.data(), payload.size());
    }
    return static_cast<int>(payload.size());
  } catch (const py::error_already_set& error) {
    remember_callback_error(error);
    return -1;
  }
}

int py_i2c_write_cb(uint16_t addr, uint32_t val, uint32_t size) {
  py::gil_scoped_acquire gil;
  if (missing_callback(py_i2c_write_obj())) {
    g_last_driver_error = "I2C write callback is not registered";
    return -1;
  }
  try {
    return py_i2c_write_obj()(addr, val, size).cast<int>();
  } catch (const py::error_already_set& error) {
    remember_callback_error(error);
    return -1;
  }
}

int py_i2c_read_cb(uint16_t addr, uint32_t* val, uint32_t size) {
  py::gil_scoped_acquire gil;
  if (missing_callback(py_i2c_read_obj())) {
    g_last_driver_error = "I2C read callback is not registered";
    return -1;
  }
  try {
    py::object result = py_i2c_read_obj()(addr, size);
    int ret = 0;
    uint32_t value = 0;
    if (py::isinstance<py::tuple>(result)) {
      py::tuple tuple = result.cast<py::tuple>();
      if (tuple.size() != 2) {
        g_last_driver_error = "I2C read callback tuple must be (ret, value)";
        return -1;
      }
      ret = tuple[0].cast<int>();
      value = tuple[1].cast<uint32_t>();
    } else {
      value = result.cast<uint32_t>();
    }
    if (ret >= 0 && val != nullptr) {
      *val = value;
    }
    return ret;
  } catch (const py::error_already_set& error) {
    remember_callback_error(error);
    return -1;
  }
}

void py_sleep_ms_cb(uint32_t ms) {
  py::gil_scoped_acquire gil;
  if (!missing_callback(py_sleep_ms_obj())) {
    try {
      py_sleep_ms_obj()(ms);
      return;
    } catch (const py::error_already_set& error) {
      remember_callback_error(error);
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void py_sleep_us_cb(uint64_t us) {
  py::gil_scoped_acquire gil;
  if (!missing_callback(py_sleep_us_obj())) {
    try {
      py_sleep_us_obj()(us);
      return;
    } catch (const py::error_already_set& error) {
      remember_callback_error(error);
    }
  }
  std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void py_printf_cb(const char* msg) {
  py::gil_scoped_acquire gil;
  if (missing_callback(py_printf_obj())) {
    return;
  }
  try {
    py_printf_obj()(msg ? msg : "");
  } catch (const py::error_already_set& error) {
    remember_callback_error(error);
  }
}

std::string bytes_from_object(const py::object& obj) {
  if (obj.is_none()) {
    return {};
  }
  if (py::isinstance<py::bytes>(obj)) {
    return obj.cast<std::string>();
  }
  if (py::isinstance<py::bytearray>(obj)) {
    py::bytes as_bytes(obj);
    return as_bytes.cast<std::string>();
  }
  throw py::type_error("expected bytes, bytearray, or None");
}

py::dict flash_status_to_dict(const spi_flash_status_t& status) {
  py::dict out;
  out["status"] = status.status;
  out["result"] = status.result;
  out["bytes_done"] = status.bytes_done;
  out["bytes_total"] = status.bytes_total;
  return out;
}

py::dict output_header_to_dict(const IMX500OutputHeader& header) {
  py::dict out;
  out["valid_flag"] = header.valid_flag;
  out["frame_count"] = header.frame_count;
  out["max_length_of_line"] = header.max_length_of_line;
  out["size_of_ap_parameter"] = header.size_of_ap_parameter;
  out["network_ordinal"] = header.network_ordinal;
  out["indicator"] = header.indicator;
  return out;
}

py::dict tensor_dimension_to_dict(const IMX500TensorDimension& dimension) {
  py::dict out;
  out["id"] = dimension.id;
  out["size"] = dimension.size;
  out["serialization_index"] = dimension.serialization_index;
  out["padding"] = dimension.padding;
  return out;
}

py::dict parsed_tensor_to_dict(const IMX500ParsedTensor& tensor,
                               const uint8_t* metadata_base,
                               size_t preview_len) {
  py::dict out;
  out["id"] = tensor.id;
  out["name"] = tensor.name;
  out["format"] = tensor.format;
  out["bits_per_element"] = tensor.bits_per_element;
  out["zero_point"] = tensor.zero_point;
  out["scale"] = tensor.scale;
  out["element_count"] = tensor.element_count;
  out["data_bytes"] = tensor.data_bytes;
  out["aligned_data_bytes"] = tensor.aligned_data_bytes;

  uintptr_t data_offset = 0;
  if (tensor.data != nullptr && metadata_base != nullptr) {
    data_offset = static_cast<uintptr_t>(tensor.data - metadata_base);
  }
  out["data_offset"] = data_offset;

  py::list dimensions;
  for (uint8_t i = 0; i < tensor.dimension_count; ++i) {
    dimensions.append(tensor_dimension_to_dict(tensor.dimensions[i]));
  }
  out["dimensions"] = std::move(dimensions);

  if (tensor.data != nullptr && tensor.data_bytes > 0 && preview_len > 0) {
    size_t n = std::min<size_t>(tensor.data_bytes, preview_len);
    out["preview"] = py::bytes(reinterpret_cast<const char*>(tensor.data), n);
  } else {
    out["preview"] = py::none();
  }

  return out;
}

py::dict parsed_network_to_dict(const IMX500ParsedNetwork& network,
                                const uint8_t* metadata_base,
                                size_t preview_len) {
  py::dict out;
  out["id"] = network.id;
  out["name"] = network.name;
  out["type"] = network.type;

  py::list inputs;
  for (uint8_t i = 0; i < network.input_tensor_count; ++i) {
    inputs.append(parsed_tensor_to_dict(network.input_tensors[i],
                                        metadata_base,
                                        preview_len));
  }
  out["input_tensors"] = std::move(inputs);

  py::list outputs;
  for (uint8_t i = 0; i < network.output_tensor_count; ++i) {
    outputs.append(parsed_tensor_to_dict(network.output_tensors[i],
                                         metadata_base,
                                         preview_len));
  }
  out["output_tensors"] = std::move(outputs);
  return out;
}

py::dict parsed_metadata_to_dict(const IMX500ParsedMetadata& parsed,
                                 const uint8_t* metadata_base,
                                 uint32_t metadata_len,
                                 size_t preview_len) {
  py::dict out;
  out["raw_bytes"] = metadata_len;
  out["has_primary_header"] = parsed.has_primary_header;
  out["has_output_header"] = parsed.has_output_header;
  out["ap_param_offset"] = parsed.ap_param_offset;
  out["ap_param_size"] = parsed.ap_param_size;
  out["ap_param_end_offset"] = parsed.ap_param_end_offset;
  out["output_payload_offset"] = parsed.output_payload_offset;
  out["output_payload_length"] = parsed.output_payload_length;
  out["network_count"] = parsed.network_count;
  out["selected_network_index"] = parsed.selected_network_index;

  out["primary_header"] = parsed.has_primary_header
                              ? py::object(output_header_to_dict(parsed.primary_header))
                              : py::none();
  out["output_header"] = parsed.has_output_header
                             ? py::object(output_header_to_dict(parsed.output_header))
                             : py::none();

  out["jpeg_size"] = parsed.jpeg_size;
  out["jpeg_data_offset"] = parsed.jpeg_data_offset;
  out["jpeg_data_len"] = parsed.jpeg_data_len;

  py::list networks;
  for (uint8_t i = 0; i < parsed.network_count; ++i) {
    networks.append(parsed_network_to_dict(parsed.networks[i],
                                           metadata_base,
                                           preview_len));
  }
  out["networks"] = std::move(networks);
  return out;
}

void bind_driver_registration(py::module_& m) {
  m.def("register_spi_driver",
        [](py::object write, py::object read) {
          py_spi_write_obj() = std::move(write);
          py_spi_read_obj() = std::move(read);
          spi_driver driver = {};
          driver.write = py_spi_write_cb;
          driver.read = py_spi_read_cb;
          ::register_spi_driver(driver);
        },
        py::arg("write"),
        py::arg("read"));

  m.def("register_i2c_driver",
        [](py::object write,
           py::object read,
           py::object sleep_ms,
           py::object sleep_us) {
          py_i2c_write_obj() = std::move(write);
          py_i2c_read_obj() = std::move(read);
          py_sleep_ms_obj() = std::move(sleep_ms);
          py_sleep_us_obj() = std::move(sleep_us);
          i2c_driver driver = {};
          driver.write = py_i2c_write_cb;
          driver.read = py_i2c_read_cb;
          driver.slp_ms = py_sleep_ms_cb;
          driver.slp_us = py_sleep_us_cb;
          ::register_i2c_driver(driver);
        },
        py::arg("write"),
        py::arg("read"),
        py::arg("sleep_ms") = py::none(),
        py::arg("sleep_us") = py::none());

  m.def("register_printf",
        [](py::object fn) {
          py_printf_obj() = std::move(fn);
          ::register_printf(missing_callback(py_printf_obj()) ? nullptr
                                                          : py_printf_cb);
        },
        py::arg("fn") = py::none());

  m.def("last_driver_error", []() { return g_last_driver_error; });
}

void bind_sdk_functions(py::module_& m) {
  m.def("i2c_write",
        [](uint32_t addr, uint32_t value, uint32_t size) {
          return _i2c_write(addr, value, size);
        },
        py::arg("addr"),
        py::arg("value"),
        py::arg("size") = 4);

  m.def("i2c_read",
        [](uint32_t addr, uint32_t size) {
          uint32_t value = 0;
          int ret = _i2c_read(addr, &value, size);
          return py::make_tuple(ret, value);
        },
        py::arg("addr"),
        py::arg("size") = 4);

  m.def("spi_write",
        [](py::object data) {
          std::string payload = bytes_from_object(data);
          if (payload.empty()) {
            return -1;
          }
          return _spi_write(reinterpret_cast<uint8_t*>(payload.data()),
                            static_cast<uint32_t>(payload.size()));
        },
        py::arg("data"));

  m.def("spi_read",
        [](uint32_t size) {
          std::vector<uint8_t> buf(size);
          int ret = _spi_read(buf.data(), size);
          if (ret < 0) {
            return py::bytes();
          }
          return py::bytes(reinterpret_cast<const char*>(buf.data()), ret);
        },
        py::arg("size"));

  m.def("probe_imx500_module", []() {
    uint32_t device_id = 0;
    uint32_t boot_status = 0;
    bool ok = ::probe_imx500_module(&device_id, &boot_status);
    return py::make_tuple(ok, device_id, boot_status);
  });

  m.def("get_fw_ver", []() {
    uint32_t value = 0;
    ::get_fw_ver(&value);
    return value;
  });

  m.def("get_pid", []() {
    uint32_t value = 0;
    ::get_pid(&value);
    return value;
  });

  m.def("imx500_open",
        [](py::object model,
           py::object network_info,
           mipi_data_format_t mipi_format,
           spi_data_format_t spi_format,
           uint32_t fps) {
          std::string model_bytes = bytes_from_object(model);
          std::string nn_info_bytes = bytes_from_object(network_info);
          const uint8_t* model_ptr = model_bytes.empty()
                                         ? nullptr
                                         : reinterpret_cast<const uint8_t*>(
                                               model_bytes.data());
          const uint8_t* nn_info_ptr = nn_info_bytes.empty()
                                           ? nullptr
                                           : reinterpret_cast<const uint8_t*>(
                                                 nn_info_bytes.data());
          return ::imx500_open(model_ptr,
                        static_cast<uint32_t>(model_bytes.size()),
                        nn_info_ptr,
                        static_cast<uint32_t>(nn_info_bytes.size()),
                        mipi_format,
                        spi_format,
                        fps);
        },
        py::arg("model") = py::none(),
        py::arg("network_info") = py::none(),
        py::arg("mipi_format") = MIPI_DATA_IMAGE,
        py::arg("spi_format") = SPI_METADATA_OUTPUT_TENSOR,
        py::arg("fps") = 30);

  m.def("load_imx500_fw",
        [](py::object data, uint32_t fw_type) {
          std::string payload = bytes_from_object(data);
          return ::load_imx500_fw(
              reinterpret_cast<const uint8_t*>(payload.data()),
              static_cast<uint32_t>(payload.size()),
              fw_type);
        },
        py::arg("data"),
        py::arg("fw_type"));

  m.def("stream_on", &::stream_on);
  m.def("switch_spi_data_forward_mode",
        [](spi_data_forwarding_mode_t mode) {
          return ::switch_spi_data_forward_mode(mode);
        },
        py::arg("mode"));
  m.def("get_metadata_size", &::get_metadata_size);

  m.def("read_metadata",
        [](py::buffer buffer) {
          py::buffer_info info = buffer.request();
          if (info.readonly) {
            throw py::value_error("read_metadata buffer must be writable");
          }
          if (info.ptr == nullptr || info.size <= 0 || info.itemsize <= 0) {
            throw py::value_error("read_metadata buffer length must be > 0");
          }
          if (info.ndim != 1 || info.strides.empty() ||
              info.strides[0] != info.itemsize) {
            throw py::value_error("read_metadata buffer must be one-dimensional and contiguous");
          }

          size_t capacity = static_cast<size_t>(info.size) *
                            static_cast<size_t>(info.itemsize);
          if (capacity > UINT32_MAX) {
            throw py::value_error("read_metadata buffer is too large");
          }

          return ::read_metadata(reinterpret_cast<uint8_t*>(info.ptr),
                                 static_cast<uint32_t>(capacity));
        },
        py::arg("buffer"));

  m.def("parse_metadata",
        [](py::buffer buffer,
           py::object length,
           spi_data_format_t spi_format,
           size_t preview_len) -> py::object {
          py::buffer_info info = buffer.request();
          if (info.ptr == nullptr || info.size <= 0 || info.itemsize <= 0) {
            throw py::value_error("metadata buffer length must be > 0");
          }
          if (info.ndim != 1 || info.strides.empty() ||
              info.strides[0] != info.itemsize) {
            throw py::value_error("metadata buffer must be one-dimensional and contiguous");
          }

          size_t capacity = static_cast<size_t>(info.size) *
                            static_cast<size_t>(info.itemsize);
          size_t metadata_len = capacity;
          if (!length.is_none()) {
            metadata_len = length.cast<size_t>();
            if (metadata_len > capacity) {
              throw py::value_error("length is larger than the metadata buffer");
            }
          }
          if (metadata_len == 0) {
            throw py::value_error("metadata length must be > 0");
          }
          if (metadata_len > UINT32_MAX) {
            throw py::value_error("metadata buffer is too large");
          }

          IMX500ParsedMetadata parsed = {};
          const auto* metadata_base = reinterpret_cast<const uint8_t*>(info.ptr);
          bool ok = ::parse_metadata(metadata_base,
                                     static_cast<uint32_t>(metadata_len),
                                     spi_format,
                                     &parsed);
          if (!ok) {
            return py::none();
          }
          return parsed_metadata_to_dict(parsed,
                                         metadata_base,
                                         static_cast<uint32_t>(metadata_len),
                                         preview_len);
        },
        py::arg("buffer"),
        py::arg("length") = py::none(),
        py::arg("spi_format") = SPI_METADATA_OUTPUT_TENSOR,
        py::arg("preview_len") = 16);

  m.def("get_spi_flash_status", []() {
    spi_flash_status_t status = {};
    bool ok = ::get_spi_flash_status(&status);
    py::dict out = flash_status_to_dict(status);
    out["ok"] = ok;
    return out;
  });

  m.def("write_model_to_cam_flash",
        [](py::object model) {
          std::string payload = bytes_from_object(model);
          return ::write_model_to_cam_flash(
              reinterpret_cast<const uint8_t*>(payload.data()),
              static_cast<uint32_t>(payload.size()));
        },
        py::arg("model"));

  m.def("write_nn_info_to_cam_flash",
        [](py::object nn_info) {
          std::string payload = bytes_from_object(nn_info);
          return ::write_nn_info_to_cam_flash(
              reinterpret_cast<const uint8_t*>(payload.data()),
              static_cast<uint32_t>(payload.size()));
        },
        py::arg("network_info"));

  m.def("load_nn_info_to_cam_memory",
        [](py::object nn_info) {
          std::string payload = bytes_from_object(nn_info);
          return ::load_nn_info_to_cam_memory(
              reinterpret_cast<const uint8_t*>(payload.data()),
              static_cast<uint32_t>(payload.size()));
        },
        py::arg("network_info"));

  m.def("load_nn_info_to_sdk_cache",
        [](py::object nn_info) {
          std::string payload = bytes_from_object(nn_info);
          return ::load_nn_info_to_sdk_cache(
              reinterpret_cast<const uint8_t*>(payload.data()),
              payload.size());
        },
        py::arg("network_info"));

  m.def("dump_network_info_list", &::dump_network_info_list);

  m.def("do_data_injection",
        [](py::object data, bool first_time) {
          std::string payload = bytes_from_object(data);
          ::do_data_injection(
              reinterpret_cast<const uint8_t*>(payload.data()),
              static_cast<uint32_t>(payload.size()),
              first_time);
        },
        py::arg("data"),
        py::arg("first_time") = false);

  m.def("stop_data_injection", &::stop_data_injection);

  m.def("sensor_i2c_write_16_8", &::sensor_i2c_write_16_8);
  m.def("sensor_i2c_read_16_8", [](uint16_t addr) {
    uint8_t value = 0;
    int ret = ::sensor_i2c_read_16_8(addr, &value);
    return py::make_tuple(ret, value);
  });
  m.def("sensor_i2c_write_16_16", &::sensor_i2c_write_16_16);
  m.def("sensor_i2c_read_16_16", [](uint16_t addr) {
    uint16_t value = 0;
    int ret = ::sensor_i2c_read_16_16(addr, &value);
    return py::make_tuple(ret, value);
  });
  m.def("sensor_i2c_write_16_32", &::sensor_i2c_write_16_32);
  m.def("sensor_i2c_read_16_32", [](uint16_t addr) {
    uint32_t value = 0;
    int ret = ::sensor_i2c_read_16_32(addr, &value);
    return py::make_tuple(ret, value);
  });
}

void bind_constants(py::module_& m) {
  m.attr("IMX500_FW_TYPE_LOADER") = IMX500_FW_TYPE_LOADER;
  m.attr("IMX500_FW_TYPE_MAIN") = IMX500_FW_TYPE_MAIN;
  m.attr("IMX500_FW_TYPE_NETWORK_WEIGHTS") = IMX500_FW_TYPE_NETWORK_WEIGHTS;

  py::enum_<spi_data_forwarding_mode_t>(m, "SpiDataForwardingMode")
      .value("NONE", SPI_DATA_FORWARDING_NONE)
      .value("SLAVE_FROM_IMX500_MSPI", SPI_SLAVE_FROM_IMX500_MSPI)
      .value("MASTER_FROM_IMX500_MSPI", SPI_MASTER_FROM_IMX500_MSPI)
      .value("SLAVE_FROM_IMX500_SSPI", SPI_SLAVE_FROM_IMX500_SSPI)
      .value("MASTER_FROM_IMX500_SSPI", SPI_MASTER_FROM_IMX500_SSPI)
      .value("SLAVE_TO_IMX500_SSPI", SPI_SLAVE_TO_IMX500_SSPI)
      .value("SLAVE_WRITE_MODEL_TO_FLASH", SPI_SLAVE_WRITE_MODEL_TO_FLASH)
      .value("SLAVE_WRITE_NN_INFO_TO_FLASH", SPI_SLAVE_WRITE_NN_INFO_TO_FLASH)
      .value("LOAD_NN_INFO_TO_MEMORY", SPI_LOAD_NN_INFO_TO_MEMORY)
      .value("FORWARDING_MODE_SWITCHING", SPI_FORWORDING_MODE_SWITCHING);

  py::enum_<spi_data_format_t>(m, "SpiDataFormat")
      .value("METADATA_OUTPUT_TENSOR", SPI_METADATA_OUTPUT_TENSOR)
      .value("METADATA_INPUT_TENSOR", SPI_METADATA_INPUT_TENSOR)
      .value("METADATA_JPEG_INPUT_TENSOR", SPI_METADATA_JPEG_INPUT_TENSOR)
      .value("METADATA_INPUT_TENSOR_OUTPUT_TENSOR",
             SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR)
      .value("METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR",
             SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR)
      .value("METADATA_NONE", SPI_METADATA_NONE);

  py::enum_<mipi_data_format_t>(m, "MipiDataFormat")
      .value("IMAGE", MIPI_DATA_IMAGE)
      .value("METADATA_INPUT_TENSOR_OUTPUT_TENSOR",
             MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR)
      .value("IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR",
             MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR)
      .value("NONE", MIPI_DATA_NONE);

  m.attr("SPI_DATA_FORWARDING_NONE") = SPI_DATA_FORWARDING_NONE;
  m.attr("SPI_SLAVE_TO_IMX500_SSPI") = SPI_SLAVE_TO_IMX500_SSPI;
  m.attr("SPI_SLAVE_WRITE_MODEL_TO_FLASH") = SPI_SLAVE_WRITE_MODEL_TO_FLASH;
  m.attr("SPI_SLAVE_WRITE_NN_INFO_TO_FLASH") =
      SPI_SLAVE_WRITE_NN_INFO_TO_FLASH;
  m.attr("SPI_LOAD_NN_INFO_TO_MEMORY") = SPI_LOAD_NN_INFO_TO_MEMORY;
  m.attr("SPI_METADATA_OUTPUT_TENSOR") = SPI_METADATA_OUTPUT_TENSOR;
  m.attr("SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR") =
      SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR;
  m.attr("MIPI_DATA_IMAGE") = MIPI_DATA_IMAGE;
  m.attr("MIPI_DATA_NONE") = MIPI_DATA_NONE;
}

}  // namespace

PYBIND11_MODULE(_sdk, m) {
  m.doc() = "pybind11 bindings for the Arducam IMX500 MCU SDK";
  bind_constants(m);
  bind_driver_registration(m);
  bind_sdk_functions(m);
}
