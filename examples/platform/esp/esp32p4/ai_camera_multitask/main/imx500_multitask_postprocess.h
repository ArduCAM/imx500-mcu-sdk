#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ArducamIMX500SDK.h"
#include "imx500_multitask_model_registry.h"

constexpr size_t kImx500TopKCount = 3;
constexpr size_t kImx500MaxOverlayDetections = 10;

struct Imx500ClassificationOverlayState {
    uint32_t count = 0;
    int class_ids[kImx500TopKCount] = {};
    float scores[kImx500TopKCount] = {};
};

struct Imx500DetectionOverlayItem {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float score = 0.0f;
    int class_id = 0;
};

struct Imx500DetectionOverlayState {
    uint32_t count = 0;
    uint16_t source_width = 0;
    uint16_t source_height = 0;
    Imx500DetectionOverlayItem items[kImx500MaxOverlayDetections] = {};
};

struct Imx500SegmentationOverlayState {
    bool ready = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t dominant_class_id = 0;
};

std::vector<std::string> imx500_parse_embedded_labels(const Imx500EmbeddedBlob &blob);
std::string imx500_sanitize_for_lcd(const std::string &input, size_t max_chars);
std::string imx500_label_for_class_id(const std::vector<std::string> &labels, int class_id, size_t max_chars = 28);
float imx500_score_to_percent(float score);

bool imx500_postprocess_classification(const IMX500ParsedMetadata &parsed,
                                       const std::vector<std::string> &labels,
                                       Imx500ClassificationOverlayState *out,
                                       std::string *summary_line_1,
                                       std::string *summary_line_2);

bool imx500_postprocess_detection(const IMX500ParsedMetadata &parsed,
                                  const std::vector<std::string> &labels,
                                  Imx500DetectionOverlayState *out,
                                  std::string *summary_line_1,
                                  std::string *summary_line_2);

bool imx500_postprocess_segmentation(const IMX500ParsedMetadata &parsed,
                                     const std::vector<std::string> &labels,
                                     uint16_t thumb_w,
                                     uint16_t thumb_h,
                                     uint8_t *mask_out,
                                     Imx500SegmentationOverlayState *out,
                                     std::string *summary_line_1,
                                     std::string *summary_line_2);
