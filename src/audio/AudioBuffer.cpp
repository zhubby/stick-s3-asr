#include "audio/AudioBuffer.h"

#include <algorithm>
#include <cstdlib>

#if defined(ESP32) && !defined(UNIT_TEST)
#include <esp_heap_caps.h>
#endif

namespace stick_s3_asr {

AudioRingBuffer::AudioRingBuffer(size_t capacityBytes) {
  reset(capacityBytes);
}

AudioRingBuffer::~AudioRingBuffer() {
  release();
}

bool AudioRingBuffer::reset(size_t capacityBytes) {
  release();
  if (capacityBytes > 0) {
#if defined(ESP32) && !defined(UNIT_TEST)
    buffer_ = static_cast<uint8_t*>(
        heap_caps_malloc(capacityBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    allocatedInPsram_ = buffer_ != nullptr;
    if (!buffer_) {
      buffer_ = static_cast<uint8_t*>(
          heap_caps_malloc(capacityBytes, MALLOC_CAP_8BIT));
    }
#else
    buffer_ = static_cast<uint8_t*>(std::malloc(capacityBytes));
#endif
    if (!buffer_) {
      capacity_ = 0;
      clear();
      overflowCount_ = 0;
      allocatedInPsram_ = false;
      return false;
    }
    capacity_ = capacityBytes;
  }
  clear();
  overflowCount_ = 0;
  return true;
}

void AudioRingBuffer::clear() {
  head_ = 0;
  tail_ = 0;
  used_ = 0;
}

size_t AudioRingBuffer::write(const uint8_t* data, size_t length) {
  if (!data || !buffer_ || capacity_ == 0 || length == 0) return 0;

  const size_t accepted = std::min(length, free());
  for (size_t i = 0; i < accepted; ++i) {
    buffer_[head_] = data[i];
    head_ = (head_ + 1) % capacity_;
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
  if (!out || !buffer_ || capacity_ == 0 || length == 0) return 0;

  const size_t count = std::min(length, available());
  for (size_t i = 0; i < count; ++i) {
    out[i] = buffer_[tail_];
    tail_ = (tail_ + 1) % capacity_;
  }
  used_ -= count;
  return count;
}

void AudioRingBuffer::release() {
  if (buffer_) {
#if defined(ESP32) && !defined(UNIT_TEST)
    heap_caps_free(buffer_);
#else
    std::free(buffer_);
#endif
  }
  buffer_ = nullptr;
  capacity_ = 0;
  allocatedInPsram_ = false;
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
