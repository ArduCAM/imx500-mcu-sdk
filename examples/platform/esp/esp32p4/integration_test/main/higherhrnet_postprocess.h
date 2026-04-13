#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "ArducamIMX500SDK.h"

static constexpr uint32_t kHigherhrnetJointCount = 17;
static constexpr uint32_t kHigherhrnetMaxPeople = 30;

typedef struct {
    float x;
    float y;
    float score;
} HigherhrnetKeypoint;

typedef struct {
    float score;
    float y1;
    float x1;
    float y2;
    float x2;
    uint32_t valid_joint_count;
    HigherhrnetKeypoint keypoints[kHigherhrnetJointCount];
} HigherhrnetPose;

typedef struct {
    uint32_t pose_count;
    HigherhrnetPose poses[kHigherhrnetMaxPeople];
} HigherhrnetResult;

bool higherhrnet_postprocess_from_metadata(const IMX500ParsedMetadata *parsed_metadata,
                                           HigherhrnetResult *result);
