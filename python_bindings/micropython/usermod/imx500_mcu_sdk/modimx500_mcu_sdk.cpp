#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/misc.h"
#include "py/mperrno.h"
}

#include "ArducamIMX500SDK.h"
#include "ai_driver.h"
#include "imx500_micropython_platform.h"
#include "peripherals_adapter.h"

static bool s_hardware_initialized = false;
static uint32_t s_i2c_baudrate = 100000;
static uint32_t s_spi_baudrate = 5000000;
static const char *s_last_driver_error = "";

static void imx500_hardware_init(uint32_t i2c_baudrate, uint32_t spi_baudrate, uint32_t settle_ms) {
    if (!imx500_micropython_platform_hardware_init(i2c_baudrate, spi_baudrate)) {
        s_last_driver_error = "platform hardware init failed";
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("IMX500 platform hardware init failed"));
    }

    s_i2c_baudrate = i2c_baudrate;
    s_spi_baudrate = spi_baudrate;
    s_hardware_initialized = true;

    if (settle_ms > 0) {
        imx500_micropython_platform_sleep_ms(settle_ms);
    }
}

static void imx500_ensure_hardware_initialized(void) {
    if (!s_hardware_initialized) {
        imx500_hardware_init(s_i2c_baudrate, s_spi_baudrate, 100);
    }
}

static bool object_is_none(mp_obj_t obj) {
    return obj == mp_const_none || obj == MP_OBJ_NULL;
}

static void optional_read_buffer(mp_obj_t obj, mp_buffer_info_t *bufinfo) {
    if (object_is_none(obj)) {
        bufinfo->buf = nullptr;
        bufinfo->len = 0;
        bufinfo->typecode = 0;
        return;
    }
    mp_get_buffer_raise(obj, bufinfo, MP_BUFFER_READ);
}

static uint32_t checked_buffer_len(size_t len, const char *name) {
    if (len > UINT32_MAX) {
        s_last_driver_error = name;
        mp_raise_ValueError(MP_ERROR_TEXT("buffer is too large"));
    }
    return (uint32_t)len;
}

static int micropython_i2c_write(uint32_t addr, uint32_t value, uint32_t size) {
    if (!g_i2c_driver.write) {
        s_last_driver_error = "I2C write driver is not registered";
        return -1;
    }
    return g_i2c_driver.write((uint16_t)addr, value, size);
}

static int micropython_i2c_read(uint32_t addr, uint32_t *value, uint32_t size) {
    if (!g_i2c_driver.read) {
        s_last_driver_error = "I2C read driver is not registered";
        return -1;
    }
    return g_i2c_driver.read((uint16_t)addr, value, size);
}

static int micropython_spi_write(uint8_t *data, uint32_t len) {
    if (!g_spi_driver.write) {
        s_last_driver_error = "SPI write driver is not registered";
        return -1;
    }
    return g_spi_driver.write(data, len);
}

static int micropython_spi_read(uint8_t *data, uint32_t len) {
    if (!g_spi_driver.read) {
        s_last_driver_error = "SPI read driver is not registered";
        return -1;
    }
    return g_spi_driver.read(data, len);
}

static mp_obj_t unsupported_on_micropython(const char *name) {
    s_last_driver_error = name;
    mp_raise_NotImplementedError(MP_ERROR_TEXT("callback driver registration is not used on MicroPython"));
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

    imx500_hardware_init((uint32_t)args[ARG_i2c_baudrate].u_int,
                         (uint32_t)args[ARG_spi_baudrate].u_int,
                         (uint32_t)args[ARG_settle_ms].u_int);

    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(imx500_sdk_init_obj, 0, imx500_sdk_init);

static mp_obj_t imx500_sdk_register_spi_driver(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return unsupported_on_micropython("register_spi_driver");
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(imx500_sdk_register_spi_driver_obj, 2, 2, imx500_sdk_register_spi_driver);

static mp_obj_t imx500_sdk_register_i2c_driver(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return unsupported_on_micropython("register_i2c_driver");
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(imx500_sdk_register_i2c_driver_obj, 2, 4, imx500_sdk_register_i2c_driver);

static mp_obj_t imx500_sdk_register_printf(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return unsupported_on_micropython("register_printf");
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(imx500_sdk_register_printf_obj, 0, 1, imx500_sdk_register_printf);

static mp_obj_t imx500_sdk_last_driver_error(void) {
    return mp_obj_new_str(s_last_driver_error, strlen(s_last_driver_error));
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_last_driver_error_obj, imx500_sdk_last_driver_error);

static mp_obj_t imx500_sdk_i2c_write(size_t n_args, const mp_obj_t *args) {
    imx500_ensure_hardware_initialized();
    uint32_t addr = (uint32_t)mp_obj_get_int(args[0]);
    uint32_t value = (uint32_t)mp_obj_get_int(args[1]);
    uint32_t size = n_args > 2 ? (uint32_t)mp_obj_get_int(args[2]) : 4;
    return mp_obj_new_int(micropython_i2c_write(addr, value, size));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(imx500_sdk_i2c_write_obj, 2, 3, imx500_sdk_i2c_write);

static mp_obj_t imx500_sdk_i2c_read(size_t n_args, const mp_obj_t *args) {
    imx500_ensure_hardware_initialized();
    uint32_t addr = (uint32_t)mp_obj_get_int(args[0]);
    uint32_t size = n_args > 1 ? (uint32_t)mp_obj_get_int(args[1]) : 4;
    uint32_t value = 0;
    int ret = micropython_i2c_read(addr, &value, size);
    mp_obj_t tuple[] = {
        mp_obj_new_int(ret),
        mp_obj_new_int_from_uint(value),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(tuple), tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(imx500_sdk_i2c_read_obj, 1, 2, imx500_sdk_i2c_read);

static mp_obj_t imx500_sdk_spi_write(mp_obj_t data_obj) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len == 0) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(micropython_spi_write((uint8_t *)bufinfo.buf, checked_buffer_len(bufinfo.len, "spi_write")));
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_spi_write_obj, imx500_sdk_spi_write);

static mp_obj_t imx500_sdk_spi_read(mp_obj_t size_obj) {
    imx500_ensure_hardware_initialized();
    mp_int_t size = mp_obj_get_int(size_obj);
    if (size <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size must be > 0"));
    }

    uint8_t *buf = m_new(uint8_t, (size_t)size);
    int ret = micropython_spi_read(buf, (uint32_t)size);
    mp_obj_t out = ret < 0 ? mp_obj_new_bytes((const byte *)"", 0) : mp_obj_new_bytes(buf, (size_t)ret);
    m_del(uint8_t, buf, (size_t)size);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_spi_read_obj, imx500_sdk_spi_read);

static mp_obj_t imx500_sdk_get_fw_ver(void) {
    imx500_ensure_hardware_initialized();
    uint32_t value = 0;
    get_fw_ver(&value);
    return mp_obj_new_int_from_uint(value);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_fw_ver_obj, imx500_sdk_get_fw_ver);

static mp_obj_t imx500_sdk_get_pid(void) {
    imx500_ensure_hardware_initialized();
    uint32_t value = 0;
    get_pid(&value);
    return mp_obj_new_int_from_uint(value);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_pid_obj, imx500_sdk_get_pid);

static mp_obj_t imx500_sdk_get_sensor_device_id(void) {
    imx500_ensure_hardware_initialized();
    char id[36] = {};
    int ret = get_sensor_device_id(id, sizeof(id));
    if (ret != 0) {
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("get_sensor_device_id failed: %d"), ret);
    }
    return mp_obj_new_str(id, strlen(id));
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_sensor_device_id_obj, imx500_sdk_get_sensor_device_id);

static mp_obj_t imx500_sdk_probe(void) {
    imx500_ensure_hardware_initialized();
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

static mp_obj_t imx500_sdk_open(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {
        ARG_model,
        ARG_network_info,
        ARG_mipi_format,
        ARG_spi_format,
        ARG_fps,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_model, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_network_info, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_mipi_format, MP_ARG_INT, {.u_int = MIPI_DATA_IMAGE} },
        { MP_QSTR_spi_format, MP_ARG_INT, {.u_int = SPI_METADATA_OUTPUT_TENSOR} },
        { MP_QSTR_fps, MP_ARG_INT, {.u_int = 30} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    imx500_ensure_hardware_initialized();

    mp_buffer_info_t model = {};
    mp_buffer_info_t network_info = {};
    optional_read_buffer(args[ARG_model].u_obj, &model);
    optional_read_buffer(args[ARG_network_info].u_obj, &network_info);

    bool ok = open((const uint8_t *)model.buf,
                   checked_buffer_len(model.len, "open model"),
                   (const uint8_t *)network_info.buf,
                   checked_buffer_len(network_info.len, "open network_info"),
                   (mipi_data_format_t)args[ARG_mipi_format].u_int,
                   (spi_data_format_t)args[ARG_spi_format].u_int,
                   (uint32_t)args[ARG_fps].u_int);
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(imx500_sdk_open_obj, 0, imx500_sdk_open);

static mp_obj_t imx500_sdk_load_imx500_fw(mp_obj_t data_obj, mp_obj_t fw_type_obj) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
    return mp_obj_new_int(load_imx500_fw((const uint8_t *)bufinfo.buf,
                                         checked_buffer_len(bufinfo.len, "load_imx500_fw"),
                                         (uint32_t)mp_obj_get_int(fw_type_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(imx500_sdk_load_imx500_fw_obj, imx500_sdk_load_imx500_fw);

static mp_obj_t imx500_sdk_stream_on(void) {
    imx500_ensure_hardware_initialized();
    stream_on();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_stream_on_obj, imx500_sdk_stream_on);

static mp_obj_t imx500_sdk_switch_spi_data_forward_mode(mp_obj_t mode_obj) {
    imx500_ensure_hardware_initialized();
    bool ok = switch_spi_data_forward_mode((spi_data_forwarding_mode_t)mp_obj_get_int(mode_obj));
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_switch_spi_data_forward_mode_obj, imx500_sdk_switch_spi_data_forward_mode);

static mp_obj_t imx500_sdk_get_metadata_size(void) {
    imx500_ensure_hardware_initialized();
    return mp_obj_new_int_from_uint(get_metadata_size());
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_metadata_size_obj, imx500_sdk_get_metadata_size);

static mp_obj_t imx500_sdk_read_metadata(mp_obj_t buffer_obj) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buffer_obj, &bufinfo, MP_BUFFER_WRITE);

    if (bufinfo.len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer length must be > 0"));
    }

    int32_t n = read_metadata((uint8_t *)bufinfo.buf, checked_buffer_len(bufinfo.len, "read_metadata"));
    return mp_obj_new_int(n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_read_metadata_obj, imx500_sdk_read_metadata);

static mp_obj_t imx500_sdk_get_spi_flash_status(void) {
    imx500_ensure_hardware_initialized();
    spi_flash_status_t status = {};
    bool ok = get_spi_flash_status(&status);

    mp_obj_t dict = mp_obj_new_dict(0);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_status), mp_obj_new_int_from_uint(status.status));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_result), mp_obj_new_int_from_uint(status.result));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bytes_done), mp_obj_new_int_from_uint(status.bytes_done));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bytes_total), mp_obj_new_int_from_uint(status.bytes_total));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ok), mp_obj_new_bool(ok));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_get_spi_flash_status_obj, imx500_sdk_get_spi_flash_status);

static mp_obj_t imx500_sdk_write_model_to_cam_flash(mp_obj_t model_obj) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(model_obj, &bufinfo, MP_BUFFER_READ);
    return mp_obj_new_bool(write_model_to_cam_flash((const uint8_t *)bufinfo.buf,
                                                    checked_buffer_len(bufinfo.len, "write_model_to_cam_flash")));
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_write_model_to_cam_flash_obj, imx500_sdk_write_model_to_cam_flash);

static mp_obj_t imx500_sdk_write_nn_info_to_cam_flash(mp_obj_t nn_info_obj) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(nn_info_obj, &bufinfo, MP_BUFFER_READ);
    return mp_obj_new_bool(write_nn_info_to_cam_flash((const uint8_t *)bufinfo.buf,
                                                      checked_buffer_len(bufinfo.len, "write_nn_info_to_cam_flash")));
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_write_nn_info_to_cam_flash_obj, imx500_sdk_write_nn_info_to_cam_flash);

static mp_obj_t imx500_sdk_load_nn_info_to_cam_memory(mp_obj_t nn_info_obj) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(nn_info_obj, &bufinfo, MP_BUFFER_READ);
    return mp_obj_new_bool(load_nn_info_to_cam_memory((const uint8_t *)bufinfo.buf,
                                                      checked_buffer_len(bufinfo.len, "load_nn_info_to_cam_memory")));
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_load_nn_info_to_cam_memory_obj, imx500_sdk_load_nn_info_to_cam_memory);

static mp_obj_t imx500_sdk_load_nn_info_to_sdk_cache(mp_obj_t nn_info_obj) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(nn_info_obj, &bufinfo, MP_BUFFER_READ);
    return mp_obj_new_int(load_nn_info_to_sdk_cache((const uint8_t *)bufinfo.buf, (size_t)bufinfo.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_load_nn_info_to_sdk_cache_obj, imx500_sdk_load_nn_info_to_sdk_cache);

static mp_obj_t imx500_sdk_dump_network_info_list(void) {
    dump_network_info_list();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_dump_network_info_list_obj, imx500_sdk_dump_network_info_list);

static mp_obj_t imx500_sdk_do_data_injection(size_t n_args, const mp_obj_t *args) {
    imx500_ensure_hardware_initialized();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    bool first_time = n_args > 1 ? mp_obj_is_true(args[1]) : false;
    do_data_injection((const uint8_t *)bufinfo.buf,
                      checked_buffer_len(bufinfo.len, "do_data_injection"),
                      first_time);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(imx500_sdk_do_data_injection_obj, 1, 2, imx500_sdk_do_data_injection);

static mp_obj_t imx500_sdk_stop_data_injection(void) {
    stop_data_injection();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_stop_data_injection_obj, imx500_sdk_stop_data_injection);

static mp_obj_t imx500_sdk_sensor_i2c_write_16_8(mp_obj_t addr_obj, mp_obj_t data_obj) {
    imx500_ensure_hardware_initialized();
    return mp_obj_new_int(sensor_i2c_write_16_8((uint16_t)mp_obj_get_int(addr_obj),
                                                (uint8_t)mp_obj_get_int(data_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(imx500_sdk_sensor_i2c_write_16_8_obj, imx500_sdk_sensor_i2c_write_16_8);

static mp_obj_t imx500_sdk_sensor_i2c_read_16_8(mp_obj_t addr_obj) {
    imx500_ensure_hardware_initialized();
    uint8_t value = 0;
    int ret = sensor_i2c_read_16_8((uint16_t)mp_obj_get_int(addr_obj), &value);
    mp_obj_t tuple[] = { mp_obj_new_int(ret), mp_obj_new_int_from_uint(value) };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(tuple), tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_sensor_i2c_read_16_8_obj, imx500_sdk_sensor_i2c_read_16_8);

static mp_obj_t imx500_sdk_sensor_i2c_write_16_16(mp_obj_t addr_obj, mp_obj_t data_obj) {
    imx500_ensure_hardware_initialized();
    return mp_obj_new_int(sensor_i2c_write_16_16((uint16_t)mp_obj_get_int(addr_obj),
                                                 (uint16_t)mp_obj_get_int(data_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(imx500_sdk_sensor_i2c_write_16_16_obj, imx500_sdk_sensor_i2c_write_16_16);

static mp_obj_t imx500_sdk_sensor_i2c_read_16_16(mp_obj_t addr_obj) {
    imx500_ensure_hardware_initialized();
    uint16_t value = 0;
    int ret = sensor_i2c_read_16_16((uint16_t)mp_obj_get_int(addr_obj), &value);
    mp_obj_t tuple[] = { mp_obj_new_int(ret), mp_obj_new_int_from_uint(value) };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(tuple), tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_sensor_i2c_read_16_16_obj, imx500_sdk_sensor_i2c_read_16_16);

static mp_obj_t imx500_sdk_sensor_i2c_write_16_32(mp_obj_t addr_obj, mp_obj_t data_obj) {
    imx500_ensure_hardware_initialized();
    return mp_obj_new_int(sensor_i2c_write_16_32((uint16_t)mp_obj_get_int(addr_obj),
                                                 (uint32_t)mp_obj_get_int(data_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(imx500_sdk_sensor_i2c_write_16_32_obj, imx500_sdk_sensor_i2c_write_16_32);

static mp_obj_t imx500_sdk_sensor_i2c_read_16_32(mp_obj_t addr_obj) {
    imx500_ensure_hardware_initialized();
    uint32_t value = 0;
    int ret = sensor_i2c_read_16_32((uint16_t)mp_obj_get_int(addr_obj), &value);
    mp_obj_t tuple[] = { mp_obj_new_int(ret), mp_obj_new_int_from_uint(value) };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(tuple), tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(imx500_sdk_sensor_i2c_read_16_32_obj, imx500_sdk_sensor_i2c_read_16_32);

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

static const mp_rom_map_elem_t imx500_spi_data_forwarding_mode_table[] = {
    { MP_ROM_QSTR(MP_QSTR_NONE), MP_ROM_INT(SPI_DATA_FORWARDING_NONE) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE_FROM_IMX500_MSPI), MP_ROM_INT(SPI_SLAVE_FROM_IMX500_MSPI) },
    { MP_ROM_QSTR(MP_QSTR_MASTER_FROM_IMX500_MSPI), MP_ROM_INT(SPI_MASTER_FROM_IMX500_MSPI) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE_FROM_IMX500_SSPI), MP_ROM_INT(SPI_SLAVE_FROM_IMX500_SSPI) },
    { MP_ROM_QSTR(MP_QSTR_MASTER_FROM_IMX500_SSPI), MP_ROM_INT(SPI_MASTER_FROM_IMX500_SSPI) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE_TO_IMX500_SSPI), MP_ROM_INT(SPI_SLAVE_TO_IMX500_SSPI) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE_WRITE_MODEL_TO_FLASH), MP_ROM_INT(SPI_SLAVE_WRITE_MODEL_TO_FLASH) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE_WRITE_NN_INFO_TO_FLASH), MP_ROM_INT(SPI_SLAVE_WRITE_NN_INFO_TO_FLASH) },
    { MP_ROM_QSTR(MP_QSTR_LOAD_NN_INFO_TO_MEMORY), MP_ROM_INT(SPI_LOAD_NN_INFO_TO_MEMORY) },
    { MP_ROM_QSTR(MP_QSTR_FORWARDING_MODE_SWITCHING), MP_ROM_INT(SPI_FORWORDING_MODE_SWITCHING) },
};
static MP_DEFINE_CONST_DICT(imx500_spi_data_forwarding_mode_globals, imx500_spi_data_forwarding_mode_table);

extern "C" const mp_obj_module_t imx500_spi_data_forwarding_mode_module = {
    { &mp_type_module },
    (mp_obj_dict_t *)&imx500_spi_data_forwarding_mode_globals,
};

static const mp_rom_map_elem_t imx500_spi_data_format_table[] = {
    { MP_ROM_QSTR(MP_QSTR_METADATA_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_INPUT_TENSOR), MP_ROM_INT(SPI_METADATA_INPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_JPEG_INPUT_TENSOR), MP_ROM_INT(SPI_METADATA_JPEG_INPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_NONE), MP_ROM_INT(SPI_METADATA_NONE) },
};
static MP_DEFINE_CONST_DICT(imx500_spi_data_format_globals, imx500_spi_data_format_table);

extern "C" const mp_obj_module_t imx500_spi_data_format_module = {
    { &mp_type_module },
    (mp_obj_dict_t *)&imx500_spi_data_format_globals,
};

static const mp_rom_map_elem_t imx500_mipi_data_format_table[] = {
    { MP_ROM_QSTR(MP_QSTR_IMAGE), MP_ROM_INT(MIPI_DATA_IMAGE) },
    { MP_ROM_QSTR(MP_QSTR_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR), MP_ROM_INT(MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR) },
    { MP_ROM_QSTR(MP_QSTR_NONE), MP_ROM_INT(MIPI_DATA_NONE) },
};
static MP_DEFINE_CONST_DICT(imx500_mipi_data_format_globals, imx500_mipi_data_format_table);

extern "C" const mp_obj_module_t imx500_mipi_data_format_module = {
    { &mp_type_module },
    (mp_obj_dict_t *)&imx500_mipi_data_format_globals,
};

static const mp_rom_map_elem_t imx500_sdk_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_imx500_mcu_sdk) },
    { MP_ROM_QSTR(MP_QSTR_IMX500_FW_TYPE_LOADER), MP_ROM_INT(IMX500_FW_TYPE_LOADER) },
    { MP_ROM_QSTR(MP_QSTR_IMX500_FW_TYPE_MAIN), MP_ROM_INT(IMX500_FW_TYPE_MAIN) },
    { MP_ROM_QSTR(MP_QSTR_IMX500_FW_TYPE_NETWORK_WEIGHTS), MP_ROM_INT(IMX500_FW_TYPE_NETWORK_WEIGHTS) },

    { MP_ROM_QSTR(MP_QSTR_SpiDataForwardingMode), MP_ROM_PTR(&imx500_spi_data_forwarding_mode_module) },
    { MP_ROM_QSTR(MP_QSTR_SpiDataFormat), MP_ROM_PTR(&imx500_spi_data_format_module) },
    { MP_ROM_QSTR(MP_QSTR_MipiDataFormat), MP_ROM_PTR(&imx500_mipi_data_format_module) },

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&imx500_sdk_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_spi_driver), MP_ROM_PTR(&imx500_sdk_register_spi_driver_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_i2c_driver), MP_ROM_PTR(&imx500_sdk_register_i2c_driver_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_printf), MP_ROM_PTR(&imx500_sdk_register_printf_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_driver_error), MP_ROM_PTR(&imx500_sdk_last_driver_error_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_write), MP_ROM_PTR(&imx500_sdk_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read), MP_ROM_PTR(&imx500_sdk_i2c_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_spi_write), MP_ROM_PTR(&imx500_sdk_spi_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_spi_read), MP_ROM_PTR(&imx500_sdk_spi_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_fw_ver), MP_ROM_PTR(&imx500_sdk_get_fw_ver_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_pid), MP_ROM_PTR(&imx500_sdk_get_pid_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_sensor_device_id), MP_ROM_PTR(&imx500_sdk_get_sensor_device_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_probe_imx500_module), MP_ROM_PTR(&imx500_sdk_probe_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&imx500_sdk_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_imx500_fw), MP_ROM_PTR(&imx500_sdk_load_imx500_fw_obj) },
    { MP_ROM_QSTR(MP_QSTR_stream_on), MP_ROM_PTR(&imx500_sdk_stream_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_switch_spi_data_forward_mode), MP_ROM_PTR(&imx500_sdk_switch_spi_data_forward_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_metadata_size), MP_ROM_PTR(&imx500_sdk_get_metadata_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_metadata), MP_ROM_PTR(&imx500_sdk_read_metadata_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_spi_flash_status), MP_ROM_PTR(&imx500_sdk_get_spi_flash_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_model_to_cam_flash), MP_ROM_PTR(&imx500_sdk_write_model_to_cam_flash_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_nn_info_to_cam_flash), MP_ROM_PTR(&imx500_sdk_write_nn_info_to_cam_flash_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_nn_info_to_cam_memory), MP_ROM_PTR(&imx500_sdk_load_nn_info_to_cam_memory_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_nn_info_to_sdk_cache), MP_ROM_PTR(&imx500_sdk_load_nn_info_to_sdk_cache_obj) },
    { MP_ROM_QSTR(MP_QSTR_dump_network_info_list), MP_ROM_PTR(&imx500_sdk_dump_network_info_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_do_data_injection), MP_ROM_PTR(&imx500_sdk_do_data_injection_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop_data_injection), MP_ROM_PTR(&imx500_sdk_stop_data_injection_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensor_i2c_write_16_8), MP_ROM_PTR(&imx500_sdk_sensor_i2c_write_16_8_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensor_i2c_read_16_8), MP_ROM_PTR(&imx500_sdk_sensor_i2c_read_16_8_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensor_i2c_write_16_16), MP_ROM_PTR(&imx500_sdk_sensor_i2c_write_16_16_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensor_i2c_read_16_16), MP_ROM_PTR(&imx500_sdk_sensor_i2c_read_16_16_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensor_i2c_write_16_32), MP_ROM_PTR(&imx500_sdk_sensor_i2c_write_16_32_obj) },
    { MP_ROM_QSTR(MP_QSTR_sensor_i2c_read_16_32), MP_ROM_PTR(&imx500_sdk_sensor_i2c_read_16_32_obj) },
    { MP_ROM_QSTR(MP_QSTR_parse_metadata), MP_ROM_PTR(&imx500_sdk_parse_metadata_obj) },

    { MP_ROM_QSTR(MP_QSTR_SPI_DATA_FORWARDING_NONE), MP_ROM_INT(SPI_DATA_FORWARDING_NONE) },
    { MP_ROM_QSTR(MP_QSTR_SPI_SLAVE_FROM_IMX500_MSPI), MP_ROM_INT(SPI_SLAVE_FROM_IMX500_MSPI) },
    { MP_ROM_QSTR(MP_QSTR_SPI_MASTER_FROM_IMX500_MSPI), MP_ROM_INT(SPI_MASTER_FROM_IMX500_MSPI) },
    { MP_ROM_QSTR(MP_QSTR_SPI_SLAVE_FROM_IMX500_SSPI), MP_ROM_INT(SPI_SLAVE_FROM_IMX500_SSPI) },
    { MP_ROM_QSTR(MP_QSTR_SPI_MASTER_FROM_IMX500_SSPI), MP_ROM_INT(SPI_MASTER_FROM_IMX500_SSPI) },
    { MP_ROM_QSTR(MP_QSTR_SPI_SLAVE_TO_IMX500_SSPI), MP_ROM_INT(SPI_SLAVE_TO_IMX500_SSPI) },
    { MP_ROM_QSTR(MP_QSTR_SPI_SLAVE_WRITE_MODEL_TO_FLASH), MP_ROM_INT(SPI_SLAVE_WRITE_MODEL_TO_FLASH) },
    { MP_ROM_QSTR(MP_QSTR_SPI_SLAVE_WRITE_NN_INFO_TO_FLASH), MP_ROM_INT(SPI_SLAVE_WRITE_NN_INFO_TO_FLASH) },
    { MP_ROM_QSTR(MP_QSTR_SPI_LOAD_NN_INFO_TO_MEMORY), MP_ROM_INT(SPI_LOAD_NN_INFO_TO_MEMORY) },
    { MP_ROM_QSTR(MP_QSTR_SPI_FORWORDING_MODE_SWITCHING), MP_ROM_INT(SPI_FORWORDING_MODE_SWITCHING) },
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
