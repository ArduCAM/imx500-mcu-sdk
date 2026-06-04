#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/misc.h"
#include "py/mperrno.h"
}

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

#include "ArducamIMX500SDK.h"
#include "peripherals_adapter.h"
#include "g_config.h"

static void configure_spi_output_pin(uint pin) {
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
}

static void imx500_i2c_master_init(uint32_t baudrate) {
    i2c_init(I2C_HW_ADDR, baudrate);

    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

static void imx500_spi_master_init(uint32_t baudrate) {
    spi_init(SPI_HW_ADDR, baudrate);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(SPI_CSN_PIN);
    gpio_set_dir(SPI_CSN_PIN, GPIO_OUT);
    gpio_put(SPI_CSN_PIN, 1);

    configure_spi_output_pin(SPI_SCK_PIN);
    configure_spi_output_pin(SPI_TX_PIN);
    configure_spi_output_pin(SPI_CSN_PIN);

    spi_set_format(SPI_HW_ADDR, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

static mp_obj_t imx500_sdk_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {
        ARG_i2c_baudrate,
        ARG_spi_baudrate,
        ARG_settle_ms,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_i2c_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100000} },
        { MP_QSTR_spi_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 5000000} },
        { MP_QSTR_settle_ms, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    imx500_i2c_master_init((uint32_t)args[ARG_i2c_baudrate].u_int);
    imx500_spi_master_init((uint32_t)args[ARG_spi_baudrate].u_int);
    bind_peripherals_api();

    if (args[ARG_settle_ms].u_int > 0) {
        sleep_ms((uint32_t)args[ARG_settle_ms].u_int);
    }

    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(imx500_sdk_init_obj, 0, imx500_sdk_init);

static mp_obj_t imx500_sdk_get_fw_ver(void) {
    uint32_t value = 0;
    get_fw_ver(&value);
    return mp_obj_new_int_from_uint(value);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_fw_ver_obj, imx500_sdk_get_fw_ver);

static mp_obj_t imx500_sdk_get_pid(void) {
    uint32_t value = 0;
    get_pid(&value);
    return mp_obj_new_int_from_uint(value);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_pid_obj, imx500_sdk_get_pid);

static mp_obj_t imx500_sdk_probe(void) {
    uint32_t device_id = 0;
    uint32_t boot_status = 0;
    bool ok = probe_imx500_module(&device_id, &boot_status);

    mp_obj_t tuple[] = {
        mp_obj_new_bool(ok),
        mp_obj_new_int_from_uint(device_id),
        mp_obj_new_int_from_uint(boot_status),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(tuple), tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_probe_obj, imx500_sdk_probe);

static mp_obj_t imx500_sdk_open_flash(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {
        ARG_mipi_format,
        ARG_spi_format,
        ARG_fps,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mipi_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MIPI_DATA_IMAGE} },
        { MP_QSTR_spi_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = SPI_METADATA_OUTPUT_TENSOR} },
        { MP_QSTR_fps, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    bool ok = open(nullptr,
                   0,
                   nullptr,
                   0,
                   (mipi_data_format_t)args[ARG_mipi_format].u_int,
                   (spi_data_format_t)args[ARG_spi_format].u_int,
                   (uint32_t)args[ARG_fps].u_int);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(imx500_sdk_open_flash_obj, 0, imx500_sdk_open_flash);

static mp_obj_t imx500_sdk_stream_on(void) {
    stream_on();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_stream_on_obj, imx500_sdk_stream_on);

static mp_obj_t imx500_sdk_get_metadata_size(void) {
    return mp_obj_new_int_from_uint(get_metadata_size());
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_metadata_size_obj, imx500_sdk_get_metadata_size);

static mp_obj_t imx500_sdk_read_metadata_into(mp_obj_t buffer_obj) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buffer_obj, &bufinfo, MP_BUFFER_WRITE);

    if (bufinfo.len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer length must be > 0"));
    }

    int32_t n = read_metadata((uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len);
    return mp_obj_new_int(n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_read_metadata_into_obj, imx500_sdk_read_metadata_into);

static mp_obj_t imx500_sdk_read_metadata(mp_obj_t max_len_obj) {
    mp_int_t max_len = mp_obj_get_int(max_len_obj);
    if (max_len <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("max_len must be > 0"));
    }

    uint8_t *buf = m_new(uint8_t, (size_t)max_len);
    int32_t n = read_metadata(buf, (uint32_t)max_len);
    if (n <= 0) {
        m_del(uint8_t, buf, (size_t)max_len);
        return mp_const_none;
    }

    mp_obj_t out = mp_obj_new_bytes(buf, (size_t)n);
    m_del(uint8_t, buf, (size_t)max_len);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_read_metadata_obj, imx500_sdk_read_metadata);

static void dict_store_qstr(mp_obj_t dict, qstr key, mp_obj_t value) {
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(key), value);
}

static mp_obj_t make_header_dict(const IMX500OutputHeader *header) {
    mp_obj_t dict = mp_obj_new_dict(0);
    dict_store_qstr(dict, MP_QSTR_valid_flag, mp_obj_new_int_from_uint(header->valid_flag));
    dict_store_qstr(dict, MP_QSTR_frame_count, mp_obj_new_int_from_uint(header->frame_count));
    dict_store_qstr(dict, MP_QSTR_max_length_of_line, mp_obj_new_int_from_uint(header->max_length_of_line));
    dict_store_qstr(dict, MP_QSTR_size_of_ap_parameter, mp_obj_new_int_from_uint(header->size_of_ap_parameter));
    dict_store_qstr(dict, MP_QSTR_network_ordinal, mp_obj_new_int_from_uint(header->network_ordinal));
    dict_store_qstr(dict, MP_QSTR_indicator, mp_obj_new_int_from_uint(header->indicator));
    return dict;
}

static mp_obj_t make_tensor_dict(const IMX500ParsedTensor *tensor,
                                 const uint8_t *metadata_base,
                                 size_t preview_len) {
    mp_obj_t dict = mp_obj_new_dict(0);
    dict_store_qstr(dict, MP_QSTR_id, mp_obj_new_int_from_uint(tensor->id));
    dict_store_qstr(dict, MP_QSTR_name, mp_obj_new_str(tensor->name, strlen(tensor->name)));
    dict_store_qstr(dict, MP_QSTR_format, mp_obj_new_int_from_uint(tensor->format));
    dict_store_qstr(dict, MP_QSTR_bits_per_element, mp_obj_new_int_from_uint(tensor->bits_per_element));
    dict_store_qstr(dict, MP_QSTR_zero_point, mp_obj_new_int(tensor->zero_point));
    dict_store_qstr(dict, MP_QSTR_scale, mp_obj_new_float(tensor->scale));
    dict_store_qstr(dict, MP_QSTR_element_count, mp_obj_new_int_from_uint(tensor->element_count));
    dict_store_qstr(dict, MP_QSTR_data_bytes, mp_obj_new_int_from_uint(tensor->data_bytes));
    dict_store_qstr(dict, MP_QSTR_aligned_data_bytes, mp_obj_new_int_from_uint(tensor->aligned_data_bytes));

    uintptr_t data_offset = 0;
    if (tensor->data && metadata_base) {
        data_offset = (uintptr_t)(tensor->data - metadata_base);
    }
    dict_store_qstr(dict, MP_QSTR_data_offset, mp_obj_new_int_from_uint(data_offset));

    mp_obj_t dims = mp_obj_new_list(0, nullptr);
    for (uint8_t i = 0; i < tensor->dimension_count; ++i) {
        const IMX500TensorDimension *dim = &tensor->dimensions[i];
        mp_obj_t dim_dict = mp_obj_new_dict(0);
        dict_store_qstr(dim_dict, MP_QSTR_id, mp_obj_new_int_from_uint(dim->id));
        dict_store_qstr(dim_dict, MP_QSTR_size, mp_obj_new_int_from_uint(dim->size));
        dict_store_qstr(dim_dict, MP_QSTR_serialization_index, mp_obj_new_int_from_uint(dim->serialization_index));
        dict_store_qstr(dim_dict, MP_QSTR_padding, mp_obj_new_int_from_uint(dim->padding));
        mp_obj_list_append(dims, dim_dict);
    }
    dict_store_qstr(dict, MP_QSTR_dimensions, dims);

    if (tensor->data && tensor->data_bytes > 0 && preview_len > 0) {
        size_t n = tensor->data_bytes < preview_len ? tensor->data_bytes : preview_len;
        dict_store_qstr(dict, MP_QSTR_preview, mp_obj_new_bytes(tensor->data, n));
    } else {
        dict_store_qstr(dict, MP_QSTR_preview, mp_const_none);
    }

    return dict;
}

static mp_obj_t make_network_dict(const IMX500ParsedNetwork *network,
                                  const uint8_t *metadata_base,
                                  size_t preview_len) {
    mp_obj_t dict = mp_obj_new_dict(0);
    dict_store_qstr(dict, MP_QSTR_id, mp_obj_new_int_from_uint(network->id));
    dict_store_qstr(dict, MP_QSTR_name, mp_obj_new_str(network->name, strlen(network->name)));
    dict_store_qstr(dict, MP_QSTR_type, mp_obj_new_str(network->type, strlen(network->type)));

    mp_obj_t inputs = mp_obj_new_list(0, nullptr);
    for (uint8_t i = 0; i < network->input_tensor_count; ++i) {
        mp_obj_list_append(inputs, make_tensor_dict(&network->input_tensors[i], metadata_base, preview_len));
    }
    dict_store_qstr(dict, MP_QSTR_input_tensors, inputs);

    mp_obj_t outputs = mp_obj_new_list(0, nullptr);
    for (uint8_t i = 0; i < network->output_tensor_count; ++i) {
        mp_obj_list_append(outputs, make_tensor_dict(&network->output_tensors[i], metadata_base, preview_len));
    }
    dict_store_qstr(dict, MP_QSTR_output_tensors, outputs);

    return dict;
}

static mp_obj_t make_metadata_dict(const IMX500ParsedMetadata *parsed,
                                   const uint8_t *metadata_base,
                                   uint32_t metadata_len,
                                   size_t preview_len) {
    mp_obj_t dict = mp_obj_new_dict(0);
    dict_store_qstr(dict, MP_QSTR_raw_bytes, mp_obj_new_int_from_uint(metadata_len));
    dict_store_qstr(dict, MP_QSTR_has_primary_header, mp_obj_new_bool(parsed->has_primary_header));
    dict_store_qstr(dict, MP_QSTR_has_output_header, mp_obj_new_bool(parsed->has_output_header));
    dict_store_qstr(dict, MP_QSTR_ap_param_offset, mp_obj_new_int_from_uint(parsed->ap_param_offset));
    dict_store_qstr(dict, MP_QSTR_ap_param_size, mp_obj_new_int_from_uint(parsed->ap_param_size));
    dict_store_qstr(dict, MP_QSTR_ap_param_end_offset, mp_obj_new_int_from_uint(parsed->ap_param_end_offset));
    dict_store_qstr(dict, MP_QSTR_output_payload_offset, mp_obj_new_int_from_uint(parsed->output_payload_offset));
    dict_store_qstr(dict, MP_QSTR_output_payload_length, mp_obj_new_int_from_uint(parsed->output_payload_length));
    dict_store_qstr(dict, MP_QSTR_network_count, mp_obj_new_int_from_uint(parsed->network_count));
    dict_store_qstr(dict, MP_QSTR_selected_network_index, mp_obj_new_int_from_uint(parsed->selected_network_index));

    if (parsed->has_primary_header) {
        dict_store_qstr(dict, MP_QSTR_primary_header, make_header_dict(&parsed->primary_header));
    } else {
        dict_store_qstr(dict, MP_QSTR_primary_header, mp_const_none);
    }

    if (parsed->has_output_header) {
        dict_store_qstr(dict, MP_QSTR_output_header, make_header_dict(&parsed->output_header));
    } else {
        dict_store_qstr(dict, MP_QSTR_output_header, mp_const_none);
    }

    dict_store_qstr(dict, MP_QSTR_jpeg_size, mp_obj_new_int_from_uint(parsed->jpeg_size));
    dict_store_qstr(dict, MP_QSTR_jpeg_data_offset, mp_obj_new_int_from_uint(parsed->jpeg_data_offset));
    dict_store_qstr(dict, MP_QSTR_jpeg_data_len, mp_obj_new_int_from_uint(parsed->jpeg_data_len));

    mp_obj_t networks = mp_obj_new_list(0, nullptr);
    for (uint8_t i = 0; i < parsed->network_count; ++i) {
        mp_obj_list_append(networks, make_network_dict(&parsed->networks[i], metadata_base, preview_len));
    }
    dict_store_qstr(dict, MP_QSTR_networks, networks);
    return dict;
}

static mp_obj_t imx500_sdk_parse_metadata(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {
        ARG_buffer,
        ARG_length,
        ARG_spi_format,
        ARG_preview_len,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_buffer, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_length, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_spi_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = SPI_METADATA_OUTPUT_TENSOR} },
        { MP_QSTR_preview_len, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_buffer].u_obj, &bufinfo, MP_BUFFER_READ);

    size_t length = bufinfo.len;
    if (args[ARG_length].u_obj != mp_const_none) {
        mp_int_t requested = mp_obj_get_int(args[ARG_length].u_obj);
        if (requested < 0 || (size_t)requested > bufinfo.len) {
            mp_raise_ValueError(MP_ERROR_TEXT("length out of range"));
        }
        length = (size_t)requested;
    }
    if (length == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("metadata length must be > 0"));
    }

    mp_int_t preview_len_arg = args[ARG_preview_len].u_int;
    size_t preview_len = preview_len_arg > 0 ? (size_t)preview_len_arg : 0;

    IMX500ParsedMetadata parsed = {};
    bool ok = parse_metadata((const uint8_t *)bufinfo.buf,
                             (uint32_t)length,
                             (spi_data_format_t)args[ARG_spi_format].u_int,
                             &parsed);
    if (!ok) {
        return mp_const_none;
    }

    return make_metadata_dict(&parsed, (const uint8_t *)bufinfo.buf, (uint32_t)length, preview_len);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(imx500_sdk_parse_metadata_obj, 1, imx500_sdk_parse_metadata);

static const mp_rom_map_elem_t imx500_sdk_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_imx500_mcu_sdk) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&imx500_sdk_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_fw_ver), MP_ROM_PTR(&imx500_sdk_get_fw_ver_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_pid), MP_ROM_PTR(&imx500_sdk_get_pid_obj) },
    { MP_ROM_QSTR(MP_QSTR_probe), MP_ROM_PTR(&imx500_sdk_probe_obj) },
    { MP_ROM_QSTR(MP_QSTR_open_flash), MP_ROM_PTR(&imx500_sdk_open_flash_obj) },
    { MP_ROM_QSTR(MP_QSTR_stream_on), MP_ROM_PTR(&imx500_sdk_stream_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_metadata_size), MP_ROM_PTR(&imx500_sdk_get_metadata_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_metadata_into), MP_ROM_PTR(&imx500_sdk_read_metadata_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_metadata), MP_ROM_PTR(&imx500_sdk_read_metadata_obj) },
    { MP_ROM_QSTR(MP_QSTR_parse_metadata), MP_ROM_PTR(&imx500_sdk_parse_metadata_obj) },

    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_INPUT_TENSOR), MP_ROM_INT(SPI_METADATA_INPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_JPEG_INPUT_TENSOR), MP_ROM_INT(SPI_METADATA_JPEG_INPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_NONE), MP_ROM_INT(SPI_METADATA_NONE) },
    { MP_ROM_QSTR(MP_QSTR_MIPI_DATA_IMAGE), MP_ROM_INT(MIPI_DATA_IMAGE) },
    { MP_ROM_QSTR(MP_QSTR_MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_MIPI_DATA_NONE), MP_ROM_INT(MIPI_DATA_NONE) },
};

static MP_DEFINE_CONST_DICT(imx500_sdk_module_globals, imx500_sdk_module_globals_table);

extern "C" const mp_obj_module_t imx500_mcu_sdk_user_cmodule = {
    { &mp_type_module },
    (mp_obj_dict_t *)&imx500_sdk_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_imx500_mcu_sdk, imx500_mcu_sdk_user_cmodule);
