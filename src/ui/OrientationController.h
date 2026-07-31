#pragma once

#include <cstdint>

namespace stick_s3_asr {

class OrientationController {
 public:
  explicit OrientationController(uint8_t initialRotation = 1);

  bool update(bool imuAvailable, float accelX, float accelY, uint32_t nowMs);
  uint8_t rotation() const { return rotation_; }
  bool landscape() const { return (rotation_ & 1U) != 0; }

 private:
  uint8_t chooseRotation(float accelX, float accelY) const;

  uint8_t rotation_;
  uint32_t lastChangeMs_ = 0;
  uint8_t pendingRotation_ = 0;
  uint32_t pendingSinceMs_ = 0;
  bool hasPendingRotation_ = false;
};

}  // namespace stick_s3_asr
