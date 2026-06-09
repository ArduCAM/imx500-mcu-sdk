#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "apParams_verifier.h"
#include "test_metadata_outputtensor_1.h"

#define IMX500_HEADER_LEN 12u
#define OUTPUT_TENSOR_ALIGNMENT 4u

typedef struct {
    uint8_t valid_flag;
    uint8_t frame_count;
    uint16_t max_length_of_line;
    uint16_t size_of_ap_parameter;
    uint16_t network_ordinal;
    uint8_t indicator;
} imx500_output_header_t;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void unpack_imx500_output_header(const uint8_t *data, imx500_output_header_t *header)
{
    header->valid_flag = data[0];
    header->frame_count = data[1];
    header->max_length_of_line = read_le16(data + 2);
    header->size_of_ap_parameter = read_le16(data + 4);
    header->network_ordinal = read_le16(data + 6);
    header->indicator = data[8];
}

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return ((value + alignment - 1u) / alignment) * alignment;
}

static const char *fb_string_or_empty(flatbuffers_string_t value)
{
    return value ? value : "";
}

static uint32_t tensor_dimensions_product(apParams_fb_FBDimension_vec_t dimensions)
{
    uint32_t product = 1u;
    size_t len = apParams_fb_FBDimension_vec_len(dimensions);

    if (len == 0u) {
        return 0u;
    }

    for (size_t i = 0; i < len; ++i) {
        apParams_fb_FBDimension_table_t dim = apParams_fb_FBDimension_vec_at(dimensions, i);
        product *= apParams_fb_FBDimension_size(dim);
    }
    return product;
}

static void print_header(const imx500_output_header_t *header)
{
    printf("IMX500 header\n");
    printf("  valid_flag            : %u\n", header->valid_flag);
    printf("  frame_count           : %u\n", header->frame_count);
    printf("  max_length_of_line    : %u\n", header->max_length_of_line);
    printf("  size_of_ap_parameter  : %u\n", header->size_of_ap_parameter);
    printf("  network_ordinal       : %u\n", header->network_ordinal);
    printf("  indicator             : %u\n", header->indicator);
}

static void print_dimensions(apParams_fb_FBDimension_vec_t dimensions, const char *indent)
{
    size_t len = apParams_fb_FBDimension_vec_len(dimensions);

    printf("%sdimensions[%zu]\n", indent, len);
    for (size_t i = 0; i < len; ++i) {
        apParams_fb_FBDimension_table_t dim = apParams_fb_FBDimension_vec_at(dimensions, i);
        printf("%s  [%zu] id=%u size=%u serializationIndex=%u padding=%u\n",
               indent,
               i,
               apParams_fb_FBDimension_id(dim),
               apParams_fb_FBDimension_size(dim),
               apParams_fb_FBDimension_serializationIndex(dim),
               apParams_fb_FBDimension_padding(dim));
    }
}

static void print_input_tensor(apParams_fb_FBInputTensor_table_t tensor, size_t index)
{
    apParams_fb_FBDimension_vec_t dimensions = apParams_fb_FBInputTensor_dimensions(tensor);

    printf("    inputTensors[%zu]\n", index);
    printf("      id                : %u\n", apParams_fb_FBInputTensor_id(tensor));
    printf("      name              : %s\n",
           fb_string_or_empty(apParams_fb_FBInputTensor_name(tensor)));
    printf("      numOfDimensions   : %u\n", apParams_fb_FBInputTensor_numOfDimensions(tensor));
    printf("      shift             : %u\n", apParams_fb_FBInputTensor_shift(tensor));
    printf("      scale             : %.9g\n", apParams_fb_FBInputTensor_scale(tensor));
    printf("      format            : %u\n", apParams_fb_FBInputTensor_format(tensor));
    printf("      element_count     : %" PRIu32 "\n", tensor_dimensions_product(dimensions));
    print_dimensions(dimensions, "      ");
}

static uint32_t output_tensor_data_bytes(apParams_fb_FBOutputTensor_table_t tensor)
{
    uint32_t element_count =
        tensor_dimensions_product(apParams_fb_FBOutputTensor_dimensions(tensor));
    uint32_t bits_per_element = apParams_fb_FBOutputTensor_bitsPerElement(tensor);
    uint32_t bytes_per_element = (bits_per_element + 7u) / 8u;

    return element_count * bytes_per_element;
}

static uint32_t print_output_tensor(apParams_fb_FBOutputTensor_table_t tensor, size_t index)
{
    apParams_fb_FBDimension_vec_t dimensions = apParams_fb_FBOutputTensor_dimensions(tensor);
    uint32_t data_bytes = output_tensor_data_bytes(tensor);
    uint32_t aligned_data_bytes = align_up_u32(data_bytes, OUTPUT_TENSOR_ALIGNMENT);

    printf("    outputTensors[%zu]\n", index);
    printf("      id                : %u\n", apParams_fb_FBOutputTensor_id(tensor));
    printf("      name              : %s\n",
           fb_string_or_empty(apParams_fb_FBOutputTensor_name(tensor)));
    printf("      numOfDimensions   : %u\n", apParams_fb_FBOutputTensor_numOfDimensions(tensor));
    printf("      bitsPerElement    : %u\n", apParams_fb_FBOutputTensor_bitsPerElement(tensor));
    printf("      shift             : %u\n", apParams_fb_FBOutputTensor_shift(tensor));
    printf("      scale             : %.9g\n", apParams_fb_FBOutputTensor_scale(tensor));
    printf("      format            : %u\n", apParams_fb_FBOutputTensor_format(tensor));
    printf("      element_count     : %" PRIu32 "\n", tensor_dimensions_product(dimensions));
    printf("      data_bytes        : %" PRIu32 "\n", data_bytes);
    printf("      aligned_data_bytes: %" PRIu32 "\n", aligned_data_bytes);
    print_dimensions(dimensions, "      ");

    return aligned_data_bytes;
}

static size_t find_apparams_offset(const uint8_t *data, size_t data_len)
{
    imx500_output_header_t header;

    if (data_len >= IMX500_HEADER_LEN) {
        unpack_imx500_output_header(data, &header);
        if (header.size_of_ap_parameter > 0u &&
            IMX500_HEADER_LEN + header.size_of_ap_parameter <= data_len &&
            apParams_fb_FBApParams_verify_as_root(data + IMX500_HEADER_LEN,
                                                  header.size_of_ap_parameter) ==
                flatcc_verify_ok) {
            return IMX500_HEADER_LEN;
        }
    }

    for (size_t offset = 0; offset + sizeof(flatbuffers_uoffset_t) <= data_len; ++offset) {
        if (apParams_fb_FBApParams_verify_as_root(data + offset, data_len - offset) ==
            flatcc_verify_ok) {
            return offset;
        }
    }

    return data_len;
}

int main(void)
{
    const uint8_t *metadata = output_bin;
    const size_t metadata_len = output_bin_len;
    imx500_output_header_t header;
    size_t ap_offset;
    size_t ap_size;
    apParams_fb_FBApParams_table_t ap_params;
    apParams_fb_FBNetwork_vec_t networks;
    uint32_t expected_output_payload_bytes = 0u;

    printf("metadata bytes: %zu\n\n", metadata_len);

    if (metadata_len < IMX500_HEADER_LEN) {
        fprintf(stderr, "metadata is too short for an IMX500 header\n");
        return 1;
    }

    unpack_imx500_output_header(metadata, &header);
    print_header(&header);

    ap_offset = find_apparams_offset(metadata, metadata_len);
    if (ap_offset == metadata_len) {
        fprintf(stderr, "could not find a valid apParams.fb.FBApParams FlatBuffer\n");
        return 1;
    }

    ap_size = metadata_len - ap_offset;
    if (ap_offset == IMX500_HEADER_LEN && header.size_of_ap_parameter > 0u) {
        ap_size = header.size_of_ap_parameter;
    }

    printf("\nFlatBuffer\n");
    printf("  offset                : %zu\n", ap_offset);
    printf("  size                  : %zu\n", ap_size);

    int verify_status = apParams_fb_FBApParams_verify_as_root(metadata + ap_offset, ap_size);
    printf("  verify                : %s (%d)\n",
           flatcc_verify_error_string(verify_status),
           verify_status);
    if (verify_status != flatcc_verify_ok) {
        return 1;
    }

    ap_params = apParams_fb_FBApParams_as_root(metadata + ap_offset);
    networks = apParams_fb_FBApParams_networks(ap_params);

    printf("\nFBApParams\n");
    printf("  networks[%zu]\n", apParams_fb_FBNetwork_vec_len(networks));

    for (size_t network_index = 0; network_index < apParams_fb_FBNetwork_vec_len(networks);
         ++network_index) {
        apParams_fb_FBNetwork_table_t network =
            apParams_fb_FBNetwork_vec_at(networks, network_index);
        apParams_fb_FBInputTensor_vec_t input_tensors =
            apParams_fb_FBNetwork_inputTensors(network);
        apParams_fb_FBOutputTensor_vec_t output_tensors =
            apParams_fb_FBNetwork_outputTensors(network);

        printf("  networks[%zu]\n", network_index);
        printf("    id                  : %u\n", apParams_fb_FBNetwork_id(network));
        printf("    name                : %s\n",
               fb_string_or_empty(apParams_fb_FBNetwork_name(network)));
        printf("    type                : %s\n",
               fb_string_or_empty(apParams_fb_FBNetwork_type(network)));
        printf("    inputTensorsLength  : %zu\n",
               apParams_fb_FBInputTensor_vec_len(input_tensors));
        printf("    outputTensorsLength : %zu\n",
               apParams_fb_FBOutputTensor_vec_len(output_tensors));

        for (size_t i = 0; i < apParams_fb_FBInputTensor_vec_len(input_tensors); ++i) {
            print_input_tensor(apParams_fb_FBInputTensor_vec_at(input_tensors, i), i);
        }

        for (size_t i = 0; i < apParams_fb_FBOutputTensor_vec_len(output_tensors); ++i) {
            expected_output_payload_bytes +=
                print_output_tensor(apParams_fb_FBOutputTensor_vec_at(output_tensors, i), i);
        }
    }

    printf("\nOutput payload estimate\n");
    printf("  offset                : %zu\n", ap_offset + ap_size);
    printf("  expected bytes        : %" PRIu32 "\n", expected_output_payload_bytes);
    printf("  available bytes       : %zu\n",
           metadata_len > ap_offset + ap_size ? metadata_len - ap_offset - ap_size : 0u);

    return 0;
}
