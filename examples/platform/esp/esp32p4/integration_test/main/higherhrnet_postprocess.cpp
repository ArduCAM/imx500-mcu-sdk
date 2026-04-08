#include "higherhrnet_postprocess.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esp_heap_caps.h"

namespace {

constexpr float kJointScoreThreshold = 0.05f;
constexpr float kPoseScoreThreshold = 0.10f;
constexpr uint32_t kMinValidJoints = 3;
constexpr float kTagThreshold = 1.0f;
constexpr uint32_t kOutputHeatmapH = 144;
constexpr uint32_t kOutputHeatmapW = 192;
constexpr uint32_t kInputImageH = 288;
constexpr uint32_t kInputImageW = 384;
constexpr uint8_t kJointOrder[kHigherhrnetJointCount] = {
    0, 1, 2, 3, 4, 5, 6, 11, 12, 7, 8, 9, 10, 13, 14, 15, 16,
};

struct PersonAccumulator {
    bool active = false;
    float mean_tag = 0.0f;
    uint32_t tag_count = 0;
    HigherhrnetPose pose = {};
};

PersonAccumulator *g_people_scratch = nullptr;

static PersonAccumulator *get_people_scratch()
{
    if (!g_people_scratch) {
        g_people_scratch = static_cast<PersonAccumulator *>(
            heap_caps_calloc(kHigherhrnetMaxPeople, sizeof(PersonAccumulator), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (!g_people_scratch) {
            g_people_scratch = static_cast<PersonAccumulator *>(calloc(kHigherhrnetMaxPeople, sizeof(PersonAccumulator)));
        }
    } else {
        for (uint32_t i = 0; i < kHigherhrnetMaxPeople; ++i) {
            g_people_scratch[i] = PersonAccumulator{};
        }
    }

    return g_people_scratch;
}

static float read_tensor_value(const IMX500ParsedTensor &tensor, uint32_t index)
{
    if (!tensor.data || index >= tensor.element_count) {
        return 0.0f;
    }

    const uint8_t *ptr = tensor.data + index * ((tensor.bits_per_element + 7u) / 8u);
    float raw = 0.0f;
    switch (tensor.bits_per_element) {
    case 8:
        raw = (tensor.format == 0) ? static_cast<float>(*(reinterpret_cast<const int8_t *>(ptr)))
                                   : static_cast<float>(*ptr);
        break;
    case 16:
        raw = (tensor.format == 0)
                  ? static_cast<float>(*(reinterpret_cast<const int16_t *>(ptr)))
                  : static_cast<float>(*(reinterpret_cast<const uint16_t *>(ptr)));
        break;
    case 32:
        raw = (tensor.format == 0)
                  ? static_cast<float>(*(reinterpret_cast<const int32_t *>(ptr)))
                  : static_cast<float>(*(reinterpret_cast<const uint32_t *>(ptr)));
        break;
    default:
        break;
    }
    return (raw - static_cast<float>(tensor.zero_point)) * tensor.scale;
}

static int read_tensor_index(const IMX500ParsedTensor &tensor, uint32_t index)
{
    return static_cast<int>(std::lround(read_tensor_value(tensor, index)));
}

static void reset_pose(HigherhrnetPose *pose)
{
    if (!pose) {
        return;
    }
    memset(pose, 0, sizeof(*pose));
    pose->x1 = 1e9f;
    pose->y1 = 1e9f;
    pose->x2 = -1e9f;
    pose->y2 = -1e9f;
}

static void finalize_pose(HigherhrnetPose *pose)
{
    if (!pose || pose->valid_joint_count == 0) {
        return;
    }
    if (pose->x1 > pose->x2) {
        pose->x1 = pose->x2 = 0.0f;
    }
    if (pose->y1 > pose->y2) {
        pose->y1 = pose->y2 = 0.0f;
    }
}

} // namespace

bool higherhrnet_postprocess_from_metadata(const IMX500ParsedMetadata *parsed_metadata,
                                           HigherhrnetResult *result)
{
    static bool tensor_layout_logged = false;
    if (!parsed_metadata || !result || parsed_metadata->network_count == 0) {
        return false;
    }
    memset(result, 0, sizeof(*result));

    const IMX500ParsedNetwork *network = &parsed_metadata->networks[parsed_metadata->selected_network_index];
    if (network->output_tensor_count < 3) {
        return false;
    }

    const IMX500ParsedTensor &tag_tensor = network->output_tensors[0];
    const IMX500ParsedTensor &ind_tensor = network->output_tensors[1];
    const IMX500ParsedTensor &val_tensor = network->output_tensors[2];

    if (!tensor_layout_logged) {
        std::printf("[HRNET] tensor dims: tag=(");
        for (uint32_t i = 0; i < tag_tensor.dimension_count; ++i) {
            std::printf("%s%u", (i == 0) ? "" : "x", tag_tensor.dimensions[i].size);
        }
        std::printf(") ind=(");
        for (uint32_t i = 0; i < ind_tensor.dimension_count; ++i) {
            std::printf("%s%u", (i == 0) ? "" : "x", ind_tensor.dimensions[i].size);
        }
        std::printf(") val=(");
        for (uint32_t i = 0; i < val_tensor.dimension_count; ++i) {
            std::printf("%s%u", (i == 0) ? "" : "x", val_tensor.dimensions[i].size);
        }
        std::printf(")\n");
        tensor_layout_logged = true;
    }

    uint32_t max_num_people = std::min({kHigherhrnetMaxPeople,
                                        tag_tensor.element_count / kHigherhrnetJointCount,
                                        ind_tensor.element_count / kHigherhrnetJointCount,
                                        val_tensor.element_count / kHigherhrnetJointCount});
    if (max_num_people == 0) {
        return false;
    }

    PersonAccumulator *people = get_people_scratch();
    if (!people) {
        std::printf("[HRNET] no memory for people scratch, bytes=%lu\n",
                    static_cast<unsigned long>(sizeof(PersonAccumulator) * kHigherhrnetMaxPeople));
        return false;
    }
    uint32_t raw_people = 0;

    for (uint32_t order_index = 0; order_index < kHigherhrnetJointCount; ++order_index) {
        uint32_t joint_index = kJointOrder[order_index];

        for (uint32_t person_slot = 0; person_slot < max_num_people; ++person_slot) {
            // The IMX500 HigherHRNet metadata stores tag/ind/val in joint-major order.
            uint32_t element_index = joint_index * max_num_people + person_slot;
            float score = read_tensor_value(val_tensor, element_index);
            if (score <= kJointScoreThreshold) {
                continue;
            }

            int flat_index = read_tensor_index(ind_tensor, element_index);
            float tag = read_tensor_value(tag_tensor, element_index);
            float x = static_cast<float>(flat_index % static_cast<int>(kOutputHeatmapW));
            float y = static_cast<float>(flat_index / static_cast<int>(kOutputHeatmapW));
            x *= static_cast<float>(kInputImageW) / static_cast<float>(kOutputHeatmapW);
            y *= static_cast<float>(kInputImageH) / static_cast<float>(kOutputHeatmapH);

            int best_person = -1;
            float best_diff = 1e9f;
            for (uint32_t i = 0; i < kHigherhrnetMaxPeople; ++i) {
                if (!people[i].active || people[i].tag_count == 0) {
                    continue;
                }
                float diff = std::fabs(tag - people[i].mean_tag);
                if (diff < best_diff) {
                    best_diff = diff;
                    best_person = static_cast<int>(i);
                }
            }

            if (best_person < 0 || best_diff >= kTagThreshold) {
                for (uint32_t i = 0; i < kHigherhrnetMaxPeople; ++i) {
                    if (!people[i].active) {
                        people[i].active = true;
                        reset_pose(&people[i].pose);
                        people[i].mean_tag = 0.0f;
                        people[i].tag_count = 0;
                        best_person = static_cast<int>(i);
                        raw_people++;
                        break;
                    }
                }
            }

            if (best_person < 0) {
                continue;
            }

            PersonAccumulator &person = people[best_person];
            person.mean_tag = (person.mean_tag * static_cast<float>(person.tag_count) + tag) /
                              static_cast<float>(person.tag_count + 1);
            person.tag_count++;
            person.pose.keypoints[joint_index].x = x;
            person.pose.keypoints[joint_index].y = y;
            person.pose.keypoints[joint_index].score = score;
        }
    }

    uint32_t filtered_people = 0;
    for (uint32_t i = 0; i < kHigherhrnetMaxPeople; ++i) {
        if (!people[i].active) {
            continue;
        }

        HigherhrnetPose pose = {};
        reset_pose(&pose);
        float score_sum = 0.0f;
        for (uint32_t joint = 0; joint < kHigherhrnetJointCount; ++joint) {
            pose.keypoints[joint] = people[i].pose.keypoints[joint];
            if (pose.keypoints[joint].score > kJointScoreThreshold) {
                pose.valid_joint_count++;
                score_sum += pose.keypoints[joint].score;
                pose.x1 = std::min(pose.x1, pose.keypoints[joint].x);
                pose.y1 = std::min(pose.y1, pose.keypoints[joint].y);
                pose.x2 = std::max(pose.x2, pose.keypoints[joint].x);
                pose.y2 = std::max(pose.y2, pose.keypoints[joint].y);
            }
        }

        if (pose.valid_joint_count < kMinValidJoints) {
            continue;
        }

        pose.score = score_sum / static_cast<float>(pose.valid_joint_count);
        if (pose.score < kPoseScoreThreshold) {
            continue;
        }

        finalize_pose(&pose);
        if (result->pose_count < kHigherhrnetMaxPeople) {
            result->poses[result->pose_count++] = pose;
            filtered_people++;
        }
    }

    float first_score = result->pose_count ? result->poses[0].score : 0.0f;
    std::printf("[HRNET] raw_people=%lu filtered_people=%lu valid_num=%lu first_score=%.3f\n",
                static_cast<unsigned long>(raw_people),
                static_cast<unsigned long>(filtered_people),
                static_cast<unsigned long>(result->pose_count),
                first_score);
    return true;
}
