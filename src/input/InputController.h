#pragma once

#include <cstdint>

#include "AppTypes.h"

namespace stick_s3_asr {

enum class InputEvent {
  None,
  StartRecording,
  StopRecording,
  ReturnToRecording,
};

class InputController {
 public:
  explicit InputController(uint32_t holdThresholdMs = 450);

  InputEvent update(bool recordButtonDown,
                    bool returnButtonPressed,
                    AppMode mode,
                    uint32_t nowMs);
  void resetRecordingGesture(bool recordButtonDown = false);

  bool recordingGestureActive() const { return recordingActive_; }
  uint32_t holdThresholdMs() const { return holdThresholdMs_; }

 private:
  bool canStartRecording(AppMode mode) const;

  uint32_t holdThresholdMs_;
  bool recordButtonWasDown_ = false;
  bool holdCandidate_ = false;
  bool recordingActive_ = false;
  uint32_t pressedAtMs_ = 0;
};

}  // namespace stick_s3_asr
