#include <stdint.h>

extern "C" {
#include "py/obj.h"
#include "py/runtime.h"
}

static mp_obj_t imx500_sdk_ping(void) {
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_ping_obj, imx500_sdk_ping);

static mp_obj_t imx500_sdk_build_mode(void) {
    return MP_OBJ_NEW_QSTR(MP_QSTR_minimal);
}
static MP_DEFINE_CONST_FUN_OBJ_0(imx500_sdk_build_mode_obj, imx500_sdk_build_mode);

static const mp_rom_map_elem_t imx500_sdk_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_imx500_mcu_sdk) },
    { MP_ROM_QSTR(MP_QSTR_ping), MP_ROM_PTR(&imx500_sdk_ping_obj) },
    { MP_ROM_QSTR(MP_QSTR_build_mode), MP_ROM_PTR(&imx500_sdk_build_mode_obj) },
};

static MP_DEFINE_CONST_DICT(imx500_sdk_module_globals, imx500_sdk_module_globals_table);

extern "C" const mp_obj_module_t imx500_mcu_sdk_user_cmodule = {
    { &mp_type_module },
    (mp_obj_dict_t *)&imx500_sdk_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_imx500_mcu_sdk, imx500_mcu_sdk_user_cmodule);
