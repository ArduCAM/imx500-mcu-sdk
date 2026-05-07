#include "imx500_multitask_postprocess.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

constexpr float kObjectDetectionConfidenceThreshold = 0.50f;

uint32_t tensor_bytes_per_element(const IMX500ParsedTensor &tensor)
{
    return std::max<uint32_t>(1u, (static_cast<uint32_t>(tensor.bits_per_element) + 7u) / 8u);
}

float tensor_read_value_raw_index(const IMX500ParsedTensor &tensor, uint32_t index)
{
    if (tensor.data == nullptr || index >= tensor.element_count) {
        return 0.0f;
    }

    const uint8_t *ptr = tensor.data + index * tensor_bytes_per_element(tensor);
    float raw = 0.0f;
    switch (tensor.bits_per_element) {
    case 8:
        raw = (tensor.format == 0) ? static_cast<float>(*reinterpret_cast<const int8_t *>(ptr))
                                   : static_cast<float>(*ptr);
        break;
    case 16:
        raw = (tensor.format == 0)
                  ? static_cast<float>(*reinterpret_cast<const int16_t *>(ptr))
                  : static_cast<float>(*reinterpret_cast<const uint16_t *>(ptr));
        break;
    case 32:
        raw = (tensor.format == 0)
                  ? static_cast<float>(*reinterpret_cast<const int32_t *>(ptr))
                  : static_cast<float>(*reinterpret_cast<const uint32_t *>(ptr));
        break;
    default:
        break;
    }
    return (raw - static_cast<float>(tensor.zero_point)) * tensor.scale;
}

bool tensor_logical_offset(const IMX500ParsedTensor &tensor, const uint32_t *coords, uint32_t coord_count, uint32_t *offset)
{
    if (coords == nullptr || offset == nullptr || coord_count != tensor.dimension_count) {
        return false;
    }

    uint32_t stride = 1;
    uint32_t raw_index = 0;
    for (uint32_t i = 0; i < coord_count; ++i) {
        const uint32_t dim_size = tensor.dimensions[i].size;
        if (coords[i] >= dim_size) {
            return false;
        }
        raw_index += coords[i] * stride;
        if (i + 1 < coord_count && dim_size > 0 && stride > UINT32_MAX / dim_size) {
            return false;
        }
        stride *= dim_size;
    }
    *offset = raw_index;
    return true;
}

bool tensor_read_value_logical(const IMX500ParsedTensor &tensor, const uint32_t *coords, uint32_t coord_count, float *value)
{
    uint32_t raw_index = 0;
    if (!tensor_logical_offset(tensor, coords, coord_count, &raw_index) || value == nullptr) {
        return false;
    }
    *value = tensor_read_value_raw_index(tensor, raw_index);
    return true;
}

bool get_network_input_hw(const IMX500ParsedNetwork &network, uint16_t *height, uint16_t *width)
{
    if (network.input_tensor_count == 0 || network.input_tensors[0].dimension_count < 2) {
        return false;
    }
    if (height != nullptr) {
        *height = network.input_tensors[0].dimensions[0].size;
    }
    if (width != nullptr) {
        *width = network.input_tensors[0].dimensions[1].size;
    }
    return true;
}

bool detection_box_component(const IMX500ParsedTensor &tensor, uint32_t box_index, uint32_t component, float *value)
{
    if (value == nullptr) {
        return false;
    }

    if (tensor.dimension_count >= 2) {
        std::array<uint32_t, IMX500_MAX_TENSOR_DIMS> coords = {};
        if (tensor.dimensions[tensor.dimension_count - 1].size == 4) {
            coords[tensor.dimension_count - 2] = box_index;
            coords[tensor.dimension_count - 1] = component;
            return tensor_read_value_logical(tensor, coords.data(), tensor.dimension_count, value);
        }
        if (tensor.dimensions[0].size == 4) {
            coords[0] = component;
            coords[1] = box_index;
            return tensor_read_value_logical(tensor, coords.data(), tensor.dimension_count, value);
        }
    }

    const uint32_t stride = tensor.element_count / 4u;
    if (stride == 0 || box_index >= stride) {
        return false;
    }
    *value = tensor_read_value_raw_index(tensor, box_index + stride * component);
    return true;
}

std::string trim_ascii(const std::string &value)
{
    size_t start = 0;
    size_t end = value.size();
    while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

} // namespace

std::vector<std::string> imx500_parse_embedded_labels(const Imx500EmbeddedBlob &blob)
{
    std::vector<std::string> labels;
    if (blob.data == nullptr || blob.size == 0) {
        return labels;
    }

    std::string text(reinterpret_cast<const char *>(blob.data), blob.size);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }

        std::string line = trim_ascii(text.substr(start, end - start));
        if (!line.empty()) {
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                bool numeric_prefix = true;
                for (size_t i = 0; i < colon; ++i) {
                    if (std::isdigit(static_cast<unsigned char>(line[i])) == 0) {
                        numeric_prefix = false;
                        break;
                    }
                }
                if (numeric_prefix) {
                    line = trim_ascii(line.substr(colon + 1));
                }
            }
            labels.push_back(line);
        }

        start = end + 1;
    }
    return labels;
}

std::string imx500_sanitize_for_lcd(const std::string &input, size_t max_chars)
{
    std::string output;
    output.reserve(std::min<size_t>(max_chars, input.size()));
    bool last_was_space = false;
    for (char raw_ch : input) {
        char ch = raw_ch;
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        bool push_space = false;
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == ':' || ch == '-' || ch == '.' || ch == '%') {
            output.push_back(ch);
            last_was_space = false;
        } else if (ch == '+' || ch == '/' || ch == ',' || ch == '_' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
            push_space = true;
        }

        if (push_space && !last_was_space && !output.empty()) {
            output.push_back(' ');
            last_was_space = true;
        }
        if (output.size() >= max_chars) {
            break;
        }
    }

    output = trim_ascii(output);
    if (output.size() > max_chars) {
        output.resize(max_chars);
        output = trim_ascii(output);
    }
    if (output.empty()) {
        return "N A";
    }
    return output;
}

std::string imx500_label_for_class_id(const std::vector<std::string> &labels, int class_id, size_t max_chars)
{
    if (class_id >= 0 && static_cast<size_t>(class_id) < labels.size()) {
        return imx500_sanitize_for_lcd(labels[static_cast<size_t>(class_id)], max_chars);
    }

    char fallback[32] = {};
    std::snprintf(fallback, sizeof(fallback), "CLASS %d", class_id);
    return fallback;
}

float imx500_score_to_percent(float score)
{
    if (score <= 1.5f) {
        return score * 100.0f;
    }
    return score;
}

bool imx500_postprocess_classification(const IMX500ParsedMetadata &parsed,
                                       const std::vector<std::string> &labels,
                                       Imx500ClassificationOverlayState *out,
                                       std::string *summary_line_1,
                                       std::string *summary_line_2)
{
    if (out == nullptr || parsed.network_count == 0) {
        return false;
    }
    *out = {};

    const IMX500ParsedNetwork &network = parsed.networks[parsed.selected_network_index];
    if (network.output_tensor_count == 0) {
        return false;
    }
    const IMX500ParsedTensor &scores_tensor = network.output_tensors[0];
    if (scores_tensor.element_count == 0) {
        return false;
    }

    const bool skip_background = !labels.empty() &&
                                 imx500_sanitize_for_lcd(labels[0], 16) == "BACKGROUND";
    const uint32_t start_index = (skip_background && scores_tensor.element_count > 1) ? 1u : 0u;

    std::array<int, kImx500TopKCount> top_ids = {-1, -1, -1};
    std::array<float, kImx500TopKCount> top_scores = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    for (uint32_t i = start_index; i < scores_tensor.element_count; ++i) {
        const float score = tensor_read_value_raw_index(scores_tensor, i);
        for (size_t slot = 0; slot < kImx500TopKCount; ++slot) {
            if (score <= top_scores[slot]) {
                continue;
            }
            for (size_t shift = kImx500TopKCount - 1; shift > slot; --shift) {
                top_scores[shift] = top_scores[shift - 1];
                top_ids[shift] = top_ids[shift - 1];
            }
            top_scores[slot] = score;
            top_ids[slot] = static_cast<int>(i);
            break;
        }
    }

    for (size_t i = 0; i < kImx500TopKCount; ++i) {
        if (top_ids[i] < 0) {
            continue;
        }
        out->class_ids[out->count] = top_ids[i];
        out->scores[out->count] = top_scores[i];
        out->count++;
    }

    if (out->count == 0) {
        return false;
    }

    const std::string best_label = imx500_label_for_class_id(labels, out->class_ids[0]);
    char line1[64] = {};
    char line2[64] = {};
    std::snprintf(line1, sizeof(line1), "TOP1 %s", best_label.c_str());
    std::snprintf(line2, sizeof(line2), "%.1f PERCENT", imx500_score_to_percent(out->scores[0]));
    if (summary_line_1 != nullptr) {
        *summary_line_1 = line1;
    }
    if (summary_line_2 != nullptr) {
        *summary_line_2 = line2;
    }
    return true;
}

bool imx500_postprocess_detection(const IMX500ParsedMetadata &parsed,
                                  const std::vector<std::string> &labels,
                                  Imx500DetectionOverlayState *out,
                                  std::string *summary_line_1,
                                  std::string *summary_line_2)
{
    if (out == nullptr || parsed.network_count == 0) {
        return false;
    }
    *out = {};

    const IMX500ParsedNetwork &network = parsed.networks[parsed.selected_network_index];
    if (network.output_tensor_count < 4) {
        return false;
    }

    const IMX500ParsedTensor &boxes_tensor = network.output_tensors[0];
    const IMX500ParsedTensor &scores_tensor = network.output_tensors[1];
    const IMX500ParsedTensor &classes_tensor = network.output_tensors[2];
    const IMX500ParsedTensor &valid_tensor = network.output_tensors[3];

    uint16_t input_h = 320;
    uint16_t input_w = 320;
    get_network_input_hw(network, &input_h, &input_w);
    out->source_height = input_h;
    out->source_width = input_w;

    const uint32_t valid_count = std::max(0, static_cast<int>(std::lround(tensor_read_value_raw_index(valid_tensor, 0))));
    const uint32_t max_box_count = std::min<uint32_t>({
        valid_count,
        static_cast<uint32_t>(scores_tensor.element_count),
        static_cast<uint32_t>(classes_tensor.element_count),
        static_cast<uint32_t>(boxes_tensor.element_count / 4u),
        static_cast<uint32_t>(kImx500MaxOverlayDetections),
    });

    float max_coord = 0.0f;
    for (uint32_t i = 0; i < max_box_count; ++i) {
        const float score = tensor_read_value_raw_index(scores_tensor, i);
        if (score < kObjectDetectionConfidenceThreshold) {
            continue;
        }

        float y1 = 0.0f;
        float x1 = 0.0f;
        float y2 = 0.0f;
        float x2 = 0.0f;
        if (!detection_box_component(boxes_tensor, i, 0, &y1) ||
            !detection_box_component(boxes_tensor, i, 1, &x1) ||
            !detection_box_component(boxes_tensor, i, 2, &y2) ||
            !detection_box_component(boxes_tensor, i, 3, &x2)) {
            continue;
        }

        max_coord = std::max(max_coord, std::max(std::fabs(y1), std::fabs(x1)));
        max_coord = std::max(max_coord, std::max(std::fabs(y2), std::fabs(x2)));

        Imx500DetectionOverlayItem &item = out->items[out->count];
        item.y1 = y1;
        item.x1 = x1;
        item.y2 = y2;
        item.x2 = x2;
        item.score = score;
        item.class_id = static_cast<int>(std::lround(tensor_read_value_raw_index(classes_tensor, i)));
        out->count++;
    }

    if (out->count == 0) {
        if (summary_line_1 != nullptr) {
            *summary_line_1 = "NO DETECTIONS";
        }
        if (summary_line_2 != nullptr) {
            *summary_line_2 = "SCENE CLEAR";
        }
        return true;
    }

    const bool normalized_coords = max_coord <= 1.5f;
    for (uint32_t i = 0; i < out->count; ++i) {
        Imx500DetectionOverlayItem &item = out->items[i];
        if (normalized_coords) {
            item.y1 *= static_cast<float>(input_h);
            item.x1 *= static_cast<float>(input_w);
            item.y2 *= static_cast<float>(input_h);
            item.x2 *= static_cast<float>(input_w);
        }
        item.x1 = std::clamp(item.x1, 0.0f, static_cast<float>(input_w));
        item.x2 = std::clamp(item.x2, 0.0f, static_cast<float>(input_w));
        item.y1 = std::clamp(item.y1, 0.0f, static_cast<float>(input_h));
        item.y2 = std::clamp(item.y2, 0.0f, static_cast<float>(input_h));
    }

    const Imx500DetectionOverlayItem &best = out->items[0];
    const std::string best_label = imx500_label_for_class_id(labels, best.class_id);
    char line1[64] = {};
    char line2[64] = {};
    std::snprintf(line1, sizeof(line1), "DETECTIONS %u", static_cast<unsigned>(out->count));
    std::snprintf(line2, sizeof(line2), "%s %.0f PERCENT", best_label.c_str(), imx500_score_to_percent(best.score));
    if (summary_line_1 != nullptr) {
        *summary_line_1 = line1;
    }
    if (summary_line_2 != nullptr) {
        *summary_line_2 = line2;
    }
    return true;
}

bool imx500_postprocess_segmentation(const IMX500ParsedMetadata &parsed,
                                     const std::vector<std::string> &labels,
                                     uint16_t thumb_w,
                                     uint16_t thumb_h,
                                     uint8_t *mask_out,
                                     Imx500SegmentationOverlayState *out,
                                     std::string *summary_line_1,
                                     std::string *summary_line_2)
{
    if (mask_out == nullptr || out == nullptr || parsed.network_count == 0 || thumb_w == 0 || thumb_h == 0) {
        return false;
    }
    *out = {};

    const IMX500ParsedNetwork &network = parsed.networks[parsed.selected_network_index];
    if (network.output_tensor_count == 0) {
        return false;
    }
    const IMX500ParsedTensor &mask_tensor = network.output_tensors[0];
    if (mask_tensor.dimension_count < 2) {
        return false;
    }

    const uint32_t mask_h = mask_tensor.dimensions[0].size;
    const uint32_t mask_w = mask_tensor.dimensions[1].size;
    const bool has_channel = mask_tensor.dimension_count >= 3;
    std::array<uint32_t, 256> histogram = {};

    for (uint32_t y = 0; y < thumb_h; ++y) {
        const uint32_t src_y = static_cast<uint32_t>((static_cast<uint64_t>(y) * mask_h) / thumb_h);
        for (uint32_t x = 0; x < thumb_w; ++x) {
            const uint32_t src_x = static_cast<uint32_t>((static_cast<uint64_t>(x) * mask_w) / thumb_w);
            float value = 0.0f;
            if (has_channel) {
                const uint32_t coords[3] = {std::min(src_y, mask_h - 1u), std::min(src_x, mask_w - 1u), 0u};
                if (!tensor_read_value_logical(mask_tensor, coords, 3, &value)) {
                    value = 0.0f;
                }
            } else {
                const uint32_t coords[2] = {std::min(src_y, mask_h - 1u), std::min(src_x, mask_w - 1u)};
                if (!tensor_read_value_logical(mask_tensor, coords, 2, &value)) {
                    value = 0.0f;
                }
            }

            const uint8_t class_id = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 255));
            mask_out[y * thumb_w + x] = class_id;
            histogram[class_id]++;
        }
    }

    out->ready = true;
    out->width = thumb_w;
    out->height = thumb_h;

    uint8_t dominant_class = 0;
    uint32_t dominant_pixels = 0;
    for (uint32_t class_id = 1; class_id < histogram.size(); ++class_id) {
        if (histogram[class_id] > dominant_pixels) {
            dominant_pixels = histogram[class_id];
            dominant_class = static_cast<uint8_t>(class_id);
        }
    }
    out->dominant_class_id = dominant_class;

    if (dominant_class == 0 || dominant_pixels == 0) {
        if (summary_line_1 != nullptr) {
            *summary_line_1 = "NO FOREGROUND";
        }
        if (summary_line_2 != nullptr) {
            *summary_line_2 = "BACKGROUND ONLY";
        }
    } else {
        const std::string label = imx500_label_for_class_id(labels, dominant_class);
        char line1[64] = {};
        char line2[64] = {};
        std::snprintf(line1, sizeof(line1), "DOMINANT %s", label.c_str());
        std::snprintf(line2, sizeof(line2), "MASK %ux%u", thumb_w, thumb_h);
        if (summary_line_1 != nullptr) {
            *summary_line_1 = line1;
        }
        if (summary_line_2 != nullptr) {
            *summary_line_2 = line2;
        }
    }
    return true;
}
