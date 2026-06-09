#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "ApParams.h"
#include "test_metadata_outputtensor_1.h"

namespace {

constexpr std::size_t kImx500HeaderLen = 12;
constexpr std::uint32_t kOutputTensorAlignment = 4;

struct Imx500OutputHeader {
    std::uint8_t valid_flag;
    std::uint8_t frame_count;
    std::uint16_t max_length_of_line;
    std::uint16_t size_of_ap_parameter;
    std::uint16_t network_ordinal;
    std::uint8_t indicator;
};

std::uint16_t ReadLe16(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8);
}

Imx500OutputHeader UnpackImx500OutputHeader(const std::uint8_t *data)
{
    return {
        data[0],
        data[1],
        ReadLe16(data + 2),
        ReadLe16(data + 4),
        ReadLe16(data + 6),
        data[8],
    };
}

std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment)
{
    return ((value + alignment - 1u) / alignment) * alignment;
}

const char *StringOrEmpty(const flatbuffers::String *value)
{
    return value ? value->c_str() : "";
}

bool VerifyApParams(const std::uint8_t *data, std::size_t size)
{
    flatbuffers::Verifier verifier(data, size);
    return apParams::fb::VerifyFBApParamsBuffer(verifier);
}

std::size_t FindApParamsOffset(const std::uint8_t *data, std::size_t data_len)
{
    if (data_len >= kImx500HeaderLen) {
        const auto header = UnpackImx500OutputHeader(data);
        if (header.size_of_ap_parameter > 0 &&
            kImx500HeaderLen + header.size_of_ap_parameter <= data_len &&
            VerifyApParams(data + kImx500HeaderLen, header.size_of_ap_parameter)) {
            return kImx500HeaderLen;
        }
    }

    for (std::size_t offset = 0; offset + sizeof(flatbuffers::uoffset_t) <= data_len;
         ++offset) {
        if (VerifyApParams(data + offset, data_len - offset)) {
            return offset;
        }
    }
    return data_len;
}

std::uint32_t DimensionProduct(
    const flatbuffers::Vector<flatbuffers::Offset<apParams::fb::FBDimension>> *dimensions)
{
    if (!dimensions || dimensions->empty()) {
        return 0;
    }

    std::uint32_t product = 1;
    for (const auto *dimension : *dimensions) {
        product *= dimension->size();
    }
    return product;
}

void PrintHeader(const Imx500OutputHeader &header)
{
    std::cout << "IMX500 header\n";
    std::cout << "  valid_flag            : " << static_cast<unsigned>(header.valid_flag)
              << "\n";
    std::cout << "  frame_count           : " << static_cast<unsigned>(header.frame_count)
              << "\n";
    std::cout << "  max_length_of_line    : " << header.max_length_of_line << "\n";
    std::cout << "  size_of_ap_parameter  : " << header.size_of_ap_parameter << "\n";
    std::cout << "  network_ordinal       : " << header.network_ordinal << "\n";
    std::cout << "  indicator             : " << static_cast<unsigned>(header.indicator)
              << "\n";
}

void PrintDimensions(
    const flatbuffers::Vector<flatbuffers::Offset<apParams::fb::FBDimension>> *dimensions,
    const std::string &indent)
{
    const auto count = dimensions ? dimensions->size() : 0;
    std::cout << indent << "dimensions[" << count << "]\n";
    if (!dimensions) {
        return;
    }

    for (flatbuffers::uoffset_t i = 0; i < dimensions->size(); ++i) {
        const auto *dimension = dimensions->Get(i);
        std::cout << indent << "  [" << i << "] id="
                  << static_cast<unsigned>(dimension->id()) << " size="
                  << dimension->size() << " serializationIndex="
                  << static_cast<unsigned>(dimension->serializationIndex())
                  << " padding=" << static_cast<unsigned>(dimension->padding()) << "\n";
    }
}

void PrintInputTensor(const apParams::fb::FBInputTensor *tensor, flatbuffers::uoffset_t index)
{
    const auto *dimensions = tensor->dimensions();

    std::cout << "    inputTensors[" << index << "]\n";
    std::cout << "      id                : " << static_cast<unsigned>(tensor->id())
              << "\n";
    std::cout << "      name              : " << StringOrEmpty(tensor->name()) << "\n";
    std::cout << "      numOfDimensions   : "
              << static_cast<unsigned>(tensor->numOfDimensions()) << "\n";
    std::cout << "      shift             : " << tensor->shift() << "\n";
    std::cout << "      scale             : " << std::setprecision(9) << tensor->scale()
              << "\n";
    std::cout << "      format            : " << static_cast<unsigned>(tensor->format())
              << "\n";
    std::cout << "      element_count     : " << DimensionProduct(dimensions) << "\n";
    PrintDimensions(dimensions, "      ");
}

std::uint32_t OutputTensorDataBytes(const apParams::fb::FBOutputTensor *tensor)
{
    const auto element_count = DimensionProduct(tensor->dimensions());
    const auto bytes_per_element =
        (static_cast<std::uint32_t>(tensor->bitsPerElement()) + 7u) / 8u;
    return element_count * bytes_per_element;
}

std::uint32_t PrintOutputTensor(const apParams::fb::FBOutputTensor *tensor,
                                flatbuffers::uoffset_t index)
{
    const auto *dimensions = tensor->dimensions();
    const auto data_bytes = OutputTensorDataBytes(tensor);
    const auto aligned_data_bytes = AlignUp(data_bytes, kOutputTensorAlignment);

    std::cout << "    outputTensors[" << index << "]\n";
    std::cout << "      id                : " << static_cast<unsigned>(tensor->id())
              << "\n";
    std::cout << "      name              : " << StringOrEmpty(tensor->name()) << "\n";
    std::cout << "      numOfDimensions   : "
              << static_cast<unsigned>(tensor->numOfDimensions()) << "\n";
    std::cout << "      bitsPerElement    : "
              << static_cast<unsigned>(tensor->bitsPerElement()) << "\n";
    std::cout << "      shift             : " << tensor->shift() << "\n";
    std::cout << "      scale             : " << std::setprecision(9) << tensor->scale()
              << "\n";
    std::cout << "      format            : " << static_cast<unsigned>(tensor->format())
              << "\n";
    std::cout << "      element_count     : " << DimensionProduct(dimensions) << "\n";
    std::cout << "      data_bytes        : " << data_bytes << "\n";
    std::cout << "      aligned_data_bytes: " << aligned_data_bytes << "\n";
    PrintDimensions(dimensions, "      ");

    return aligned_data_bytes;
}

}  // namespace

int main()
{
    const auto *metadata = reinterpret_cast<const std::uint8_t *>(output_bin);
    const std::size_t metadata_len = output_bin_len;
    std::uint32_t expected_output_payload_bytes = 0;

    std::cout << "metadata bytes: " << metadata_len << "\n\n";

    if (metadata_len < kImx500HeaderLen) {
        std::cerr << "metadata is too short for an IMX500 header\n";
        return 1;
    }

    const auto header = UnpackImx500OutputHeader(metadata);
    PrintHeader(header);

    const auto ap_offset = FindApParamsOffset(metadata, metadata_len);
    if (ap_offset == metadata_len) {
        std::cerr << "could not find a valid apParams.fb.FBApParams FlatBuffer\n";
        return 1;
    }

    auto ap_size = metadata_len - ap_offset;
    if (ap_offset == kImx500HeaderLen && header.size_of_ap_parameter > 0) {
        ap_size = header.size_of_ap_parameter;
    }

    const bool verified = VerifyApParams(metadata + ap_offset, ap_size);
    std::cout << "\nFlatBuffer\n";
    std::cout << "  parser                : FlatBuffers C++\n";
    std::cout << "  offset                : " << ap_offset << "\n";
    std::cout << "  size                  : " << ap_size << "\n";
    std::cout << "  verify                : " << (verified ? "ok" : "failed") << "\n";
    if (!verified) {
        return 1;
    }

    const auto *ap_params = apParams::fb::GetFBApParams(metadata + ap_offset);
    const auto *networks = ap_params->networks();
    const auto network_count = networks ? networks->size() : 0;

    std::cout << "\nFBApParams\n";
    std::cout << "  networks[" << network_count << "]\n";

    if (networks) {
        for (flatbuffers::uoffset_t network_index = 0; network_index < networks->size();
             ++network_index) {
            const auto *network = networks->Get(network_index);
            const auto *input_tensors = network->inputTensors();
            const auto *output_tensors = network->outputTensors();

            std::cout << "  networks[" << network_index << "]\n";
            std::cout << "    id                  : " << network->id() << "\n";
            std::cout << "    name                : " << StringOrEmpty(network->name())
                      << "\n";
            std::cout << "    type                : " << StringOrEmpty(network->type())
                      << "\n";
            std::cout << "    inputTensorsLength  : "
                      << (input_tensors ? input_tensors->size() : 0) << "\n";
            std::cout << "    outputTensorsLength : "
                      << (output_tensors ? output_tensors->size() : 0) << "\n";

            if (input_tensors) {
                for (flatbuffers::uoffset_t i = 0; i < input_tensors->size(); ++i) {
                    PrintInputTensor(input_tensors->Get(i), i);
                }
            }

            if (output_tensors) {
                for (flatbuffers::uoffset_t i = 0; i < output_tensors->size(); ++i) {
                    expected_output_payload_bytes +=
                        PrintOutputTensor(output_tensors->Get(i), i);
                }
            }
        }
    }

    std::cout << "\nOutput payload estimate\n";
    std::cout << "  offset                : " << (ap_offset + ap_size) << "\n";
    std::cout << "  expected bytes        : " << expected_output_payload_bytes << "\n";
    std::cout << "  available bytes       : "
              << (metadata_len > ap_offset + ap_size ? metadata_len - ap_offset - ap_size
                                                     : 0)
              << "\n";

    return 0;
}
