#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace stick_s3_asr {

enum class AppMode {
  Boot,
  Idle,
  Pairing,
  Connecting,
  Recording,
  Recognizing,
  Result,
  Error,
};

struct AudioFormat {
  uint32_t sampleRate = 16000;
  uint8_t bitsPerSample = 16;
  uint8_t channels = 1;
};

struct VolcAsrConfig {
  std::string endpoint = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel";
  std::string appKey;
  std::string accessKey;
  std::string resourceId = "volc.seedasr.sauc.duration";
  AudioFormat audio;
  bool enablePunctuation = true;
  bool enableItn = true;
};

struct TranscriptionResult {
  std::string text;
  std::string error;
  bool final = false;
};

}  // namespace stick_s3_asr
