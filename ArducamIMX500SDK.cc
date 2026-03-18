#include "ArducamIMX500SDK.h"
#include "ArducamIMX500SDK.h"
#include <algorithm>
#include <vector>
#include <string>
#include "stdio.h"
#include "string.h"

#define ALIGN_DOWN(size, align) ((size) & ~((align) - 1))
#define ALIGN_UP(size, align)   (ALIGN_DOWN((size) + (align) - 1, (align)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef enum {
    IMX500_CMD_OK = 0,
    IMX500_CMD_ERR_INVALID_ARG = -1,
    IMX500_CMD_ERR_I2C_WRITE   = -2,
    IMX500_CMD_ERR_I2C_READ    = -3,
    IMX500_CMD_ERR_TIMEOUT    = -4,
    IMX500_CMD_ERR_RUNNING_FAILED = -5,
    IMX500_CMD_ERR_NOT_EFFECTIVE = -6,
} imx500_err_t;

static const uint32_t IMX500_MAX_BUFFER = (1u << 20);
static const uint32_t MAX_SPI_PACKET_LEN = 4096;
static const uint32_t SPI_BRIDGE_BLOCK_LEN = 256;
static const uint32_t SPI_BRIDGE_BLOCK_GAP_US = 100;
static const uint32_t SPI_FW_BRIDGE_BLOCK_GAP_US = 1000;
static const uint32_t SPI_FLASH_POLL_INTERVAL_MS = 50;
static const uint32_t SPI_FLASH_WAIT_IDLE_TIMEOUT_MS = 5000;
static const uint32_t SPI_FLASH_TRANSFER_BASE_TIMEOUT_MS = 10000;
static const uint32_t SPI_FLASH_TRANSFER_PER_KB_TIMEOUT_MS = 4;
static const uint32_t SPI_FLASH_FINALIZE_TIMEOUT_MS = 30000;
static const uint32_t SPI_BLOB_HEADER_MAGIC = 0x424C4253u;
static const uint32_t SPI_FLASH_CHUNK_LEN = 4096;
static const uint32_t SPI_FLASH_HEADER_GAP_MS = 10;
static const uint32_t SPI_FLASH_CHUNK_GAP_MS = 20;

typedef struct {
    uint32_t magic;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t reserved;
} SpiBlobWireHeader;

typedef void (*logger_cb_t)(const char *msg);

static void default_logger(const char *msg) { printf("%s\n", msg); }

static void log_progress(logger_cb_t logger_cb, const char *prefix,
                         unsigned long long current, unsigned long long total,
                         int bar_length) {
    if (bar_length <= 0) bar_length = 30;
    if (logger_cb == NULL) logger_cb = default_logger;
    if (prefix == NULL) prefix = "";

    double progress = 0.0;
    if (total > 0) {
        progress = (double)current / (double)total;
    }
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;

    int block = (int)(bar_length * progress);
    if (block < 0) block = 0;
    if (block > bar_length) block = bar_length;

    int percent = (int)(progress * 100.0);

    size_t prefix_len = strlen(prefix);
    size_t buf_len = prefix_len + 3 + (size_t)bar_length + 2 + 4 + 1 + 4;
    char *buf = (char *)malloc(buf_len);
    if (!buf) return;

    char *p = buf;
    memcpy(p, prefix, prefix_len);
    p += prefix_len;
    *p++ = ' ';
    *p++ = '|';
    for (int i = 0; i < block; ++i) *p++ = '#';
    for (int i = block; i < bar_length; ++i) *p++ = ' ';
    *p++ = '|';
    *p++ = ' ';
    snprintf(p, buf_len - (size_t)(p - buf), "%d%%", percent);

    logger_cb(buf);
    free(buf);
}

static const char* get_imx500_cmd_status(uint32_t code) {
    switch (code) {
    case 0x00: return "State transition ready";
    case 0x01: return "State transition done";
    case 0x10: return "Update ready";
    case 0x11: return "Update done";
    case 0x12: return "Update cancel done";
    case 0x21: return "Flash erase done";
    case 0xFF: return "Status Error";
    case 0xFE: return "MAC Authentication Error";
    case 0xFD: return "Timeout Error";
    case 0xFC: return "Parameter Error";
    case 0xFB: return "Internal Error";
    case 0xFA: return "Packet format Error";
    default:   return "Unknown status code";
    }
}

static inline int calc_align(int num, int align) {
  return ((num + align - 1) / align) * align;
}

static uint32_t crc32_update_local(uint32_t crc, const uint8_t *data, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static uint32_t calc_crc32_local(const uint8_t *data, uint32_t size) {
    return ~crc32_update_local(0xFFFFFFFFu, data, size);
}

static std::vector<uint8_t> byteswap_u32_words(const uint8_t *src, uint32_t size) {
    std::vector<uint8_t> out;
    if (!src || size == 0u) {
        return out;
    }
    out.assign(src, src + size);
    for (uint32_t i = 0; i + 4u <= size; i += 4u) {
        out[i + 0] = src[i + 3];
        out[i + 1] = src[i + 2];
        out[i + 2] = src[i + 1];
        out[i + 3] = src[i + 0];
    }
    return out;
}

static std::vector<uint8_t> byteswap_model_payload_4byte(const uint8_t *src, uint32_t size) {
    std::vector<uint8_t> out;
    if (!src || size == 0) {
        return out;
    }
    out.assign(src, src + size);
    for (uint32_t i = 0; i + 4u <= size; i += 4u) {
        out[i + 0] = src[i + 3];
        out[i + 1] = src[i + 2];
        out[i + 2] = src[i + 1];
        out[i + 3] = src[i + 0];
    }
    return out;
}

static bool sdk_i2c_write_reg(uint16_t addr, uint32_t val) {
    if (!g_i2c_driver.write) {
        return false;
    }
    return g_i2c_driver.write(addr, val, 4) >= 0;
}

static bool sdk_i2c_read_reg(uint16_t addr, uint32_t *val) {
    if (!val || !g_i2c_driver.read) {
        return false;
    }
    return g_i2c_driver.read(addr, val, 4) >= 0;
}

static bool wait_for_boot_status(uint32_t min_status, uint32_t timeout_ms,
                                 const char *label) {
    uint32_t boot_status = 0;
    uint32_t elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        if (!sdk_i2c_read_reg(BOOT_STATUS_REG, &boot_status)) {
            printf("%s read boot status failed\n", label);
            return false;
        }
        if (boot_status >= min_status) {
            return true;
        }
        if (g_i2c_driver.slp_ms) {
            g_i2c_driver.slp_ms(SPI_FLASH_POLL_INTERVAL_MS);
        }
        elapsed_ms += SPI_FLASH_POLL_INTERVAL_MS;
    }
    printf("%s wait boot status >= %u timeout, current=%u\n",
           label, (unsigned)min_status, (unsigned)boot_status);
    return false;
}

static int sdk_spi_write(const uint8_t *buf, uint32_t len) {
    if (!buf || len == 0) return -1;
    if (!g_spi_driver.write) return -1;
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > SPI_BRIDGE_BLOCK_LEN) {
            chunk = SPI_BRIDGE_BLOCK_LEN;
        }
        int ret = g_spi_driver.write((uint8_t *)(buf + sent), chunk);
        if (ret < 0 || (uint32_t)ret != chunk) {
            return -1;
        }
        sent += chunk;
        if (sent < len && g_i2c_driver.slp_us) {
            g_i2c_driver.slp_us(SPI_BRIDGE_BLOCK_GAP_US);
        }
    }
    return (int)sent;
}

static int sdk_spi_write_once(const uint8_t *buf, uint32_t len) {
    if (!buf || len == 0) return -1;
    if (!g_spi_driver.write) return -1;
    int ret = g_spi_driver.write((uint8_t *)buf, len);
    if (ret < 0 || (uint32_t)ret != len) {
        return -1;
    }
    return ret;
}

static int sdk_spi_write_bridge_paced(const uint8_t *buf, uint32_t len, uint32_t gap_us) {
    if (!buf || len == 0) return -1;
    if (!g_spi_driver.write) return -1;
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > SPI_BRIDGE_BLOCK_LEN) {
            chunk = SPI_BRIDGE_BLOCK_LEN;
        }
        int ret = g_spi_driver.write((uint8_t *)(buf + sent), chunk);
        if (ret < 0 || (uint32_t)ret != chunk) {
            return -1;
        }
        sent += chunk;
        if (sent < len && gap_us > 0 && g_i2c_driver.slp_us) {
            g_i2c_driver.slp_us(gap_us);
        }
    }
    return (int)sent;
}


int32_t print_buf_hex(const uint8_t* buf, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; ++i) {
        printf("0x%02x ", buf[i]);
    }
    return 0;
}

void imx500_print_header(const IMX500OutputHeader *h)
{
    if (!h) {
        printf("IMX500OutputHeader: NULL\n");
        return;
    }

    printf("IMX500OutputHeader {\n");
    printf("  valid_flag               : 0x%02X (%s)\n",
           h->valid_flag,
           h->valid_flag ? "valid" : "invalid");

    printf("  frame_count              : %u\n", h->frame_count);
    printf("  max_length_of_line        : %u\n", h->max_length_of_line);
    printf("  size_of_ap_p_parameter   : %u\n", h->size_of_ap_parameter);
    printf("  network_ordinal           : %u\n", h->network_ordinal);
    printf("  indicator                 : 0x%02X\n", h->indicator);
    printf("}\n");
}

void unpack_imx500_output_header(const uint8_t* data, IMX500OutputHeader* header) {
    const IMX500OutputHeader *data_ = (const IMX500OutputHeader *)data;
    memcpy(header, data_, sizeof(IMX500OutputHeader));
    // imx500_print_header(header); // for debug
}

extern "C" {
#include <algorithm>
#include <vector>
#include <cstdio>
#include "flatbuffers/flatbuffers.h"

sc_dnn_nw_info_t network_info[MAX_NUM_OF_NETWORKS];
static uint32_t s_dnn_nw_id = 0;
static uint8_t s_num_of_networks = 0;

static const uint16_t REG_OFST0_LEV_PL_NORM_YM_YADD = 0x00;
static const uint16_t REG_OFST1_LEV_PL_NORM_YM_YADD = (REG_OFST0_LEV_PL_NORM_YM_YADD + 0x1A);
static const uint16_t REG_OFST2_LEV_PL_NORM_YM_YADD = (REG_OFST1_LEV_PL_NORM_YM_YADD + 0x0C);
static const uint16_t REG_OFST0_LEV_PL_NORM_YM_YSFT = 0x00;
static const uint16_t REG_OFST1_LEV_PL_NORM_YM_YSFT = (REG_OFST0_LEV_PL_NORM_YM_YSFT + 0x1A);
static const uint16_t REG_OFST2_LEV_PL_NORM_YM_YSFT = (REG_OFST1_LEV_PL_NORM_YM_YSFT + 0x0C);
static const uint16_t REG_OFST0_LEV_PL_NORM_CB_YADD = 0x00;
static const uint16_t REG_OFST1_LEV_PL_NORM_CB_YADD = (REG_OFST0_LEV_PL_NORM_CB_YADD + 0x16);
static const uint16_t REG_OFST2_LEV_PL_NORM_CB_YADD = (REG_OFST1_LEV_PL_NORM_CB_YADD + 0x0C);
static const uint16_t REG_OFST0_LEV_PL_NORM_CR_YADD = 0x00;
static const uint16_t REG_OFST1_LEV_PL_NORM_CR_YADD = (REG_OFST0_LEV_PL_NORM_CR_YADD + 0x12);
static const uint16_t REG_OFST2_LEV_PL_NORM_CR_YADD = (REG_OFST1_LEV_PL_NORM_CR_YADD + 0x0C);
static const uint16_t REG_OFST0_LEV_PL_NORM_CR_YSFT = 0x00;
static const uint16_t REG_OFST1_LEV_PL_NORM_CR_YSFT = (REG_OFST0_LEV_PL_NORM_CR_YSFT + 0x13);
static const uint16_t REG_OFST2_LEV_PL_NORM_CR_YSFT = (REG_OFST1_LEV_PL_NORM_CR_YSFT + 0x0C);
static const uint16_t REG_OFST0_LEV_PL_NORM_CB_YSFT = 0x00;
static const uint16_t REG_OFST1_LEV_PL_NORM_CB_YSFT = (REG_OFST0_LEV_PL_NORM_CR_YSFT + 0x17);
static const uint16_t REG_OFST2_LEV_PL_NORM_CB_YSFT = (REG_OFST1_LEV_PL_NORM_CR_YSFT + 0x0C);

static const uint16_t REG_ADDR_DNN_INPUT_FORMAT_BASE = 0xD750;
static const uint16_t REG_ADDR_DNN_YCMTRX_K00 = 0xD76C;
static const uint16_t REG_ADDR_DNN_YCMTRX_K01 = 0xD76E;
static const uint16_t REG_ADDR_DNN_YCMTRX_K02 = 0xD770;
static const uint16_t REG_ADDR_DNN_YCMTRX_K03 = 0xD772;
static const uint16_t REG_ADDR_DNN_YCMTRX_K10 = 0xD774;
static const uint16_t REG_ADDR_DNN_YCMTRX_K11 = 0xD776;
static const uint16_t REG_ADDR_DNN_YCMTRX_K12 = 0xD778;
static const uint16_t REG_ADDR_DNN_YCMTRX_K13 = 0xD77A;
static const uint16_t REG_ADDR_DNN_YCMTRX_K20 = 0xD77C;
static const uint16_t REG_ADDR_DNN_YCMTRX_K21 = 0xD77E;
static const uint16_t REG_ADDR_DNN_YCMTRX_K22 = 0xD780;
static const uint16_t REG_ADDR_DNN_YCMTRX_K23 = 0xD782;
static const uint16_t REG_OFST_DNN_YCMTRX = 0x24;
static const uint16_t REG_ADDR_DNN_YCMTRX_Y_CLIP = 0xD784;
static const uint16_t REG_ADDR_DNN_YCMTRX_CB_CLIP = 0xD788;
static const uint16_t REG_ADDR_DNN_YCMTRX_CR_CLIP = 0xD78C;
static const uint16_t REG_ADDR_DNN_INPUT_NORM = 0xD708;
static const uint16_t REG_ADDR_DNN_INPUT_NORM_SHIFT = 0xD70A;
static const uint16_t REG_ADDR_DNN_INPUT_NORM_CLIP_MAX = 0xD70C;
static const uint16_t REG_ADDR_DNN_INPUT_NORM_CLIP_MIN = 0xD70E;
static const uint16_t REG_OFST_DNN_INPUT_NORM_CH = 0x08;
static const uint16_t REG_OFST_DNN_INPUT_NORM = 0x18;
static const uint16_t REG_ADDR_DNN_NORM_YM_CLIP = 0xD7D8;
static const uint16_t REG_ADDR_DNN_NORM_CB_CLIP = 0xD7DC;
static const uint16_t REG_ADDR_DNN_NORM_CR_CLIP = 0xD7E0;
static const uint16_t REG_OFST_DNN_NORM_CLIP = 0x0C;
static const uint16_t REG_ADDR_LEV_PL_GAIN_VALUE = 0xD600;
static const uint16_t REG_ADDR_LEV_PL_NORM_YM_YSFT = 0xD629;
static const uint16_t REG_ADDR_LEV_PL_NORM_YM_YADD = 0xD62A;
static const uint16_t REG_ADDR_LEV_PL_NORM_CB_YSFT = 0xD630;
static const uint16_t REG_ADDR_LEV_PL_NORM_CB_YADD = 0xD632;
static const uint16_t REG_ADDR_LEV_PL_NORM_CR_YSFT = 0xD638;
static const uint16_t REG_ADDR_LEV_PL_NORM_CR_YADD = 0xD63A;
static const uint16_t REG_OFST_LEV_PL_GAIN = 0x01;
static const uint16_t REG_ADDR_ROT_DNN_NORM = 0xD684;
static const uint16_t REG_ADDR_ROT_DNN_NORM_SHIFT = 0xD686;
static const uint16_t REG_ADDR_ROT_DNN_NORM_CLIP_MAX = 0xD688;
static const uint16_t REG_ADDR_ROT_DNN_NORM_CLIP_MIN = 0xD68A;
static const uint16_t REG_OFST_ROT_DNN_NORM_CH = 0x08;
static const uint16_t REG_OFST_ROT_DNN_NORM_DNN = 0x20;

static const uint8_t REG_DNN_INPUT_FORMAT_Y = 1;
static const uint8_t REG_DNN_INPUT_FORMAT_YUV444 = 2;
static const uint8_t REG_DNN_INPUT_FORMAT_BAYER_RGB = 5;

#ifndef OUTPUT_TENSOR_POOL_BYTES
#define OUTPUT_TENSOR_POOL_BYTES (4096)
#endif

static uint8_t g_ot_pool[OUTPUT_TENSOR_POOL_BYTES];
static size_t g_ot_pool_used = 0;

static void pool_reset(void) { g_ot_pool_used = 0; }

static void *pool_alloc(size_t n, size_t align) {
    size_t p = g_ot_pool_used;
    size_t mask = (align ? (align - 1) : 0);
    if (align && (align & mask)) {
        return NULL;
    }
    if (align) {
        p = (p + mask) & ~mask;
    }
    if (p + n > sizeof(g_ot_pool)) {
        return NULL;
    }
    g_ot_pool_used = p + n;
    return &g_ot_pool[p];
}

static uint8_t clamp_u8(uint32_t v) { return (v > 0xFFu) ? 0xFFu : (uint8_t)v; }

typedef struct {
    uint32_t input_format;
    int32_t norm_val[BAYER_CH_MAX];
    int32_t norm_shift[BAYER_CH_MAX];
    int32_t div_val[BAYER_CH_MAX];
    int32_t div_shift;
} sc_input_norm_info_t;

static int32_t conv_reg_signed(uint32_t reg_val, int signed_bit, uint32_t reg_mask) {
    if (((reg_val >> (uint32_t)signed_bit) & 1u) == 0u) {
        return (int32_t)reg_val;
    }
    return -(int32_t)((~reg_val + 1u) & reg_mask);
}

static void build_input_norm_info(const sc_dnn_nw_info_t *nw, sc_input_norm_info_t *info) {
    static const int LEV_PL_GAIN_DEC_SHT = 5;
    static const int LEV_PL_NORM_YM_YADD_SIGNED_SHT = 8;
    static const uint32_t LEV_PL_NORM_YM_YADD_MASK = 0x01FFu;
    static const int YCMTRX_KX0_2_DEC_SHT = 10;
    static const int YCMTRX_KX0_2_SIGNED_SHT = 11;
    static const uint32_t YCMTRX_KX0_2_MASK = 0x0FFFu;
    static const int YCMTRX_KX3_DEC_SHT = 4;
    static const int YCMTRX_KX3_SIGNED_SHT = 12;
    static const uint32_t YCMTRX_KX3_MASK = 0x1FFFu;

    memset(info, 0, sizeof(*info));
    info->input_format = nw->inputTensorFormat;
    for (int i = 0; i < BAYER_CH_MAX; ++i) {
        info->div_val[i] = 1;
    }

    if (nw->inputTensorFormat == DNN_INPUT_FORMAT_Y) {
        info->norm_val[0] = conv_reg_signed(nw->yAdd, LEV_PL_NORM_YM_YADD_SIGNED_SHT, LEV_PL_NORM_YM_YADD_MASK);
        info->norm_shift[0] = (int32_t)nw->ySht;
        info->div_val[0] = (nw->yGgain == 0u) ? 1 : (int32_t)nw->yGgain;
        info->div_shift = LEV_PL_GAIN_DEC_SHT;
        return;
    }

    if (nw->inputTensorFormat == DNN_INPUT_FORMAT_BAYER_RGB) {
        for (int j = 0; j < BAYER_CH_MAX; ++j) {
            info->norm_val[j] = conv_reg_signed(nw->rgbNorm[j].add, LEV_PL_NORM_YM_YADD_SIGNED_SHT, LEV_PL_NORM_YM_YADD_MASK);
            info->norm_shift[j] = (int32_t)nw->rgbNorm[j].shift;
            info->div_val[j] = (nw->yGgain == 0u) ? 1 : (int32_t)nw->yGgain;
        }
        info->div_shift = LEV_PL_GAIN_DEC_SHT;
        return;
    }

    info->norm_val[0] = conv_reg_signed(nw->NormK03, YCMTRX_KX3_SIGNED_SHT, YCMTRX_KX3_MASK);
    info->norm_val[1] = conv_reg_signed(nw->NormK13, YCMTRX_KX3_SIGNED_SHT, YCMTRX_KX3_MASK);
    info->norm_val[2] = conv_reg_signed(nw->NormK23, YCMTRX_KX3_SIGNED_SHT, YCMTRX_KX3_MASK);
    info->norm_shift[0] = YCMTRX_KX3_DEC_SHT;
    info->norm_shift[1] = YCMTRX_KX3_DEC_SHT;
    info->norm_shift[2] = YCMTRX_KX3_DEC_SHT;
    if (nw->inputTensorFormat == DNN_INPUT_FORMAT_RGB) {
        info->div_val[0] = conv_reg_signed(nw->NormK00, YCMTRX_KX0_2_SIGNED_SHT, YCMTRX_KX0_2_MASK);
        info->div_val[2] = conv_reg_signed(nw->NormK22, YCMTRX_KX0_2_SIGNED_SHT, YCMTRX_KX0_2_MASK);
    } else {
        info->div_val[0] = conv_reg_signed(nw->NormK02, YCMTRX_KX0_2_SIGNED_SHT, YCMTRX_KX0_2_MASK);
        info->div_val[2] = conv_reg_signed(nw->NormK20, YCMTRX_KX0_2_SIGNED_SHT, YCMTRX_KX0_2_MASK);
    }
    info->div_val[1] = conv_reg_signed(nw->NormK11, YCMTRX_KX0_2_SIGNED_SHT, YCMTRX_KX0_2_MASK);
    info->div_shift = YCMTRX_KX0_2_DEC_SHT - YCMTRX_KX3_DEC_SHT;
}

static sc_output_tensor_size_info_t *ensure_output_arr(uint8_t nwOrdinal) {
    uint8_t num = network_info[nwOrdinal].outputTensorNum;
    if (num == 0) {
        return NULL;
    }
    if (network_info[nwOrdinal].p_outputTensorSizeInfo != NULL) {
        return network_info[nwOrdinal].p_outputTensorSizeInfo;
    }
    size_t arr_bytes = (size_t)num * sizeof(sc_output_tensor_size_info_t);
    sc_output_tensor_size_info_t *arr =
        (sc_output_tensor_size_info_t *)pool_alloc(arr_bytes, alignof(sc_output_tensor_size_info_t));
    if (!arr) {
        return NULL;
    }
    memset(arr, 0, arr_bytes);
    network_info[nwOrdinal].p_outputTensorSizeInfo = arr;
    return arr;
}

static const char *skip_ws(const char *s, const char *end) {
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
        s++;
    }
    return s;
}

static bool next_line(const char **cur, const char *end, const char **lb, const char **le) {
    const char *p = *cur;
    p = skip_ws(p, end);
    if (p >= end) {
        return false;
    }
    const char *start = p;
    while (p < end && *p != '\n') {
        p++;
    }
    const char *stop = p;
    if (stop > start && *(stop - 1) == '\r') {
        stop--;
    }
    if (p < end && *p == '\n') {
        p++;
    }
    *lb = start;
    *le = stop;
    *cur = p;
    return true;
}

static bool parse_kv(const char *lb, const char *le, char *key, size_t key_sz, char *val, size_t val_sz) {
    const char *p = lb;
    while (p < le && (*p == ' ' || *p == '\t')) {
        p++;
    }
    if (p >= le || *p == '#' || *p == ';') {
        return false;
    }

    const char *eq = NULL;
    for (const char *t = p; t < le; t++) {
        if (*t == '=') {
            eq = t;
            break;
        }
    }
    if (!eq) {
        return false;
    }

    const char *k0 = p;
    const char *k1 = eq;
    while (k0 < k1 && (*k0 == ' ' || *k0 == '\t')) {
        k0++;
    }
    while (k1 > k0 && (*(k1 - 1) == ' ' || *(k1 - 1) == '\t')) {
        k1--;
    }

    const char *v0 = eq + 1;
    const char *v1 = le;
    while (v0 < v1 && (*v0 == ' ' || *v0 == '\t')) {
        v0++;
    }
    while (v1 > v0 && (*(v1 - 1) == ' ' || *(v1 - 1) == '\t')) {
        v1--;
    }

    size_t klen = (size_t)(k1 - k0);
    size_t vlen = (size_t)(v1 - v0);
    if (klen + 1 > key_sz || vlen + 1 > val_sz) {
        return false;
    }

    memcpy(key, k0, klen);
    key[klen] = 0;
    memcpy(val, v0, vlen);
    val[vlen] = 0;
    return true;
}

static bool parse_u32_auto(const char *s, uint32_t *out) {
    if (!s || !*s) {
        return false;
    }
    char *endp = NULL;
    unsigned long v = strtoul(s, &endp, 0);
    if (endp == s) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool parse_suffix_index_1based(const char *key, const char *prefix, uint32_t max_n, uint32_t *out_idx0) {
    if (!key || !prefix || !out_idx0) {
        return false;
    }
    size_t plen = strlen(prefix);
    if (strncmp(key, prefix, plen) != 0) {
        return false;
    }
    const char *p = key + plen;
    if (*p == '\0') {
        return false;
    }
    uint32_t n = 0;
    while (*p >= '0' && *p <= '9') {
        n = n * 10u + (uint32_t)(*p - '0');
        p++;
    }
    if (*p != '\0' || n < 1 || n > max_n) {
        return false;
    }
    *out_idx0 = n - 1;
    return true;
}

int set_nw_info_from_flash_buffer(const uint8_t *cfg, size_t cfg_len) {
    if (!cfg || cfg_len == 0) {
        printf("[NW_INFO] cfg buffer invalid\n");
        return -1;
    }

    for (uint32_t i = 0; i < MAX_NUM_OF_NETWORKS; i++) {
        network_info[i].p_outputTensorSizeInfo = NULL;
        network_info[i].outputTensorNum = 0;
        memset(network_info[i].outputTensorDimSize, 0, sizeof(network_info[i].outputTensorDimSize));
        memset(network_info[i].outputTensorPadding, 0, sizeof(network_info[i].outputTensorPadding));
        memset(network_info[i].outputTensorBytesPerElement, 0, sizeof(network_info[i].outputTensorBytesPerElement));
    }
    pool_reset();

    const char *cur = (const char *)cfg;
    const char *end = (const char *)cfg + cfg_len;
    char key[64];
    char val[128];
    uint16_t dnnHeaderSize = 0;

    {
        const char *p = cur;
        while (1) {
            const char *lb, *le;
            if (!next_line(&p, end, &lb, &le)) {
                break;
            }
            if (!parse_kv(lb, le, key, sizeof(key), val, sizeof(val))) {
                continue;
            }

            uint32_t get_value = 0;
            if (strcmp(key, "networkID") == 0) {
                if (strlen(val) < 6) {
                    printf("[NW_INFO] networkID must be 6 digits, got '%s'\n", val);
                    return -1;
                }

                get_value = (uint32_t)atoi(val);
                if (get_value > SC_DNN_MAX_NETWORK_ID_DEC) {
                    printf("[NW_INFO] Invalid Network ID(decimal) %lu\n", (unsigned long)get_value);
                    return -1;
                }

                s_dnn_nw_id = 0;
                for (int i = 0; i < 6; i++) {
                    char c = val[i];
                    if (c < '0' || c > '9') {
                        printf("[NW_INFO] networkID must be 6 digits, got '%s'\n", val);
                        return -1;
                    }
                    s_dnn_nw_id |= (uint32_t)(c - '0') << (20 - (i * 4));
                }
            } else if (strcmp(key, "apParamSize") == 0) {
                get_value = (uint32_t)atoi(val);
                dnnHeaderSize = (uint16_t)(12 + (((get_value + 15) / 16) * 16));
            } else if (strcmp(key, "networkNum") == 0) {
                get_value = (uint32_t)atoi(val);
                s_num_of_networks = (uint8_t)get_value;
            }
        }
    }

    if (s_dnn_nw_id > SC_DNN_MAX_NETWORK_ID) {
        printf("[NW_INFO] Invalid Network ID(BCD) 0x%08lX\n", (unsigned long)s_dnn_nw_id);
        return -1;
    }
    if (dnnHeaderSize > MAX_DNN_HEADER_SIZE) {
        printf("[NW_INFO] Invalid DNN Header Size %u\n", dnnHeaderSize);
        return -1;
    }
    if (s_num_of_networks == 0 || s_num_of_networks > MAX_NUM_OF_NETWORKS) {
        printf("[NW_INFO] Invalid num of networks %u\n", s_num_of_networks);
        return -1;
    }

    {
        const char *p = cur;
        int cur_ord = -1;
        bool seen_ord[MAX_NUM_OF_NETWORKS] = {0};

        while (1) {
            const char *lb, *le;
            if (!next_line(&p, end, &lb, &le)) {
                break;
            }
            if (!parse_kv(lb, le, key, sizeof(key), val, sizeof(val))) {
                continue;
            }

            uint32_t v = 0;
            if (strcmp(key, "networkOrdinal") == 0) {
                uint32_t ord_u = 0;
                if (!parse_u32_auto(val, &ord_u)) {
                    continue;
                }
                if (ord_u >= s_num_of_networks) {
                    printf("[NW_INFO] ignore networkOrdinal=%lu (>= networkNum=%u)\n",
                           (unsigned long)ord_u, s_num_of_networks);
                    cur_ord = -1;
                    continue;
                }
                cur_ord = (int)ord_u;
                seen_ord[cur_ord] = true;
                network_info[cur_ord].dnnHeaderSize = dnnHeaderSize;
                continue;
            }

            if (cur_ord < 0) {
                continue;
            }

            uint8_t nwOrdinal = (uint8_t)cur_ord;
            if (strcmp(key, "inputTensorWidth") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].inputTensorWidth = (uint16_t)v;
            } else if (strcmp(key, "inputTensorHeight") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].inputTensorHeight = (uint16_t)v;
            } else if (strcmp(key, "inputTensorFormat") == 0) {
                if (strcmp(val, "RGB") == 0) network_info[nwOrdinal].inputTensorFormat = DNN_INPUT_FORMAT_RGB;
                else if (strcmp(val, "BGR") == 0) network_info[nwOrdinal].inputTensorFormat = DNN_INPUT_FORMAT_BGR;
                else if (strcmp(val, "Y") == 0) network_info[nwOrdinal].inputTensorFormat = DNN_INPUT_FORMAT_Y;
                else if (strcmp(val, "BayerRGB") == 0) network_info[nwOrdinal].inputTensorFormat = DNN_INPUT_FORMAT_BAYER_RGB;
                else {
                    printf("[NW_INFO] invalid InputTensor format(%s), corrected to RGB\n", val);
                    network_info[nwOrdinal].inputTensorFormat = DNN_INPUT_FORMAT_RGB;
                }
            } else if (strcmp(key, "inputTensorNorm_K00") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK00 = (uint16_t)(v & 0x0FFF);
            } else if (strcmp(key, "inputTensorNorm_K02") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK02 = (uint16_t)(v & 0x0FFF);
            } else if (strcmp(key, "inputTensorNorm_K03") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK03 = (uint16_t)(v & 0x1FFF);
            } else if (strcmp(key, "inputTensorNorm_K11") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK11 = (uint16_t)(v & 0x0FFF);
            } else if (strcmp(key, "inputTensorNorm_K13") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK13 = (uint16_t)(v & 0x1FFF);
            } else if (strcmp(key, "inputTensorNorm_K20") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK20 = (uint16_t)(v & 0x0FFF);
            } else if (strcmp(key, "inputTensorNorm_K22") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK22 = (uint16_t)(v & 0x0FFF);
            } else if (strcmp(key, "inputTensorNorm_K23") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].NormK23 = (uint16_t)(v & 0x1FFF);
            } else if (strcmp(key, "yClip") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].yClip = v;
            } else if (strcmp(key, "cbClip") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].cbClip = v;
            } else if (strcmp(key, "crClip") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].crClip = v;
            } else if (strcmp(key, "inputNorm_CH0") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_R].add = (uint16_t)(v & 0x01FF);
            } else if (strcmp(key, "inputNormShift_CH0") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_R].shift = (uint8_t)(v & 0x01);
            } else if (strcmp(key, "inputNormClip_CH0") == 0) {
                if (parse_u32_auto(val, &v)) {
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_R].clipMax = (uint16_t)((v >> 16) & 0x01FF);
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_R].clipMin = (uint16_t)((v >> 0) & 0x01FF);
                }
            } else if (strcmp(key, "inputNorm_CH1") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_GR].add = (uint16_t)(v & 0x01FF);
            } else if (strcmp(key, "inputNormShift_CH1") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_GR].shift = (uint8_t)(v & 0x01);
            } else if (strcmp(key, "inputNormClip_CH1") == 0) {
                if (parse_u32_auto(val, &v)) {
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_GR].clipMax = (uint16_t)((v >> 16) & 0x01FF);
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_GR].clipMin = (uint16_t)((v >> 0) & 0x01FF);
                }
            } else if (strcmp(key, "inputNorm_CH2") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_GB].add = (uint16_t)(v & 0x01FF);
            } else if (strcmp(key, "inputNormShift_CH2") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_GB].shift = (uint8_t)(v & 0x01);
            } else if (strcmp(key, "inputNormClip_CH2") == 0) {
                if (parse_u32_auto(val, &v)) {
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_GB].clipMax = (uint16_t)((v >> 16) & 0x01FF);
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_GB].clipMin = (uint16_t)((v >> 0) & 0x01FF);
                }
            } else if (strcmp(key, "inputNorm_CH3") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_B].add = (uint16_t)(v & 0x01FF);
            } else if (strcmp(key, "inputNormShift_CH3") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].rgbNorm[BAYER_CH_B].shift = (uint8_t)(v & 0x01);
            } else if (strcmp(key, "inputNormClip_CH3") == 0) {
                if (parse_u32_auto(val, &v)) {
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_B].clipMax = (uint16_t)((v >> 16) & 0x01FF);
                    network_info[nwOrdinal].rgbNorm[BAYER_CH_B].clipMin = (uint16_t)((v >> 0) & 0x01FF);
                }
            } else if (strcmp(key, "inputTensorNorm_YGain") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].yGgain = (uint8_t)v;
            } else if (strcmp(key, "inputTensorNorm_YAdd") == 0) {
                if (parse_u32_auto(val, &v)) network_info[nwOrdinal].yAdd = (uint16_t)v;
            } else if (strcmp(key, "outputTensorNum") == 0) {
                if (parse_u32_auto(val, &v)) {
                    if (v == 0 || v > MAX_OUTPUT_TENSOR_NUM) {
                        printf("[NW_INFO] outputTensorNum invalid for networkOrdinal %u: %lu\n",
                               nwOrdinal, (unsigned long)v);
                        return -1;
                    }
                    network_info[nwOrdinal].outputTensorNum = (uint8_t)v;
                    sc_output_tensor_size_info_t *arr = ensure_output_arr(nwOrdinal);
                    if (!arr) {
                        printf("[NW_INFO] output tensor pool exhausted for nw %u\n", nwOrdinal);
                        return -1;
                    }
                    for (uint32_t i = 0; i < v; i++) {
                        if (network_info[nwOrdinal].outputTensorDimSize[i] != 0) arr[i].dimSize = network_info[nwOrdinal].outputTensorDimSize[i];
                        if (network_info[nwOrdinal].outputTensorPadding[i] != 0) arr[i].padding = network_info[nwOrdinal].outputTensorPadding[i];
                        if (network_info[nwOrdinal].outputTensorBytesPerElement[i] != 0) arr[i].bytePerElement = network_info[nwOrdinal].outputTensorBytesPerElement[i];
                    }
                }
            } else {
                uint32_t idx0;
                if (parse_suffix_index_1based(key, "outputTensorDimSize", MAX_OUTPUT_TENSOR_NUM, &idx0)) {
                    if (parse_u32_auto(val, &v)) {
                        network_info[nwOrdinal].outputTensorDimSize[idx0] = v;
                        sc_output_tensor_size_info_t *arr = ensure_output_arr(nwOrdinal);
                        if (arr && idx0 < network_info[nwOrdinal].outputTensorNum) {
                            arr[idx0].dimSize = v;
                        }
                    }
                } else if (parse_suffix_index_1based(key, "outputTensorPadding", MAX_OUTPUT_TENSOR_NUM, &idx0)) {
                    if (parse_u32_auto(val, &v)) {
                        network_info[nwOrdinal].outputTensorPadding[idx0] = clamp_u8(v);
                        sc_output_tensor_size_info_t *arr = ensure_output_arr(nwOrdinal);
                        if (arr && idx0 < network_info[nwOrdinal].outputTensorNum) {
                            arr[idx0].padding = (uint8_t)v;
                        }
                    }
                } else if (parse_suffix_index_1based(key, "outputTensorBytesPerElement", MAX_OUTPUT_TENSOR_NUM, &idx0)) {
                    if (parse_u32_auto(val, &v)) {
                        network_info[nwOrdinal].outputTensorBytesPerElement[idx0] = clamp_u8(v);
                        sc_output_tensor_size_info_t *arr = ensure_output_arr(nwOrdinal);
                        if (arr && idx0 < network_info[nwOrdinal].outputTensorNum) {
                            arr[idx0].bytePerElement = (uint8_t)v;
                        }
                    }
                }
            }
        }

        for (uint32_t i = 0; i < s_num_of_networks; i++) {
            if (!seen_ord[i]) {
                printf("[NW_INFO] Missing networkOrdinal block for %lu\n", (unsigned long)i);
                return -1;
            }
            if (network_info[i].outputTensorNum == 0) {
                printf("[NW_INFO] Missing outputTensorNum for networkOrdinal %lu\n", (unsigned long)i);
                return -1;
            }
            sc_output_tensor_size_info_t *arr = ensure_output_arr((uint8_t)i);
            if (!arr) {
                printf("[NW_INFO] output tensor array missing for nw %lu\n", (unsigned long)i);
                return -1;
            }
        }
    }

    return 0;
}

void dump_network_info_list(void) {
    printf("========== SDK NETWORK INFO LIST DUMP ==========\n");
    printf("num_of_networks=%u\n", (unsigned)s_num_of_networks);
    uint32_t n = s_num_of_networks;
    if (n > MAX_NUM_OF_NETWORKS) {
        n = MAX_NUM_OF_NETWORKS;
    }
    for (uint32_t i = 0; i < n; i++) {
        const sc_dnn_nw_info_t *nw = &network_info[i];
        printf("---- NetworkOrdinal=%u ----\n", (unsigned)i);
        printf("dnnHeaderSize=%u inputTensorWidth=%u inputTensorHeight=%u inputTensorFormat=%u\n",
               (unsigned)nw->dnnHeaderSize,
               (unsigned)nw->inputTensorWidth,
               (unsigned)nw->inputTensorHeight,
               (unsigned)nw->inputTensorFormat);
        printf("outputTensorNum=%u\n", (unsigned)nw->outputTensorNum);
        for (uint32_t t = 0; t < nw->outputTensorNum; t++) {
            sc_output_tensor_size_info_t *arr = nw->p_outputTensorSizeInfo;
            uint32_t dim = arr ? arr[t].dimSize : nw->outputTensorDimSize[t];
            uint8_t pad = arr ? arr[t].padding : nw->outputTensorPadding[t];
            uint8_t bpe = arr ? arr[t].bytePerElement : nw->outputTensorBytesPerElement[t];
            printf("  tensor[%u]: dimSize=%u padding=%u bytePerElement=%u\n",
                   (unsigned)t, (unsigned)dim, (unsigned)pad, (unsigned)bpe);
        }
    }
}

static int i2c_passthrough_write(uint16_t reg_addr, uint32_t data, uint32_t size) {
    if (size == 1) {
        return sensor_i2c_write_16_8(reg_addr, (uint8_t)(data & 0xFFu));
    }
    if (size == 2) {
        return sensor_i2c_write_16_16(reg_addr, (uint16_t)(data & 0xFFFFu));
    }
    if (size == 4) {
        return sensor_i2c_write_16_32(reg_addr, data);
    }
    return -1;
}

static int init_input_tensor_preprocess_config(void) {
    if (s_num_of_networks == 0) {
        return 0;
    }
    if (s_num_of_networks > MAX_NUM_OF_NETWORKS) {
        return -1;
    }

    static const uint16_t lev_pl_norm_offset_add[3][3] = {
        {REG_OFST0_LEV_PL_NORM_YM_YADD, REG_OFST1_LEV_PL_NORM_YM_YADD, REG_OFST2_LEV_PL_NORM_YM_YADD},
        {REG_OFST0_LEV_PL_NORM_CB_YADD, REG_OFST1_LEV_PL_NORM_CB_YADD, REG_OFST2_LEV_PL_NORM_CB_YADD},
        {REG_OFST0_LEV_PL_NORM_CR_YADD, REG_OFST1_LEV_PL_NORM_CR_YADD, REG_OFST2_LEV_PL_NORM_CR_YADD}
    };

    static const uint16_t lev_pl_norm_offset_sht[3][3] = {
        {REG_OFST0_LEV_PL_NORM_YM_YSFT, REG_OFST1_LEV_PL_NORM_YM_YSFT, REG_OFST2_LEV_PL_NORM_YM_YSFT},
        {REG_OFST0_LEV_PL_NORM_CB_YSFT, REG_OFST1_LEV_PL_NORM_CB_YSFT, REG_OFST2_LEV_PL_NORM_CB_YSFT},
        {REG_OFST0_LEV_PL_NORM_CR_YSFT, REG_OFST1_LEV_PL_NORM_CR_YSFT, REG_OFST2_LEV_PL_NORM_CR_YSFT}
    };

    static const uint16_t ycmtrx_regs[] = {
        REG_ADDR_DNN_YCMTRX_K00, REG_ADDR_DNN_YCMTRX_K01, REG_ADDR_DNN_YCMTRX_K02, REG_ADDR_DNN_YCMTRX_K03,
        REG_ADDR_DNN_YCMTRX_K10, REG_ADDR_DNN_YCMTRX_K11, REG_ADDR_DNN_YCMTRX_K12, REG_ADDR_DNN_YCMTRX_K13,
        REG_ADDR_DNN_YCMTRX_K20, REG_ADDR_DNN_YCMTRX_K21, REG_ADDR_DNN_YCMTRX_K22, REG_ADDR_DNN_YCMTRX_K23
    };

    for (size_t i = 0; i < s_num_of_networks; ++i) {
        const sc_dnn_nw_info_t *nw = &network_info[i];
        printf("Set network %u preprocess...\n", (unsigned)i);

        if (nw->inputTensorFormat == DNN_INPUT_FORMAT_Y) {
            if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_FORMAT_BASE + (uint16_t)i, REG_DNN_INPUT_FORMAT_Y, 1) < 0) return -1;

            for (size_t r = 0; r < (sizeof(ycmtrx_regs) / sizeof(ycmtrx_regs[0])); ++r) {
                if (i2c_passthrough_write(ycmtrx_regs[r] + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 2) < 0) return -1;
            }
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_Y_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_CB_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_CR_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 4) < 0) return -1;

            for (int j = 0; j < 3; ++j) {
                bool is_y = (j == 0);
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          is_y ? nw->rgbNorm[j].add : 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_SHIFT + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          is_y ? nw->rgbNorm[j].shift : 0, 1) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_CLIP_MAX + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          is_y ? nw->rgbNorm[j].clipMax : 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_CLIP_MIN + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          is_y ? nw->rgbNorm[j].clipMin : 0, 2) < 0) return -1;
            }

            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_YM_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), nw->yClip, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_CB_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_CR_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), 0, 4) < 0) return -1;

            if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YADD + lev_pl_norm_offset_add[0][i], nw->yAdd, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YSFT + lev_pl_norm_offset_sht[0][i], 0, 1) < 0) return -1;

            for (int c = 1; c < 3; ++c) {
                if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YADD + lev_pl_norm_offset_add[c][i], 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YSFT + lev_pl_norm_offset_sht[c][i], 0, 1) < 0) return -1;
            }

            if (i2c_passthrough_write(REG_ADDR_LEV_PL_GAIN_VALUE + (uint16_t)(i * REG_OFST_LEV_PL_GAIN), nw->yGgain, 1) < 0) return -1;

            for (int j = 0; j < BAYER_CH_MAX; ++j) {
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_SHIFT + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 1) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_CLIP_MAX + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_CLIP_MIN + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 2) < 0) return -1;
            }
        } else if (nw->inputTensorFormat == DNN_INPUT_FORMAT_BAYER_RGB) {
            if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_FORMAT_BASE + (uint16_t)i, REG_DNN_INPUT_FORMAT_BAYER_RGB, 1) < 0) return -1;

            for (size_t r = 0; r < (sizeof(ycmtrx_regs) / sizeof(ycmtrx_regs[0])); ++r) {
                if (i2c_passthrough_write(ycmtrx_regs[r] + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 2) < 0) return -1;
            }
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_Y_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_CB_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_CR_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), 0, 4) < 0) return -1;

            for (int j = 0; j < 3; ++j) {
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM), 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_SHIFT + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM), 0, 1) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_CLIP_MAX + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM), 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_CLIP_MIN + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM), 0, 2) < 0) return -1;
            }

            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_YM_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_CB_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), 0, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_CR_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), 0, 4) < 0) return -1;

            for (int c = 0; c < 3; ++c) {
                if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YADD + lev_pl_norm_offset_add[c][i], 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YSFT + lev_pl_norm_offset_sht[c][i], 0, 1) < 0) return -1;
            }

            if (i2c_passthrough_write(REG_ADDR_LEV_PL_GAIN_VALUE + (uint16_t)(i * REG_OFST_LEV_PL_GAIN), nw->yGgain, 1) < 0) return -1;

            for (int j = 0; j < BAYER_CH_MAX; ++j) {
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), nw->rgbNorm[j].add, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_SHIFT + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), nw->rgbNorm[j].shift, 1) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_CLIP_MAX + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), nw->rgbNorm[j].clipMax, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_CLIP_MIN + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), nw->rgbNorm[j].clipMin, 2) < 0) return -1;
            }
        } else {
            if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_FORMAT_BASE + (uint16_t)i, REG_DNN_INPUT_FORMAT_YUV444, 1) < 0) return -1;

            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K00 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK00, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K01 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK01, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K02 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK02, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K03 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK03, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K10 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK10, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K11 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK11, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K12 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK12, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K13 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK13, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K20 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK20, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K21 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK21, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K22 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK22, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_K23 + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->NormK23, 2) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_Y_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->yClip, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_CB_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->cbClip, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_YCMTRX_CR_CLIP + (uint16_t)(i * REG_OFST_DNN_YCMTRX), nw->crClip, 4) < 0) return -1;

            for (int j = 0; j < 3; ++j) {
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          nw->rgbNorm[j].add, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_SHIFT + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          nw->rgbNorm[j].shift, 1) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_CLIP_MAX + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          nw->rgbNorm[j].clipMax, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_DNN_INPUT_NORM_CLIP_MIN + j * REG_OFST_DNN_INPUT_NORM_CH + (uint16_t)(i * REG_OFST_DNN_INPUT_NORM),
                                          nw->rgbNorm[j].clipMin, 2) < 0) return -1;
            }

            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_YM_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), nw->yClip, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_CB_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), nw->cbClip, 4) < 0) return -1;
            if (i2c_passthrough_write(REG_ADDR_DNN_NORM_CR_CLIP + (uint16_t)(i * REG_OFST_DNN_NORM_CLIP), nw->crClip, 4) < 0) return -1;

            for (int c = 0; c < 3; ++c) {
                if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YADD + lev_pl_norm_offset_add[c][i], 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_LEV_PL_NORM_YM_YSFT + lev_pl_norm_offset_sht[c][i], 0, 1) < 0) return -1;
            }

            if (i2c_passthrough_write(REG_ADDR_LEV_PL_GAIN_VALUE + (uint16_t)(i * REG_OFST_LEV_PL_GAIN), nw->yGgain, 1) < 0) return -1;

            for (int j = 0; j < BAYER_CH_MAX; ++j) {
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_SHIFT + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 1) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_CLIP_MAX + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 2) < 0) return -1;
                if (i2c_passthrough_write(REG_ADDR_ROT_DNN_NORM_CLIP_MIN + j * REG_OFST_ROT_DNN_NORM_CH + (uint16_t)(i * REG_OFST_ROT_DNN_NORM_DNN), 0, 2) < 0) return -1;
            }
        }
    }

    return 0;
}

bool parse_ap_params(const uint8_t* data, size_t data_len, DetectionResult* detection_result) {
    if (!data || !detection_result) {
        printf("parse_ap_params: null input\n");
        return false;
    }
    if (data_len < IMX500_HEADER_LEN) {
        printf("parse_ap_params: data_len too small: %u\n", (unsigned)data_len);
        return false;
    }

    uint32_t data_offset = 0;
    IMX500OutputHeader header;
    unpack_imx500_output_header(data, &header);
    data_offset += IMX500_HEADER_LEN;

    if (header.size_of_ap_parameter == 0) {
        printf("ApParams size is 0\n");
        return false;
    }

    if ((size_t)data_offset + (size_t)header.size_of_ap_parameter > data_len) {
        printf("ApParams out of range: offset=%lu size=%u data_len=%u\n",
               data_offset, header.size_of_ap_parameter, (unsigned)data_len);
        return false;
    }

    const uint8_t* ap_buf = data + data_offset;
    size_t ap_len = header.size_of_ap_parameter;

    // FlatBuffers verify
    flatbuffers::Verifier verifier(ap_buf, ap_len);
    if (!apParams::fb::VerifyFBApParamsBuffer(verifier)) {
        // printf("ApParams flatbuffer verify failed\n");
        return false;
    }

    const apParams::fb::FBApParams* ap_parameter = apParams::fb::GetFBApParams(ap_buf);
    if (!ap_parameter) {
        printf("GetFBApParams returned null\n");
        return false;
    }

    auto networks = ap_parameter->networks();
    if (!networks || networks->size() == 0) {
        printf("networks is null or empty\n");
        return false;
    }

    auto network = networks->Get(0);
    if (!network) {
        printf("network[0] is null\n");
        return false;
    }

    auto output_tensors = network->outputTensors();
    if (!output_tensors) {
        printf("outputTensors is null (schema mismatch or field missing)\n");
        return false;
    }

    if (output_tensors->size() < 4) {
        printf("OutputTensor num is insufficient: %lu\n", (unsigned long)output_tensors->size());
        return false;
    }

    data_offset += header.size_of_ap_parameter;
    if ((size_t)data_offset > data_len) {
        printf("output tensor data offset out of range: %lu / %u\n",
               data_offset, (unsigned)data_len);
        return false;
    }

    const uint8_t* output_tensor_data = data + data_offset;

    std::vector<const uint8_t*> output_tensor_ptrs;
    std::vector<uint32_t> output_tensor_sizes;
    output_tensor_ptrs.reserve(output_tensors->size());
    output_tensor_sizes.reserve(output_tensors->size());

    uint32_t output_data_offset = 0;

    for (uint32_t i = 0; i < output_tensors->size(); ++i) {
        auto t = output_tensors->Get(i);
        if (!t) {
            printf("output_tensors[%lu] is null\n", i);
            return false;
        }

        auto dims = t->dimensions();
        if (!dims || dims->size() == 0) {
            printf("tensor[%lu] dims is null/empty\n", i);
            return false;
        }

        uint32_t tensor_elements = 1;
        for (uint32_t j = 0; j < dims->size(); ++j) {
            auto d = dims->Get(j);
            if (!d) {
                printf("tensor[%lu] dim[%lu] is null\n", i, j);
                return false;
            }
            uint32_t s = (uint32_t)d->size();
            if (s == 0) {
                printf("tensor[%lu] dim[%lu] size=0\n", i, j);
                return false;
            }
            // over prevention
            if (tensor_elements > (UINT32_MAX / s)) {
                printf("tensor[%lu] elements overflow\n", i);
                return false;
            }
            tensor_elements *= s;
        }

        uint8_t bits_per_element = t->bitsPerElement();
        uint32_t tensor_bytes = (bits_per_element == 16) ? (tensor_elements * 2) : tensor_elements;
        uint32_t tensor_bytes_aligned = ALIGN_UP(tensor_bytes, 4);

        if ((size_t)data_offset + (size_t)output_data_offset + (size_t)tensor_bytes_aligned > data_len) {
            printf("tensor[%lu] data out of range: off=%lu bytes=%lu aligned=%lu data_len=%u\n",
                   i, output_data_offset, tensor_bytes, tensor_bytes_aligned, (unsigned)data_len);
            return false;
        }

        output_tensor_ptrs.push_back(output_tensor_data + output_data_offset);
        output_tensor_sizes.push_back(tensor_elements);
        output_data_offset += tensor_bytes_aligned;
    }

    const auto* bbox_tensor = output_tensors->Get(0);
    const auto* score_tensor = output_tensors->Get(1);
    const auto* class_tensor = output_tensors->Get(2);
    const auto* detect_num_tensor = output_tensors->Get(3);

    const auto* bbox_data = reinterpret_cast<const int16_t*>(output_tensor_ptrs[0]);
    const auto* score_data = reinterpret_cast<const uint8_t*>(output_tensor_ptrs[1]);
    const auto* class_data = reinterpret_cast<const int16_t*>(output_tensor_ptrs[2]);
    const auto* detect_num_data = reinterpret_cast<const int16_t*>(output_tensor_ptrs[3]);

    uint32_t bbox_elements = output_tensor_sizes[0];
    uint32_t bbox_stride = bbox_elements / 4;
    if (bbox_stride == 0) {
        printf("OutputTensor bbox stride is invalid: %lu\n", (unsigned long)bbox_elements);
        return false;
    }

    uint32_t score_elements = output_tensor_sizes[1];
    uint32_t class_elements = output_tensor_sizes[2];
    uint32_t detect_num = detect_num_data ? (uint32_t)detect_num_data[0] : 0;

    uint32_t max_items = std::min({bbox_stride, score_elements, class_elements, detect_num, (uint32_t)MAX_DETECT_ITEM_NUM});

    auto bboxs = detection_result->bboxs;
    detection_result->valid_num = 0;

    const float confidence_threshold = 0.1f;
    for (uint32_t i = 0; i < max_items; ++i) {
        float confidence = (static_cast<float>(score_data[i]) - score_tensor->shift()) * score_tensor->scale();
        if (confidence < confidence_threshold) continue;

        float xmin = (static_cast<float>(bbox_data[i]) - bbox_tensor->shift()) * bbox_tensor->scale();
        float ymin = (static_cast<float>(bbox_data[i + bbox_stride]) - bbox_tensor->shift()) * bbox_tensor->scale();
        float xmax = (static_cast<float>(bbox_data[i + bbox_stride * 2]) - bbox_tensor->shift()) * bbox_tensor->scale();
        float ymax = (static_cast<float>(bbox_data[i + bbox_stride * 3]) - bbox_tensor->shift()) * bbox_tensor->scale();
        uint32_t class_id = (uint32_t)((static_cast<float>(class_data[i]) - class_tensor->shift()) * class_tensor->scale());

        bboxs->class_id = class_id;
        bboxs->score = confidence;
        bboxs->x1 = xmin;
        bboxs->y1 = ymin;
        bboxs->x2 = xmax;
        bboxs->y2 = ymax;

        printf("box[%lu]: xmin=%0.2f ymin=%0.2f xmax=%0.2f ymax=%0.2f cls_id=%lu score=%0.3f\n",
               (unsigned long)i, xmin, ymin, xmax, ymax, (unsigned long)class_id, confidence);

        bboxs++;
        detection_result->valid_num++;
    }

    return true;
}

uint32_t bbox_coordinate_x_scale_map(float x, uint32_t s_w, uint32_t t_w) {
    uint32_t x_ = 0;
    float s = (float)(t_w) / (float)(s_w);
    x_ = MIN((uint32_t)(x*s), t_w);
    return x_;
}

uint32_t bbox_coordinate_y_scale_map(float y, uint32_t s_h, uint32_t t_h) {
    uint32_t y_ = 0;
    float s = (float)(t_h) / (float)(s_h);
    y_ = MIN((uint32_t)(y*s), t_h);
    return y_;
}

imx500_err_t imx500_res_read(uint32_t cmd_id,
                            uint32_t *data,
                            uint32_t wait_ms)
{
    if (data == NULL) {
        return IMX500_CMD_ERR_INVALID_ARG;
    }

    int ret = g_i2c_driver.write(cmd_id, 0x00000000, 4);
    if (ret < 0) {
        return IMX500_CMD_ERR_I2C_WRITE;
    }

    uint32_t elapsed = 0;
    const uint32_t poll_interval_ms = 1;

    uint32_t imx500_cmd_running_status = 0;
    while (elapsed < wait_ms) {
        g_i2c_driver.slp_ms(poll_interval_ms);
        elapsed += poll_interval_ms;
        ret = g_i2c_driver.read(IMX500_COMMAND_RUNNING_STATUS, &imx500_cmd_running_status, 4);
        if (ret < 0) {
            return IMX500_CMD_ERR_I2C_READ;
        }
        if (imx500_cmd_running_status == 0) {
            return IMX500_CMD_ERR_NOT_EFFECTIVE;
        } else if (imx500_cmd_running_status == 1) {
            break;
        } else if (imx500_cmd_running_status == 2) {
            return IMX500_CMD_ERR_RUNNING_FAILED;
        } else if (imx500_cmd_running_status == 3) {
            continue;
        }
    }
    if (imx500_cmd_running_status != 1) {
        return IMX500_CMD_ERR_TIMEOUT;
    }

    ret = g_i2c_driver.read(IMX500_COMMAND_RETURN, data, 4);
    if (ret < 0) {
        return IMX500_CMD_ERR_I2C_READ;
    }

    return IMX500_CMD_OK;
}

static imx500_err_t imx500_res_write(uint32_t cmd_id,
                                     uint32_t *data,
                                     uint32_t wait_ms)
{
    if (data == NULL) {
        return IMX500_CMD_ERR_INVALID_ARG;
    }

    int ret = g_i2c_driver.write(cmd_id, *data, 4);
    if (ret < 0) {
        return IMX500_CMD_ERR_I2C_WRITE;
    }

    uint32_t elapsed = 0;
    const uint32_t poll_interval_ms = 1;

    uint32_t imx500_cmd_running_status = 0;
    while (elapsed < wait_ms) {
        g_i2c_driver.slp_ms(poll_interval_ms);
        elapsed += poll_interval_ms;
        ret = g_i2c_driver.read(IMX500_COMMAND_RUNNING_STATUS, &imx500_cmd_running_status, 4);
        if (ret < 0) {
            return IMX500_CMD_ERR_I2C_READ;
        }
        if (imx500_cmd_running_status == 0) {
            return IMX500_CMD_ERR_NOT_EFFECTIVE;
        } else if (imx500_cmd_running_status == 1) {
            break;
        } else if (imx500_cmd_running_status == 2) {
            return IMX500_CMD_ERR_RUNNING_FAILED;
        } else if (imx500_cmd_running_status == 3) {
            continue;
        }
    }
    if (imx500_cmd_running_status != 1) {
        return IMX500_CMD_ERR_TIMEOUT;
    }

    ret = g_i2c_driver.read(IMX500_COMMAND_RETURN, data, 4);
    if (ret < 0) {
        return IMX500_CMD_ERR_I2C_READ;
    }

    return IMX500_CMD_OK;
}

void imx500_dump_basic_info()
{
    uint32_t val = 0;

    struct {
        uint32_t cmd;
        const char *name;
    } cmd_list[] = {
        {IMX500_COMMAND_GET_ES_VER,                "ES_VER"},
        {IMX500_COMMAND_GET_LOADER_FW_VER,         "LOADER_FW_VER"},
        {IMX500_COMMAND_GET_MAIN_FW_VER,           "MAIN_FW_VER"},
        {IMX500_COMMAND_GET_SCPU_VER,              "SCPU_VER"},
        {IMX500_COMMAND_GET_DCPU_VER,              "DCPU_VER"},
        {IMX500_COMMAND_GET_ICPU_VER,              "ICPU_VER"},
        {IMX500_COMMAND_GET_CRAM_PARAM_VER,        "CRAM_PARAM_VER"},
        {IMX500_COMMAND_GET_DWP_CP_VER,            "DWP_CP_VER"},
        {IMX500_COMMAND_GET_NETWORK_ID,            "NETWORK_ID"},
        {IMX500_COMMAND_GET_MODEL_VER,             "MODEL_VER"},
        {IMX500_COMMAND_GET_CONVERTER_VER,         "CONVERTER_VER"},
        {IMX500_COMMAND_GET_AP_PARAM_REV,          "AP_PARAM_REV"},
        {IMX500_COMMAND_GET_DNN_PARAM_REV,         "DNN_PARAM_REV"},
        {IMX500_COMMAND_GET_CFG_BLOB1_REV,         "CFG_BLOB1_REV"},
        {IMX500_COMMAND_GET_CFG_BLOB2_REV,         "CFG_BLOB2_REV"},
        {IMX500_COMMAND_GET_SDSP_A_REV,            "SDSP_A_REV"},
        {IMX500_COMMAND_GET_SDSP_C_REV,            "SDSP_C_REV"},
        {IMX500_COMMAND_GET_NETWORK_WEIGHTS_REV,   "NETWORK_WEIGHTS_REV"},
        {IMX500_COMMAND_GET_SENSOR_PROD_ID,        "SENSOR_PROD_ID"},
        {IMX500_COMMAND_GET_EXEC_DNN_INDEX,        "EXEC_DNN_INDEX"},
        {IMX500_COMMAND_GET_EXEC_DNN_NUM,          "EXEC_DNN_NUM"},
        {IMX500_COMMAND_GET_FRAMERATE,             "FRAMERATE"},
    };

    for (size_t i = 0; i < sizeof(cmd_list) / sizeof(cmd_list[0]); i++) {
        int ret = imx500_res_read(cmd_list[i].cmd, &val, 10);
        printf("%-22s : 0x%08X (%u) ec: %d\n", cmd_list[i].name, val, val, ret);
    }
    uint32_t dev_id[4];
    imx500_res_read(IMX500_COMMAND_GET_SENSOR_DEVICE_ID_1, &dev_id[0], 10);
    imx500_res_read(IMX500_COMMAND_GET_SENSOR_DEVICE_ID_2, &dev_id[1], 10);
    imx500_res_read(IMX500_COMMAND_GET_SENSOR_DEVICE_ID_3, &dev_id[2], 10);
    imx500_res_read(IMX500_COMMAND_GET_SENSOR_DEVICE_ID_4, &dev_id[3], 10);
    printf("sensor device id: ");
    for (int i = 0; i < 4; i++) {
        uint32_t v = dev_id[i];

        printf("%02X%02X%02X%02X",
               (v >> 24) & 0xFF,
               (v >> 16) & 0xFF,
               (v >>  8) & 0xFF,
               (v >>  0) & 0xFF);

        if (i != 3) {
            printf("-");
        }
    }
    printf("\n");
}

bool switch_spi_data_forward_mode(spi_data_forwarding_mode_t m) {
    uint32_t t_m = m;
    uint32_t c_m = 0;
    const uint32_t poll_interval_ms = 20;
    const uint32_t timeout_ms = 2000;
    uint32_t elapsed = 0;
    int ret = g_i2c_driver.write(METADATA_SPI_FORWARD_MODE_REG, m, 4);
    if (ret < 0) {
        printf("switch spi data forward mode write failed: %d\n", ret);
        return false;
    }
    while (elapsed < timeout_ms) {
        printf("wait for imx500 module spi data forward mode switching %d ... \n", m);
        g_i2c_driver.slp_ms(poll_interval_ms);
        elapsed += poll_interval_ms;
        ret = g_i2c_driver.read(METADATA_SPI_FORWARD_MODE_REG, &c_m, 4);
        if (ret < 0) {
            printf("switch spi data forward mode read failed: %d\n", ret);
            return false;
        }
        if (c_m == t_m) break;
    }
    if (c_m != t_m) {
        printf("switch spi data forward mode timeout: target=%u current=%u\n",
               (unsigned)t_m, (unsigned)c_m);
        return false;
    }
    printf("wait for imx500 module spi data forward mode: %d switch completed\n", c_m);
    return true;
}

bool get_spi_flash_status(spi_flash_status_t *status) {
    if (!status) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    return sdk_i2c_read_reg(SPI_FLASH_OP_STATUS_REG, &status->status) &&
           sdk_i2c_read_reg(SPI_FLASH_OP_RESULT_REG, &status->result) &&
           sdk_i2c_read_reg(SPI_FLASH_BYTES_DONE_REG, &status->bytes_done) &&
           sdk_i2c_read_reg(SPI_FLASH_BYTES_TOTAL_REG, &status->bytes_total);
}

static bool wait_for_spi_data_forward_mode(spi_data_forwarding_mode_t mode,
                                           uint32_t timeout_ms) {
    uint32_t current_mode = 0;
    uint32_t elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        if (!sdk_i2c_read_reg(METADATA_SPI_FORWARD_MODE_REG, &current_mode)) {
            return false;
        }
        if (current_mode == (uint32_t)mode) {
            return true;
        }
        if (g_i2c_driver.slp_ms) {
            g_i2c_driver.slp_ms(SPI_FLASH_POLL_INTERVAL_MS);
        }
        elapsed_ms += SPI_FLASH_POLL_INTERVAL_MS;
    }
    return false;
}

static bool wait_for_spi_flash_status(uint32_t expected_status,
                                      uint32_t timeout_ms,
                                      spi_flash_status_t *status_out) {
    spi_flash_status_t status = {};
    uint32_t elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        if (!get_spi_flash_status(&status)) {
            return false;
        }
        if (status.status == expected_status) {
            if (status_out) {
                *status_out = status;
            }
            return true;
        }
        if (g_i2c_driver.slp_ms) {
            g_i2c_driver.slp_ms(SPI_FLASH_POLL_INTERVAL_MS);
        }
        elapsed_ms += SPI_FLASH_POLL_INTERVAL_MS;
    }
    return false;
}

static bool wait_for_spi_flash_progress(uint32_t min_bytes_done,
                                        uint32_t timeout_ms,
                                        spi_flash_status_t *status_out) {
    spi_flash_status_t status = {};
    uint32_t elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        if (!get_spi_flash_status(&status)) {
            return false;
        }
        if (status.status == SPI_FLASH_OP_FAILED || status.status == SPI_FLASH_OP_SUCCESS) {
            if (status_out) {
                *status_out = status;
            }
            return status.bytes_done >= min_bytes_done;
        }
        if (status.bytes_done >= min_bytes_done) {
            if (status_out) {
                *status_out = status;
            }
            return true;
        }
        if (g_i2c_driver.slp_ms) {
            g_i2c_driver.slp_ms(SPI_FLASH_POLL_INTERVAL_MS);
        }
        elapsed_ms += SPI_FLASH_POLL_INTERVAL_MS;
    }
    return false;
}

static bool run_spi_flash_transfer(spi_data_forwarding_mode_t mode,
                                   const uint8_t *payload,
                                   uint32_t payload_size,
                                   const char *label) {
    if (!payload || payload_size == 0) {
        printf("%s invalid payload\n", label);
        return false;
    }
    if (!g_spi_driver.write || !g_i2c_driver.read || !g_i2c_driver.write) {
        printf("%s missing spi/i2c driver\n", label);
        return false;
    }
    if (!wait_for_boot_status(1, 10000, label)) {
        return false;
    }

    spi_flash_status_t flash_status = {};
    if (!get_spi_flash_status(&flash_status)) {
        printf("%s read initial flash status failed\n", label);
        return false;
    }
    if (flash_status.status != SPI_FLASH_OP_IDLE &&
        flash_status.status != SPI_FLASH_OP_SUCCESS &&
        flash_status.status != SPI_FLASH_OP_FAILED) {
        printf("%s module flash op busy: status=%u result=%u\n",
               label, (unsigned)flash_status.status, (unsigned)flash_status.result);
        return false;
    }

    if (!switch_spi_data_forward_mode(mode)) {
        printf("%s switch spi forwarding mode failed\n", label);
        return false;
    }
    if (!wait_for_spi_flash_status(SPI_FLASH_OP_WAIT_HEADER,
                                   SPI_FLASH_WAIT_IDLE_TIMEOUT_MS,
                                   &flash_status)) {
        printf("%s wait flash receiver ready timeout, status=%u result=%u\n",
               label, (unsigned)flash_status.status, (unsigned)flash_status.result);
        return false;
    }

    SpiBlobWireHeader header = {};
    header.magic = SPI_BLOB_HEADER_MAGIC;
    header.payload_size = payload_size;
    header.payload_crc32 = calc_crc32_local(payload, payload_size);
    header.reserved = 0;

    if (sdk_spi_write_once(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) < 0) {
        printf("%s send header failed\n", label);
        return false;
    }
    if (g_i2c_driver.slp_ms) {
        g_i2c_driver.slp_ms(SPI_FLASH_HEADER_GAP_MS);
    }

    uint32_t sent = 0;
    while (sent < payload_size) {
        uint32_t chunk = payload_size - sent;
        if (chunk > SPI_FLASH_CHUNK_LEN) {
            chunk = SPI_FLASH_CHUNK_LEN;
        }
        if (sdk_spi_write_once(payload + sent, chunk) < 0) {
            printf("%s send payload failed at offset=%u len=%u\n",
                   label, (unsigned)sent, (unsigned)chunk);
            return false;
        }
        sent += chunk;
        printf("%s sent chunk: %u/%u\n",
               label, (unsigned)sent, (unsigned)payload_size);

        uint32_t chunk_timeout_ms =
            SPI_FLASH_TRANSFER_BASE_TIMEOUT_MS +
            ((chunk + 1023u) / 1024u) * SPI_FLASH_TRANSFER_PER_KB_TIMEOUT_MS;
        if (!wait_for_spi_flash_progress(sent, chunk_timeout_ms, &flash_status)) {
            printf("%s wait progress timeout at %u/%u, status=%u result=%u done=%u total=%u\n",
                   label,
                   (unsigned)sent,
                   (unsigned)payload_size,
                   (unsigned)flash_status.status,
                   (unsigned)flash_status.result,
                   (unsigned)flash_status.bytes_done,
                   (unsigned)flash_status.bytes_total);
            return false;
        }
        printf("%s ack chunk: %u/%u status=%u result=%u\n",
               label,
               (unsigned)flash_status.bytes_done,
               (unsigned)flash_status.bytes_total,
               (unsigned)flash_status.status,
               (unsigned)flash_status.result);
        if (flash_status.status == SPI_FLASH_OP_FAILED) {
            printf("%s failed: result=%u bytes=%u/%u\n",
                   label,
                   (unsigned)flash_status.result,
                   (unsigned)flash_status.bytes_done,
                   (unsigned)flash_status.bytes_total);
            return false;
        }
        if (sent < payload_size && g_i2c_driver.slp_ms) {
            g_i2c_driver.slp_ms(SPI_FLASH_CHUNK_GAP_MS);
        }
    }

    uint32_t timeout_ms = SPI_FLASH_FINALIZE_TIMEOUT_MS;
    uint32_t elapsed_ms = 0;
    uint32_t last_bytes_done = 0xFFFFFFFFu;
    while (elapsed_ms < timeout_ms) {
        if (!get_spi_flash_status(&flash_status)) {
            printf("%s read flash status failed\n", label);
            return false;
        }
        if (flash_status.bytes_done != last_bytes_done) {
            printf("%s device progress: %u/%u status=%u result=%u\n",
                   label,
                   (unsigned)flash_status.bytes_done,
                   (unsigned)flash_status.bytes_total,
                   (unsigned)flash_status.status,
                   (unsigned)flash_status.result);
            last_bytes_done = flash_status.bytes_done;
        }
        if (flash_status.status == SPI_FLASH_OP_SUCCESS) {
            if (flash_status.result != SPI_FLASH_RESULT_OK) {
                printf("%s success state with unexpected result=%u\n",
                       label, (unsigned)flash_status.result);
                return false;
            }
            if (!wait_for_spi_data_forward_mode(SPI_DATA_FORWARDING_NONE,
                                                SPI_FLASH_WAIT_IDLE_TIMEOUT_MS)) {
                printf("%s wait mode back to idle timeout\n", label);
                return false;
            }
            return true;
        }
        if (flash_status.status == SPI_FLASH_OP_FAILED) {
            printf("%s failed: result=%u bytes=%u/%u\n",
                   label,
                   (unsigned)flash_status.result,
                   (unsigned)flash_status.bytes_done,
                   (unsigned)flash_status.bytes_total);
            return false;
        }
        if (g_i2c_driver.slp_ms) {
            g_i2c_driver.slp_ms(SPI_FLASH_POLL_INTERVAL_MS);
        }
        elapsed_ms += SPI_FLASH_POLL_INTERVAL_MS;
    }

    printf("%s timeout waiting flash result after %u ms\n",
           label, (unsigned)timeout_ms);
    return false;
}

bool spi_slave_write_model_to_flash(const uint8_t *model, uint32_t model_size) {
    return run_spi_flash_transfer(SPI_SLAVE_WRITE_MODEL_TO_FLASH,
                                  model,
                                  model_size,
                                  "[SPI FLASH MODEL]");
}

bool spi_slave_write_nn_info_to_flash(const uint8_t *nn_info, uint32_t nn_info_size) {
    return run_spi_flash_transfer(SPI_SLAVE_WRITE_NN_INFO_TO_FLASH,
                                  nn_info,
                                  nn_info_size,
                                  "[SPI FLASH NN_INFO]");
}

static int rp2350_send_fw_to_imx500_sspi(const uint8_t *data, uint32_t len) {
    const uint32_t download_sts_poll_interval_ms = 10;
    const uint32_t download_sts_timeout_ms = 3000;
    uint32_t div_num = len / IMX500_MAX_BUFFER;
    printf("fw size: %u\n", (unsigned)len);
    printf("fw division: %u\n", (unsigned)div_num);

    uint32_t all_division_num = (len + IMX500_MAX_BUFFER - 1) / IMX500_MAX_BUFFER;
    const uint8_t *div_data_remain = data + (div_num * IMX500_MAX_BUFFER);
    uint32_t remain_data_len = len - div_num * IMX500_MAX_BUFFER;

    uint32_t step = 0;
    uint32_t total = all_division_num;
    for (uint32_t i = 0; i < div_num; ++i) {
        if (sdk_spi_write_bridge_paced(data + (i * IMX500_MAX_BUFFER),
                                       IMX500_MAX_BUFFER,
                                       SPI_FW_BRIDGE_BLOCK_GAP_US) < 0) {
            printf("spi write failed on firmware chunk %u/%u\n", (unsigned)(i + 1), (unsigned)div_num);
            return -1;
        }
        step += 1;
        uint32_t val = 0;
        log_progress(NULL, "[Download Firmware]", step, total, 30);
        uint32_t waited_ms = 0;
        while (1) {
            imx500_err_t cmd_ret = imx500_res_read(IMX500_COMMAND_TICK_DD_DOWNLOAD_STS, &val, 50);
            if (cmd_ret != IMX500_CMD_OK) {
                printf("read DD_DOWNLOAD_STS failed: %d\n", (int)cmd_ret);
                return -1;
            }
            uint32_t DD_DOWNLOAD_STS = val;
            printf("DD_DOWNLOAD_STS = %u\n", (unsigned)DD_DOWNLOAD_STS);
            if (DD_DOWNLOAD_STS != 1) break;
            if (waited_ms >= download_sts_timeout_ms) {
                printf("wait DD_DOWNLOAD_STS timeout after %u ms\n", (unsigned)waited_ms);
                return -1;
            }
            g_i2c_driver.slp_ms(download_sts_poll_interval_ms);
            waited_ms += download_sts_poll_interval_ms;
        }
    }
    if (remain_data_len > 0 &&
        sdk_spi_write_bridge_paced(div_data_remain,
                                   remain_data_len,
                                   SPI_FW_BRIDGE_BLOCK_GAP_US) < 0) {
        printf("spi write failed on firmware tail, len=%u\n", (unsigned)remain_data_len);
        return -1;
    }
    step += 1;
    log_progress(NULL, "[Download Firmware]", step, total, 30);

    uint32_t tail = len % SPI_BRIDGE_BLOCK_LEN;
    if (tail != 0) {
        uint8_t pad[SPI_BRIDGE_BLOCK_LEN] = {0};
        uint32_t pad_len = SPI_BRIDGE_BLOCK_LEN - tail;
        printf("fw tail=%u, pad=%u for SPI bridge flush\n", (unsigned)tail, (unsigned)pad_len);
        if (sdk_spi_write_bridge_paced(pad, pad_len, SPI_FW_BRIDGE_BLOCK_GAP_US) < 0) {
            printf("spi write failed on firmware flush padding, len=%u\n", (unsigned)pad_len);
            return -1;
        }
    }

    return 0;
}

int load_imx500_fw(const uint8_t *fw, uint32_t size, uint32_t fw_type) {
    const uint32_t wait_short_ms = 100;
    const uint32_t wait_dd_reply_ms = 3000;
    uint32_t val = 0;
    if (!fw || size == 0u) {
        printf("load_imx500_fw invalid input\n");
        return -1;
    }
    if (imx500_res_read(IMX500_COMMAND_PREPARE_DOWNLOAD_FIRMWARE, &val, 500) != IMX500_CMD_OK) {
        printf("prepare download firmware failed\n");
        return -1;
    }
    if (imx500_res_read(IMX500_COMMAND_TICK_DD_CMD_REPLY_STS_CNT, &val, wait_short_ms) != IMX500_CMD_OK) {
        printf("read DD_CMD_REPLY_STS_CNT failed before download\n");
        return -1;
    }
    uint32_t DD_CMD_REPLY_STS_CNT = val;
    printf("DD_CMD_REPLY_STS_CNT = %x\n", DD_CMD_REPLY_STS_CNT);

    uint32_t division = size / IMX500_MAX_BUFFER;
    if (imx500_res_write(IMX500_COMMAND_BEFORE_DOWNLOAD_FIRMWARE_1, &fw_type, wait_short_ms) != IMX500_CMD_OK ||
        imx500_res_write(IMX500_COMMAND_BEFORE_DOWNLOAD_FIRMWARE_2, &division, wait_short_ms) != IMX500_CMD_OK ||
        imx500_res_write(IMX500_COMMAND_BEFORE_DOWNLOAD_FIRMWARE_3, &size, wait_short_ms) != IMX500_CMD_OK ||
        imx500_res_write(IMX500_COMMAND_BEFORE_DOWNLOAD_FIRMWARE_4, &fw_type, wait_short_ms) != IMX500_CMD_OK) {
        printf("before download firmware setup failed\n");
        return -1;
    }
    if (imx500_res_write(IMX500_COMMAND_WAIT_DD_REPLY_SYS_CNT_CHANGE, &DD_CMD_REPLY_STS_CNT, wait_dd_reply_ms) != IMX500_CMD_OK) {
        printf("wait DD reply (ready) failed\n");
        return -1;
    }

    if (imx500_res_read(IMX500_COMMAND_TICK_DD_CMD_REPLY_STS_CNT, &val, wait_short_ms) != IMX500_CMD_OK) {
        printf("read DD_CMD_REPLY_STS_CNT failed after ready wait\n");
        return -1;
    }
    DD_CMD_REPLY_STS_CNT = val;
    printf("DD_CMD_REPLY_STS_CNT = %x\n", DD_CMD_REPLY_STS_CNT);
    g_i2c_driver.slp_ms(10);

    if (imx500_res_read(IMX500_COMMAND_TICK_DD_CMD_REPLY_STS, &val, wait_short_ms) != IMX500_CMD_OK) {
        printf("read DD_CMD_REPLY_STS failed at ready stage\n");
        return -1;
    }
    uint32_t DD_CMD_REPLY_STS = val;
    printf("DD_CMD_REPLY_STS = %x: %s\n", DD_CMD_REPLY_STS, get_imx500_cmd_status(DD_CMD_REPLY_STS));
    if (DD_CMD_REPLY_STS != 0x00) {
        printf("DD_CMD_REPLY_STS is not ready: %s\n", get_imx500_cmd_status(DD_CMD_REPLY_STS));
        return -1;
    }

    std::vector<uint8_t> swapped_fw = byteswap_u32_words(fw, size);
    if (swapped_fw.size() != size) {
        printf("prepare byteswapped firmware failed size=%u\n", (unsigned)size);
        return -1;
    }

    if (rp2350_send_fw_to_imx500_sspi(swapped_fw.data(), size) != 0) {
        printf("send firmware by SSPI failed\n");
        return -1;
    }
    g_i2c_driver.slp_ms(10);

    if (imx500_res_write(IMX500_COMMAND_WAIT_DD_REPLY_SYS_CNT_CHANGE, &DD_CMD_REPLY_STS_CNT, wait_dd_reply_ms) != IMX500_CMD_OK) {
        printf("wait DD reply (done) failed\n");
        return -1;
    }
    if (imx500_res_read(IMX500_COMMAND_TICK_DD_CMD_REPLY_STS_CNT, &val, wait_short_ms) != IMX500_CMD_OK) {
        printf("read DD_CMD_REPLY_STS_CNT failed after done wait\n");
        return -1;
    }
    DD_CMD_REPLY_STS_CNT = val;
    if (imx500_res_read(IMX500_COMMAND_TICK_DD_CMD_REPLY_STS, &val, wait_short_ms) != IMX500_CMD_OK) {
        printf("read DD_CMD_REPLY_STS failed at done stage\n");
        return -1;
    }
    DD_CMD_REPLY_STS = val;
    printf("DD_CMD_REPLY_STS = %x: %s\n", DD_CMD_REPLY_STS, get_imx500_cmd_status(DD_CMD_REPLY_STS));
    if (DD_CMD_REPLY_STS != 0x01) {
        printf("DD_CMD_REPLY_STS is not done: %s\n", get_imx500_cmd_status(DD_CMD_REPLY_STS));
        return -1;
    }
    return 0;
}

void stream_on() {
    uint32_t val = 0;
    imx500_res_read(IMX500_COMMAND_STREAM_ON, &val, 10);
    g_i2c_driver.slp_ms(10);
}

int32_t calculate_spi_output_metadata_size(spi_data_format_t f, uint32_t *data_size) {

  // dump_s_nw_info_list();
  // init_input_tensor_preprocess_config();

  uint32_t r_buff_offset = 0;
  const sc_dnn_nw_info_t* net = &network_info[0];

  int input_tensor_data_size =
      net->dnnHeaderSize + calc_align(net->inputTensorHeight, 2) *
                               calc_align(net->inputTensorWidth, 32) * 3;
  int output_tensor_data_size = 0;
  output_tensor_data_size += net->dnnHeaderSize;
  for (int j = 0; j < (int)net->outputTensorNum; j++) {
    int size =
        ((net->outputTensorBytesPerElement[j] *
              (net->outputTensorPadding[j] + net->outputTensorDimSize[j]) +
          3) /
         4) *
        4;
    output_tensor_data_size += size;
  }
  // uint32_t data_size = input_tensor_data_size + output_tensor_data_size;
  switch (f) {
  case SPI_METADATA_OUTPUT_TENSOR:
    *data_size = output_tensor_data_size;
    break;
  case SPI_METADATA_INPUT_TENSOR:
    *data_size = input_tensor_data_size;
    break;
  case SPI_METADATA_JPEG_INPUT_TENSOR:
    // TODO:
    break;
  case SPI_METADATA_INPUT_TENSOR_OUTPUT_TENSOR:
    *data_size = input_tensor_data_size + output_tensor_data_size;
    break;
  case SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR:
    // TODO:
    break;
  case SPI_METADATA_NONE:
    *data_size = 0;
    break;
  default:
    break;
  }
  return 0;
}

bool open(const uint8_t *nn_fw, uint32_t nn_fw_size, const uint8_t* nn_info, uint32_t nn_info_size, mipi_data_format_t mipi_format, spi_data_format_t spi_format) {
    uint32_t imx500_boot_status = 0;
    const uint32_t boot_timeout_ms = 10000;
    const uint32_t boot_poll_ms = 100;
    uint32_t boot_elapsed = 0;
    while (boot_elapsed < boot_timeout_ms) {
        int ret = g_i2c_driver.read(BOOT_STATUS_REG, &imx500_boot_status, 4);
        if (ret < 0) {
            printf("read boot status failed: %d\n", ret);
            return false;
        }
        if (imx500_boot_status == 1) break;
        printf("wait for imx500 module boot ... \n");
        g_i2c_driver.slp_ms(boot_poll_ms);
        boot_elapsed += boot_poll_ms;
    }
    if (imx500_boot_status != 1) {
        printf("wait imx500 boot timeout after %u ms\n", (unsigned)boot_elapsed);
        return false;
    }
    printf("imx500 module boot completed\n");
    imx500_dump_basic_info();
    uint32_t spi_frq = 5 * 1000 * 1000;
    imx500_res_write(IMX500_COMMAND_SET_SPI_FRQ, &spi_frq, 10);

    switch_spi_data_forward_mode(SPI_SLAVE_TO_IMX500_SSPI);
    uint32_t data_size;
    if (nn_fw != nullptr) {
        if (load_imx500_fw(nn_fw, nn_fw_size, IMX500_FW_TYPE_NETWORK_WEIGHTS) != 0) {
            printf("Error: nn fw failed\n");
            return false;
        }
        printf("spi write nn fw completed\n");
        set_nw_info_from_flash_buffer(nn_info, nn_info_size);
        dump_network_info_list();
        if (init_input_tensor_preprocess_config() != 0) {
            printf("Error: init_input_tensor_preprocess_config failed\n");
            return false;
        }
        calculate_spi_output_metadata_size(spi_format, &data_size);
        printf("data_size: %d\n", data_size);
        g_i2c_driver.write(METADATA_SIZE_REG, data_size, 4);
    } else {
        const uint32_t load_nn_timeout_ms = 20000;
        const uint32_t load_nn_poll_ms = 500;
        uint32_t load_nn_elapsed = 0;
        int ret = g_i2c_driver.write(LOAD_MODEL_FROM_FLASH, 1, 4);
        while (load_nn_elapsed < load_nn_timeout_ms) {
            int ret = g_i2c_driver.read(BOOT_STATUS_REG, &imx500_boot_status, 4);
            if (ret < 0) {
                printf("read boot status failed: %d\n", ret);
                return false;
            }
            if (imx500_boot_status == 2) break;
            printf("wait for loading nn ... \n");
            g_i2c_driver.slp_ms(load_nn_poll_ms);
            load_nn_elapsed += load_nn_poll_ms;
        }
    }

    uint32_t val;
    imx500_res_read(IMX500_COMMAND_SENSOR_DEFAULT_CONFIG, &val, 500);
    imx500_res_read(IMX500_COMMAND_SENSOR_MIPI_COMMON_RAW10_2LANES_CONFIG, &val, 500);
    imx500_res_read(IMX500_COMMAND_SENSOR_MIPI_1024x600_2LANES_CONFIG, &val, 500);
    switch (mipi_format)
    {
    case MIPI_DATA_IMAGE:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_MIPI_DATA_IMAGE, &val, 500);
        break;
    case MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_MIPI_DATA_METADATA_INPUT_TENSOR_OUTPUT_TENSOR, &val, 500);
        break;
    case MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_MIPI_DATA_IMAGE_METADATA_INPUT_TENSOR_OUTPUT_TENSOR, &val, 500);
        break;
    case MIPI_DATA_NONE:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_MIPI_DATA_NONE, &val, 500);
        break;
    default:
        printf("Error: invalid mipi format");
        break;
    }
    
    switch (spi_format)
    {
    case SPI_METADATA_OUTPUT_TENSOR:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_SPI_METADATA_OUTPUT_TENSOR, &val, 10);
        switch_spi_data_forward_mode(SPI_SLAVE_FROM_IMX500_MSPI);
        break;
    case SPI_METADATA_INPUT_TENSOR:
        printf("Not yet implemented(SPI_METADATA_INPUT_TENSOR), disabled.\n");
        break;
    case SPI_METADATA_JPEG_INPUT_TENSOR:
        printf("Not yet implemented(SPI_METADATA_JPEG_INPUT_TENSOR), disabled.\n");
        break;
    case SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_SPI_METADATA_JPEG_INPUT_TENSOR_OUTPUT_TENSOR, &val, 10);
        switch_spi_data_forward_mode(SPI_SLAVE_FROM_IMX500_SSPI);
        break;
    case SPI_METADATA_NONE:
        imx500_res_read(IMX500_COMMAND_SET_FORMAT_SPI_METADATA_NONE, &val, 10);
        break;
    default:
        printf("Error: invalid spi format");
        break;
    }

    return true;
}

uint32_t get_metadata_size(void) {
    uint32_t data_size = 0;
    if (!g_i2c_driver.read(METADATA_SIZE_REG, &data_size, 4)) {
        printf("Error: Failed to read METADATA_SIZE_REG\n");
        return 0;
    }
    printf("data_size: %u\n", data_size);
    return data_size;
}

int32_t read_metadata(uint8_t *rx_buf, uint32_t buf_size) {
    uint8_t ready_status;
    int ret;

    printf("Waiting for data ready...\n");
    bool data_ready = false;
    uint32_t is_metadata_ready_;

    while (1) {
        ret = g_i2c_driver.read(DATA_READY_STATUS_REG, &is_metadata_ready_, 4);
        ready_status = is_metadata_ready_&0xff;
        if (!ret) {
            printf("Error: Failed to read DATA_READY register\n");
            return 0;
        }
        
        if (ready_status == 0x01) {
            data_ready = true;
            printf("Data ready! (status: 0x%02X)\n", ready_status);
            break;
        }
        
        // Small delay between polls
        g_i2c_driver.slp_ms(10);
    }
    
    if (!data_ready) {
        printf("Timeout: Data not ready after 1 second (last status: 0x%02X)\n", ready_status);
        return 0;
    }

    printf("Data ready, starting SPI DMA read...\n");

    uint32_t data_size = get_metadata_size();

    if (data_size == 0) {
        printf("Error: Invalid data size\n");
        return false;
    }

    if (data_size > buf_size) {
        printf("Error: data_size > buf_size\n");
        return false;
    }

    g_spi_driver.read(rx_buf, data_size);
    return data_size;
}

int _preprocess_nn_input_data(uint8_t *src, uint32_t src_size) {
    if (!src) {
        return -1;
    }
    if (src_size == 0 || (src_size % 3u) != 0u) {
        return -1;
    }
    if (s_num_of_networks == 0) {
        return 0;
    }

    sc_input_norm_info_t norm_info;
    build_input_norm_info(&network_info[0], &norm_info);

    for (uint32_t i = 0; i < src_size; i += 3u) {
        for (int c = 0; c < 3; ++c) {
            const int32_t den = (norm_info.div_val[c] == 0) ? 1 : norm_info.div_val[c];
            int64_t num = (((int64_t)src[i + (uint32_t)c] << norm_info.norm_shift[c]) - norm_info.norm_val[c]);
            num = num << norm_info.div_shift;
            const int32_t out = (int32_t)(num / den);
            src[i + (uint32_t)c] = (uint8_t)(out & 0xFF);
        }
    }
    return 0;
}

int _convert_injected_data(const uint8_t *img,
                           uint32_t img_width, uint32_t img_height, uint32_t img_channels,
                           uint8_t *dst, uint32_t dst_size,
                           uint32_t input_height, uint32_t input_width, uint32_t channel_num,
                           const uint8_t transpose_order[3], uint32_t align_base) {
    if (!img || !dst) {
        return -1;
    }
    if (img_width == 0 || img_height == 0 || img_channels == 0) {
        return -1;
    }
    if (input_height == 0 || input_width == 0 || channel_num == 0) {
        return -1;
    }
    if (img_channels < channel_num) {
        return -1;
    }
    if (align_base == 0) {
        align_base = 32;
    }

    const uint32_t input_width_aligned = (uint32_t)ALIGN_UP(input_width, align_base);
    const uint64_t total_u64 = (uint64_t)input_height * (uint64_t)input_width_aligned * (uint64_t)channel_num;
    if (total_u64 > 0xFFFFFFFFu) {
        return -1;
    }
    const uint32_t total = (uint32_t)total_u64;
    if (dst_size < total) {
        return -1;
    }

    uint8_t order_local[3] = {2, 0, 1};
    const uint8_t *order = transpose_order ? transpose_order : order_local;
    bool seen[3] = {false, false, false};
    for (int i = 0; i < 3; ++i) {
        if (order[i] > 2) {
            return -1;
        }
        if (seen[order[i]]) {
            return -1;
        }
        seen[order[i]] = true;
    }

    uint8_t *input_tensor_final = (uint8_t *)malloc(total);
    if (!input_tensor_final) {
        return -1;
    }
    memset(input_tensor_final, 0, total);

    for (uint32_t y = 0; y < input_height; ++y) {
        uint32_t src_y = (uint32_t)(((uint64_t)y * (uint64_t)img_height) / (uint64_t)input_height);
        if (src_y >= img_height) {
            src_y = img_height - 1;
        }
        for (uint32_t x = 0; x < input_width; ++x) {
            uint32_t src_x = (uint32_t)(((uint64_t)x * (uint64_t)img_width) / (uint64_t)input_width);
            if (src_x >= img_width) {
                src_x = img_width - 1;
            }
            const uint32_t src_base = (src_y * img_width + src_x) * img_channels;
            const uint32_t dst_base = (y * input_width_aligned + x) * channel_num;
            for (uint32_t c = 0; c < channel_num; ++c) {
                input_tensor_final[dst_base + c] = img[src_base + c];
            }
        }
    }

    const uint32_t dims[3] = {input_height, input_width_aligned, channel_num};
    const uint32_t out_dims[3] = {dims[order[0]], dims[order[1]], dims[order[2]]};
    for (uint32_t h = 0; h < input_height; ++h) {
        for (uint32_t w = 0; w < input_width_aligned; ++w) {
            for (uint32_t c = 0; c < channel_num; ++c) {
                const uint32_t src_idx = (h * input_width_aligned + w) * channel_num + c;
                const uint32_t src_coord[3] = {h, w, c};
                const uint32_t o0 = src_coord[order[0]];
                const uint32_t o1 = src_coord[order[1]];
                const uint32_t o2 = src_coord[order[2]];
                const uint32_t dst_idx = (o0 * out_dims[1] + o1) * out_dims[2] + o2;
                dst[dst_idx] = input_tensor_final[src_idx];
            }
        }
    }

    free(input_tensor_final);
    return (int)total;
}

void do_data_injection_stream(
    data_provider_t provider,
    uint32_t total_size,
    bool first_time
) {
    static uint8_t inject_buf[4096];
    uint32_t val = 0;

    if (first_time) {
        imx500_res_read(IMX500_COMMAND_SWITCH_TO_DATA_INJECTION_MODE, &val, 10);
    }

    imx500_res_read(IMX500_COMMAND_BEFORE_DATA_INJECTION, &val, 10);

    uint32_t offset = 0;
    uint32_t step = 0;
    uint32_t total = (total_size + MAX_SPI_PACKET_LEN - 1) / MAX_SPI_PACKET_LEN;

    while (offset < total_size) {
        uint32_t to_read =
            (total_size - offset > sizeof(inject_buf))
            ? (uint32_t)sizeof(inject_buf)
            : (total_size - offset);

        uint32_t got = provider(inject_buf, to_read, offset);
        if (got == 0) {
            printf("[Data Injection] provider failed at offset=%u\n", (unsigned)offset);
            break;
        }

        uint32_t sent = 0;
        while (sent < got) {
            uint32_t pkt =
                (got - sent > MAX_SPI_PACKET_LEN)
                ? MAX_SPI_PACKET_LEN
                : (got - sent);

            sdk_spi_write(inject_buf + sent, pkt);
            sent += pkt;

            step++;
            log_progress(NULL, "[Data Injection]", step, total, 30);
        }

        offset += got;
    }
    printf("data injection completed\n");
}

void do_data_injection(const uint8_t *data, uint32_t size, bool first_time) {
    if (!data || size == 0) {
        printf("[Data Injection] invalid input\n");
        return;
    }

    uint32_t val = 0;
    if (first_time) {
        imx500_res_read(IMX500_COMMAND_SWITCH_TO_DATA_INJECTION_MODE, &val, 10);
    }

    imx500_res_read(IMX500_COMMAND_BEFORE_DATA_INJECTION, &val, 10);

    uint32_t offset = 0;
    uint32_t step = 0;
    uint32_t total = (size + MAX_SPI_PACKET_LEN - 1) / MAX_SPI_PACKET_LEN;

    while (offset < size) {
        uint32_t pkt =
            (size - offset > MAX_SPI_PACKET_LEN)
            ? MAX_SPI_PACKET_LEN
            : (size - offset);

        sdk_spi_write(data + offset, pkt);
        offset += pkt;

        step++;
        log_progress(NULL, "[Data Injection]", step, total, 30);
    }
    printf("data injection completed\n");
}

void stop_data_injection(void) {
    uint32_t val;
    imx500_res_read(IMX500_COMMAND_AFTER_DATA_INJECTION, &val, 10);
}

void get_fw_ver(uint32_t* v) {
    g_i2c_driver.read(DEVICE_VERSION_REG, v, 4);
}

void get_pid(uint32_t* v) {
    g_i2c_driver.read(DEVICE_ID_REG, v, 4);
}

int sensor_i2c_write_16_8(uint16_t reg_addr, uint8_t data) {
    if (!g_i2c_driver.write) {
        return -1;
    }
    uint32_t packed = SENSOR_I2C_16_8_PACK(reg_addr, data);
    int ret = g_i2c_driver.write(SENSOR_WR_REG, packed, 4);
    g_i2c_driver.slp_ms(1);
    return (ret < 0) ? -1 : 0;
}

int sensor_i2c_read_16_8(uint16_t reg_addr, uint8_t *data) {
    if (!data || !g_i2c_driver.write || !g_i2c_driver.read) {
        return -1;
    }

    uint32_t req = SENSOR_I2C_16_8_ADDR(reg_addr);
    int ret = g_i2c_driver.write(SENSOR_RD_REG, req, 4);
    g_i2c_driver.slp_ms(1);
    if (ret < 0) {
        return -1;
    }

    uint32_t rsp = 0;
    ret = g_i2c_driver.read(SENSOR_RD_REG, &rsp, 4);
    if (ret < 0) {
        return -1;
    }

    *data = SENSOR_I2C_16_8_DATA(rsp);
    return 0;
}

int sensor_i2c_write_16_16(uint16_t reg_addr, uint16_t data) {
    if (sensor_i2c_write_16_8(reg_addr, (uint8_t)(data & 0xFFu)) < 0) {
        return -1;
    }
    if (sensor_i2c_write_16_8((uint16_t)(reg_addr + 1u), (uint8_t)((data >> 8) & 0xFFu)) < 0) {
        return -1;
    }
    return 0;
}

int sensor_i2c_read_16_16(uint16_t reg_addr, uint16_t *data) {
    if (!data) {
        return -1;
    }
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    if (sensor_i2c_read_16_8(reg_addr, &b0) < 0) {
        return -1;
    }
    if (sensor_i2c_read_16_8((uint16_t)(reg_addr + 1u), &b1) < 0) {
        return -1;
    }
    *data = (uint16_t)((uint16_t)b0 | ((uint16_t)b1 << 8));
    return 0;
}

int sensor_i2c_write_16_32(uint16_t reg_addr, uint32_t data) {
    if (sensor_i2c_write_16_8(reg_addr, (uint8_t)(data & 0xFFu)) < 0) {
        return -1;
    }
    if (sensor_i2c_write_16_8((uint16_t)(reg_addr + 1u), (uint8_t)((data >> 8) & 0xFFu)) < 0) {
        return -1;
    }
    if (sensor_i2c_write_16_8((uint16_t)(reg_addr + 2u), (uint8_t)((data >> 16) & 0xFFu)) < 0) {
        return -1;
    }
    if (sensor_i2c_write_16_8((uint16_t)(reg_addr + 3u), (uint8_t)((data >> 24) & 0xFFu)) < 0) {
        return -1;
    }
    return 0;
}

int sensor_i2c_read_16_32(uint16_t reg_addr, uint32_t *data) {
    if (!data) {
        return -1;
    }
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    uint8_t b3 = 0;
    if (sensor_i2c_read_16_8(reg_addr, &b0) < 0) {
        return -1;
    }
    if (sensor_i2c_read_16_8((uint16_t)(reg_addr + 1u), &b1) < 0) {
        return -1;
    }
    if (sensor_i2c_read_16_8((uint16_t)(reg_addr + 2u), &b2) < 0) {
        return -1;
    }
    if (sensor_i2c_read_16_8((uint16_t)(reg_addr + 3u), &b3) < 0) {
        return -1;
    }
    *data = ((uint32_t)b0) |
            ((uint32_t)b1 << 8) |
            ((uint32_t)b2 << 16) |
            ((uint32_t)b3 << 24);
    return 0;
}

}
