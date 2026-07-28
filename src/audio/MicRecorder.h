#pragma once

#ifndef UNIT_TEST

#include <cstddef>
#include <cstdint>
#include <vector>

#include "AppTypes.h"
#include "audio/AudioBuffer.h"

namespace stick_s3_asr {

class MicRecorder {
 public:
  bool begin(const AudioFormat& format,
             uint16_t chunkMs = 100,
             uint16_t maxRecordSeconds = 20,
             size_t queuedChunks = 4);

  bool start(uint32_t nowMs);
  void requestStop();
  void cancel();
  void update(uint32_t nowMs);

  size_t readPcm(uint8_t* out, size_t maxBytes);
  size_t queuedBytes() const { return ring_.available(); }
  size_t chunkBytes() const { return capture_.size() * sizeof(int16_t); }
  bool active() const { return active_; }
  bool stopping() const { return stopRequested_; }
  bool finished() const;
  bool timedOut() const { return timedOut_; }
  bool overflowed() const { return overflowed_; }
  uint16_t lastPeak() const { return lastPeak_; }
  uint32_t recordedMs(uint32_t nowMs) const;
  size_t overflowCount() const { return ring_.overflowCount(); }

 private:
  bool scheduleChunk();
  bool captureComplete() const;

  AudioFormat format_;
  uint16_t chunkMs_ = 100;
  uint16_t maxRecordSeconds_ = 20;
  std::vector<int16_t> capture_;
  AudioRingBuffer ring_;
  bool initialized_ = false;
  bool active_ = false;
  bool pendingCapture_ = false;
  bool stopRequested_ = false;
  bool timedOut_ = false;
  bool overflowed_ = false;
  uint16_t lastPeak_ = 0;
  uint32_t startedAtMs_ = 0;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
