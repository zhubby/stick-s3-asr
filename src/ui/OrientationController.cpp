#include "ui/OrientationController.h"

#include <cmath>

namespace stick_s3_asr {

namespace {
constexpr float kTiltThreshold = 0.55f;
constexpr float kDominanceMargin = 0.15f;
constexpr uint32_t kRotationCooldownMs = 700;
}

OrientationController::OrientationController(uint8_t initialRotation)
    : rotation_(initialRotation & 3U) {}

bool OrientationController::update(bool imuAvailable,
                                   float accelX,
                                   float accelY,
                                   uint32_t nowMs) {
  if (!imuAvailable) return false;

  const uint8_t next = chooseRotation(accelX, accelY);
  if (next == rotation_) return false;
  if (lastChangeMs_ != 0 && nowMs - lastChangeMs_ < kRotationCooldownMs) {
    return false;
  }

  rotation_ = next;
  lastChangeMs_ = nowMs;
  return true;
}

uint8_t OrientationController::chooseRotation(float accelX, float accelY) const {
  const float absX = std::fabs(accelX);
  const float absY = std::fabs(accelY);

  if (absX < kTiltThreshold && absY < kTiltThreshold) {
    return rotation_;
  }

  if (absX > absY + kDominanceMargin) {
    return accelX >= 0.0f ? 2 : 0;
  }
  if (absY > absX + kDominanceMargin) {
    return accelY >= 0.0f ? 1 : 3;
  }
  return rotation_;
}

}  // namespace stick_s3_asr
