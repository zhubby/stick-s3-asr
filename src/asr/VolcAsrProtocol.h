#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppTypes.h"

namespace stick_s3_asr {

enum class VolcMessageType : uint8_t {
  FullClientRequest = 0x1,
  AudioOnlyRequest = 0x2,
  FullServerResponse = 0x9,
  ServerAck = 0xB,
  ErrorResponse = 0xF,
  Unknown = 0x0,
};

struct VolcFrameHeader {
  uint8_t version = 0;
  uint8_t headerSizeWords = 0;
  VolcMessageType messageType = VolcMessageType::Unknown;
  uint8_t flags = 0;
  uint8_t serialization = 0;
  uint8_t compression = 0;
  int32_t sequence = 0;
  bool hasSequence = false;
  bool finalPackage = false;
  uint32_t errorCode = 0;
  uint32_t payloadSize = 0;
  size_t payloadOffset = 0;
};

struct VolcResponse {
  VolcMessageType messageType = VolcMessageType::Unknown;
  int32_t sequence = 0;
  bool final = false;
  uint32_t errorCode = 0;
  std::string text;
  std::string error;
  std::string rawPayload;
};

class VolcAsrProtocol {
 public:
  static constexpr uint8_t kProtocolVersion = 0x1;
  static constexpr uint8_t kHeaderSizeWords = 0x1;
  static constexpr uint8_t kFlagNoSequence = 0x0;
  static constexpr uint8_t kFlagPositiveSequence = 0x1;
  static constexpr uint8_t kFlagFinalNoSequence = 0x2;
  static constexpr uint8_t kFlagNegativeSequence = 0x3;
  static constexpr uint8_t kSerializationNone = 0x0;
  static constexpr uint8_t kSerializationJson = 0x1;
  static constexpr uint8_t kCompressionNone = 0x0;

  static std::vector<uint8_t> makeFullClientRequest(const VolcAsrConfig& config,
                                                    const std::string& requestId,
                                                    int32_t sequence);
  static std::vector<uint8_t> makeAudioRequest(const uint8_t* pcm,
                                               size_t length,
                                               int32_t sequence,
                                               bool finalFrame);
  static bool parseHeader(const uint8_t* data,
                          size_t length,
                          VolcFrameHeader& header,
                          std::string& error);
  static VolcResponse parseResponse(const uint8_t* data, size_t length);
  static std::string makeConnectHeaders(const VolcAsrConfig& config,
                                        const std::string& requestId);

 private:
  static std::vector<uint8_t> makeFrame(VolcMessageType type,
                                        uint8_t flags,
                                        uint8_t serialization,
                                        uint8_t compression,
                                        int32_t sequence,
                                        const uint8_t* payload,
                                        size_t payloadLength);
  static void appendInt32(std::vector<uint8_t>& out, int32_t value);
  static uint32_t readUint32(const uint8_t* data);
  static int32_t readInt32(const uint8_t* data);
  static std::string buildStartJson(const VolcAsrConfig& config,
                                    const std::string& requestId);
  static std::string extractJsonString(const std::string& json,
                                       const std::vector<std::string>& keys);
  static bool extractJsonBool(const std::string& json,
                              const std::vector<std::string>& keys);
  static std::string unescapeJsonString(const std::string& value);
};

}  // namespace stick_s3_asr
