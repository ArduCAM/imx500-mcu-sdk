#include "imx500_multitask_model_registry.h"

#include "generated_imx500_models/coco_ssd_labels.h"
#include "generated_imx500_models/deeplabv3plus_fpk.h"
#include "generated_imx500_models/deeplabv3plus_network_info.h"
#include "generated_imx500_models/higherhrnet_fpk.h"
#include "generated_imx500_models/higherhrnet_network_info.h"
#include "generated_imx500_models/imagenet_labels.h"
#include "generated_imx500_models/mobilenet_v2_fpk.h"
#include "generated_imx500_models/mobilenet_v2_network_info.h"
#include "generated_imx500_models/ssd_mobilenetv2_fpnlite_fpk.h"
#include "generated_imx500_models/ssd_mobilenetv2_fpnlite_network_info.h"
#include "generated_imx500_models/voc20_segmentation_labels.h"

namespace {

const Imx500EmbeddedModelDescriptor *embedded_models(size_t *count)
{
    static const Imx500EmbeddedModelDescriptor kEmbeddedModels[] = {
        {
            "mobilenet_v2",
            "MobileNet V2",
            "Classification",
            Imx500ModelTaskType::kClassification,
            {mobilenet_v2_fpk_data, mobilenet_v2_fpk_size},
            {mobilenet_v2_network_info_data, mobilenet_v2_network_info_size},
            {imagenet_labels_data, imagenet_labels_size},
        },
        {
            "ssd_mobilenetv2_fpnlite",
            "SSD MobileNetV2 FPNLite",
            "Object Detection",
            Imx500ModelTaskType::kObjectDetection,
            {ssd_mobilenetv2_fpnlite_fpk_data, ssd_mobilenetv2_fpnlite_fpk_size},
            {ssd_mobilenetv2_fpnlite_network_info_data, ssd_mobilenetv2_fpnlite_network_info_size},
            {coco_ssd_labels_data, coco_ssd_labels_size},
        },
        {
            "higherhrnet",
            "HigherHRNet",
            "Pose Estimation",
            Imx500ModelTaskType::kPoseEstimation,
            {higherhrnet_fpk_data, higherhrnet_fpk_size},
            {higherhrnet_network_info_data, higherhrnet_network_info_size},
            {nullptr, 0},
        },
        {
            "deeplabv3plus",
            "DeepLabV3Plus",
            "Segmentation",
            Imx500ModelTaskType::kSegmentation,
            {deeplabv3plus_fpk_data, deeplabv3plus_fpk_size},
            {deeplabv3plus_network_info_data, deeplabv3plus_network_info_size},
            {voc20_segmentation_labels_data, voc20_segmentation_labels_size},
        },
    };

    if (count != nullptr) {
        *count = sizeof(kEmbeddedModels) / sizeof(kEmbeddedModels[0]);
    }
    return kEmbeddedModels;
}

} // namespace

const Imx500EmbeddedModelDescriptor *imx500_get_embedded_models(size_t *count)
{
    return embedded_models(count);
}

const Imx500EmbeddedModelDescriptor *imx500_get_embedded_model(size_t index)
{
    size_t model_count = 0;
    const Imx500EmbeddedModelDescriptor *models = embedded_models(&model_count);
    if (index >= model_count) {
        return nullptr;
    }
    return &models[index];
}

size_t imx500_get_default_embedded_model_index()
{
    return 2;
}
