#ifndef UNIT_TEST

#include "audio/MicRecorder.h"

#include <M5Unified.h>

namespace stick_s3_asr {

bool MicRecorder::begin(const AudioFormat& format,
                        uint16_t chunkMs,
                        uint16_t maxRecordSeconds,
                        size_t queuedChunks) {
  format_ = format;
  chunkMs_ = chunkMs;
  maxRecordSeconds_ = maxRecordSeconds;
  const size_t samplesPerChunk =
      (static_cast<size_t>(format_.sampleRate) * chunkMs_) / 1000U;
  capture_.assign(samplesPerChunk, 0);
  if (!ring_.reset(capture_.size() * sizeof(int16_t) * queuedChunks)) {
    initialized_ = false;
    return false;
  }
  M5.Mic.setSampleRate(format_.sampleRate);
  initialized_ = M5.Mic.begin();
  return initialized_;
}

bool MicRecorder::start(uint32_t nowMs) {
  if (!initialized_ || capture_.empty()) return false;
  ring_.clear();
  active_ = true;
  pendingCapture_ = false;
  stopRequested_ = false;
  timedOut_ = false;
  overflowed_ = false;
  lastPeak_ = 0;
  startedAtMs_ = nowMs;
  return scheduleChunk();
}

void MicRecorder::requestStop() {
  stopRequested_ = true;
  active_ = false;
}

void MicRecorder::cancel() {
  active_ = false;
  stopRequested_ = true;
  pendingCapture_ = false;
  ring_.clear();
}

void MicRecorder::update(uint32_t nowMs) {
  if (!initialized_) return;

  if (active_ && maxRecordSeconds_ > 0 &&
      nowMs - startedAtMs_ >= static_cast<uint32_t>(maxRecordSeconds_) * 1000U) {
    timedOut_ = true;
    requestStop();
  }

  if (pendingCapture_ && captureComplete()) {
    pendingCapture_ = false;
    lastPeak_ = pcmPeak(capture_.data(), capture_.size());
    if (!ring_.writeAll(reinterpret_cast<const uint8_t*>(capture_.data()), chunkBytes())) {
      overflowed_ = true;
      requestStop();
    }
  }

  if (active_ && !pendingCapture_ && !stopRequested_) {
    scheduleChunk();
  }
}

size_t MicRecorder::readPcm(uint8_t* out, size_t maxBytes) {
  return ring_.read(out, maxBytes);
}

bool MicRecorder::finished() const {
  return stopRequested_ && !pendingCapture_ && ring_.empty();
}

uint32_t MicRecorder::recordedMs(uint32_t nowMs) const {
  if (!active_ && !stopRequested_) return 0;
  return nowMs >= startedAtMs_ ? nowMs - startedAtMs_ : 0;
}

bool MicRecorder::scheduleChunk() {
  if (pendingCapture_ || capture_.empty()) return false;
  pendingCapture_ = M5.Mic.record(capture_.data(), capture_.size(),
                                  format_.sampleRate, false);
  return pendingCapture_;
}

bool MicRecorder::captureComplete() const {
  return M5.Mic.isRecording() == 0;
}

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
