#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stick_s3_asr {

class AudioRingBuffer {
 public:
  explicit AudioRingBuffer(size_t capacityBytes = 0);

  void reset(size_t capacityBytes);
  void clear();

  size_t write(const uint8_t* data, size_t length);
  bool writeAll(const uint8_t* data, size_t length);
  size_t read(uint8_t* out, size_t length);

  size_t capacity() const { return buffer_.size(); }
  size_t available() const { return used_; }
  size_t free() const { return buffer_.size() - used_; }
  size_t overflowCount() const { return overflowCount_; }
  bool empty() const { return used_ == 0; }

 private:
  std::vector<uint8_t> buffer_;
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t used_ = 0;
  size_t overflowCount_ = 0;
};

uint16_t pcmPeak(const int16_t* samples, size_t sampleCount);

}  // namespace stick_s3_asr
