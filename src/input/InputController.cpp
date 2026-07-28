#include "input/InputController.h"

namespace stick_s3_asr {

InputController::InputController(uint32_t holdThresholdMs)
    : holdThresholdMs_(holdThresholdMs) {}

InputEvent InputController::update(bool recordButtonDown,
                                   bool nextPagePressed,
                                   AppMode mode,
                                   uint32_t nowMs) {
  if (nextPagePressed && mode == AppMode::Result && !recordingActive_) {
    return InputEvent::NextPage;
  }

  if (recordButtonDown && !recordButtonWasDown_) {
    recordButtonWasDown_ = true;
    pressedAtMs_ = nowMs;
    holdCandidate_ = canStartRecording(mode);
    return InputEvent::None;
  }

  if (recordButtonDown && holdCandidate_ && !recordingActive_ &&
      nowMs - pressedAtMs_ >= holdThresholdMs_) {
    recordingActive_ = true;
    return InputEvent::StartRecording;
  }

  if (!recordButtonDown && recordButtonWasDown_) {
    recordButtonWasDown_ = false;
    holdCandidate_ = false;
    if (recordingActive_) {
      recordingActive_ = false;
      return InputEvent::StopRecording;
    }
  }

  return InputEvent::None;
}

void InputController::resetRecordingGesture() {
  recordButtonWasDown_ = false;
  holdCandidate_ = false;
  recordingActive_ = false;
  pressedAtMs_ = 0;
}

bool InputController::canStartRecording(AppMode mode) const {
  return mode == AppMode::Idle || mode == AppMode::Result ||
         mode == AppMode::Error;
}

}  // namespace stick_s3_asr

