#include <stdint.h>
#include <stdio.h>

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

    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_INPUT_TENSOR), MP_ROM_INT(SPI_METADATA_INPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_SPI_METADATA_NONE), MP_ROM_INT(SPI_METADATA_NONE) },
    { MP_ROM_QSTR(MP_QSTR_MIPI_DATA_IMAGE), MP_ROM_INT(MIPI_DATA_IMAGE) },
    { MP_ROM_QSTR(MP_QSTR_MIPI_DATA_NONE), MP_ROM_INT(MIPI_DATA_NONE) },
};

static MP_DEFINE_CONST_DICT(imx500_sdk_module_globals, imx500_sdk_module_globals_table);

extern "C" const mp_obj_module_t imx500_mcu_sdk_user_cmodule = {
    { &mp_type_module },
    (mp_obj_dict_t *)&imx500_sdk_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_imx500_mcu_sdk, imx500_mcu_sdk_user_cmodule);
