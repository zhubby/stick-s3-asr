#include "input/InputController.h"

namespace stick_s3_asr {

InputController::InputController(uint32_t holdThresholdMs)
    : holdThresholdMs_(holdThresholdMs) {}

InputEvent InputController::update(bool recordButtonDown,
                                   bool pageButtonDown,
                                   AppMode mode,
                                   uint32_t nowMs) {
  if (pageButtonDown && !pageButtonWasDown_) {
    pageButtonWasDown_ = true;
    pagePressedAtMs_ = nowMs;
    pageCandidate_ = mode == AppMode::Result && !recordingActive_;
    pageHoldFired_ = false;
    return InputEvent::None;
  }

  if (pageButtonDown && pageCandidate_ && !pageHoldFired_ &&
      nowMs - pagePressedAtMs_ >= holdThresholdMs_) {
    pageHoldFired_ = true;
    return InputEvent::ReturnToRecording;
  }

  if (!pageButtonDown && pageButtonWasDown_) {
    pageButtonWasDown_ = false;
    const bool shortPagePress = pageCandidate_ && !pageHoldFired_;
    pageCandidate_ = false;
    pageHoldFired_ = false;
    pagePressedAtMs_ = 0;
    if (shortPagePress) {
      return InputEvent::NextPage;
    }
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

void InputController::resetRecordingGesture(bool recordButtonDown) {
  recordButtonWasDown_ = recordButtonDown;
  holdCandidate_ = false;
  recordingActive_ = false;
  pressedAtMs_ = 0;
}

bool InputController::canStartRecording(AppMode mode) const {
  return mode == AppMode::Idle || mode == AppMode::Result ||
         mode == AppMode::Error;
}

}  // namespace stick_s3_asr
