#pragma once

#include <cstddef>
#include <cstdint>

enum class Imx500ModelTaskType : uint8_t {
    kClassification = 0,
    kObjectDetection = 1,
    kPoseEstimation = 2,
    kSegmentation = 3,
};

struct Imx500EmbeddedBlob {
    const uint8_t *data;
    size_t size;
};

struct Imx500EmbeddedModelDescriptor {
    const char *key;
    const char *display_name;
    const char *task_name;
    Imx500ModelTaskType task_type;
    Imx500EmbeddedBlob firmware;
    Imx500EmbeddedBlob network_info;
    Imx500EmbeddedBlob labels;
};

const Imx500EmbeddedModelDescriptor *imx500_get_embedded_models(size_t *count);
const Imx500EmbeddedModelDescriptor *imx500_get_embedded_model(size_t index);
size_t imx500_get_default_embedded_model_index();
