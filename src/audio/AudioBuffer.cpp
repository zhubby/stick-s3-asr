#include "audio/AudioBuffer.h"

#include <algorithm>
#include <cstdlib>

namespace stick_s3_asr {

AudioRingBuffer::AudioRingBuffer(size_t capacityBytes) {
  reset(capacityBytes);
}

void AudioRingBuffer::reset(size_t capacityBytes) {
  buffer_.assign(capacityBytes, 0);
  clear();
  overflowCount_ = 0;
}

void AudioRingBuffer::clear() {
  head_ = 0;
  tail_ = 0;
  used_ = 0;
}

size_t AudioRingBuffer::write(const uint8_t* data, size_t length) {
  if (!data || buffer_.empty() || length == 0) return 0;

  const size_t accepted = std::min(length, free());
  for (size_t i = 0; i < accepted; ++i) {
    buffer_[head_] = data[i];
    head_ = (head_ + 1) % buffer_.size();
  }
  used_ += accepted;
  if (accepted < length) {
    ++overflowCount_;
  }
  return accepted;
}

bool AudioRingBuffer::writeAll(const uint8_t* data, size_t length) {
  if (length > free()) {
    ++overflowCount_;
    return false;
  }
  return write(data, length) == length;
}

size_t AudioRingBuffer::read(uint8_t* out, size_t length) {
  if (!out || buffer_.empty() || length == 0) return 0;

  const size_t count = std::min(length, available());
  for (size_t i = 0; i < count; ++i) {
    out[i] = buffer_[tail_];
    tail_ = (tail_ + 1) % buffer_.size();
  }
  used_ -= count;
  return count;
}

uint16_t pcmPeak(const int16_t* samples, size_t sampleCount) {
  uint16_t peak = 0;
  for (size_t i = 0; samples && i < sampleCount; ++i) {
    const int32_t value = samples[i];
    const uint16_t absValue = static_cast<uint16_t>(value < 0 ? -value : value);
    if (absValue > peak) peak = absValue;
  }
  return peak;
}

}  // namespace stick_s3_asr
