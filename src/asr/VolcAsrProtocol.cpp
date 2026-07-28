#include "asr/VolcAsrProtocol.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace stick_s3_asr {

std::vector<uint8_t> VolcAsrProtocol::makeFullClientRequest(
    const VolcAsrConfig& config,
    const std::string& requestId,
    int32_t sequence) {
  const std::string json = buildStartJson(config, requestId);
  return makeFrame(VolcMessageType::FullClientRequest,
                   kFlagPositiveSequence,
                   kSerializationJson,
                   kCompressionNone,
                   sequence,
                   reinterpret_cast<const uint8_t*>(json.data()),
                   json.size());
}

std::vector<uint8_t> VolcAsrProtocol::makeAudioRequest(const uint8_t* pcm,
                                                       size_t length,
                                                       int32_t sequence,
                                                       bool finalFrame) {
  const int32_t wireSequence =
      finalFrame ? -std::max<int32_t>(1, sequence) : std::max<int32_t>(1, sequence);
  return makeFrame(VolcMessageType::AudioOnlyRequest,
                   finalFrame ? kFlagNegativeSequence : kFlagPositiveSequence,
                   kSerializationNone,
                   kCompressionNone,
                   wireSequence,
                   pcm,
                   length);
}

bool VolcAsrProtocol::parseHeader(const uint8_t* data,
                                  size_t length,
                                  VolcFrameHeader& header,
                                  std::string& error) {
  if (!data || length < 8) {
    error = "frame too short";
    return false;
  }

  header = {};
  header.version = data[0] >> 4;
  header.headerSizeWords = data[0] & 0x0F;
  header.messageType = static_cast<VolcMessageType>(data[1] >> 4);
  header.flags = data[1] & 0x0F;
  header.serialization = data[2] >> 4;
  header.compression = data[2] & 0x0F;
  header.finalPackage = (header.flags & 0x02) != 0;

  const size_t baseHeaderBytes = static_cast<size_t>(header.headerSizeWords) * 4;
  if (header.version != kProtocolVersion || baseHeaderBytes < 4 ||
      length < baseHeaderBytes + 4) {
    error = "invalid frame header";
    return false;
  }

  size_t cursor = baseHeaderBytes;
  header.hasSequence = (header.flags & 0x01) != 0;
  if (header.hasSequence) {
    if (length < cursor + 8) {
      error = "sequence frame too short";
      return false;
    }
    header.sequence = readInt32(data + cursor);
    cursor += 4;
  }

  if (header.messageType == VolcMessageType::ErrorResponse) {
    if (length < cursor + 8) {
      error = "error frame too short";
      return false;
    }
    header.errorCode = readUint32(data + cursor);
    cursor += 4;
    header.payloadSize = readUint32(data + cursor);
    cursor += 4;
    if (header.payloadSize > length - cursor) {
      error = "error payload truncated";
      return false;
    }
    header.payloadOffset = cursor;
    return true;
  }

  header.payloadSize = readUint32(data + cursor);
  cursor += 4;
  if (header.payloadSize > length - cursor) {
    error = "payload truncated";
    return false;
  }

  header.payloadOffset = cursor;
  return true;
}

VolcResponse VolcAsrProtocol::parseResponse(const uint8_t* data, size_t length) {
  VolcResponse response;
  VolcFrameHeader header;
  std::string error;
  if (!parseHeader(data, length, header, error)) {
    response.error = error;
    return response;
  }

  response.messageType = header.messageType;
  response.sequence = header.sequence;
  response.final = header.finalPackage || (header.hasSequence && header.sequence < 0);
  response.errorCode = header.errorCode;

  const char* payloadStart =
      reinterpret_cast<const char*>(data + header.payloadOffset);
  response.rawPayload.assign(payloadStart, payloadStart + header.payloadSize);

  if (header.compression != kCompressionNone) {
    response.error = "compressed ASR responses are not supported by this firmware";
    return response;
  }

  if (header.messageType == VolcMessageType::ErrorResponse) {
    if (response.rawPayload.empty()) {
      response.error = "ASR server error code " + std::to_string(header.errorCode);
    } else {
      response.error = "ASR server error code " + std::to_string(header.errorCode) +
                       ": " + response.rawPayload;
    }
    return response;
  }

  if (header.serialization == kSerializationJson) {
    response.text = extractJsonString(
        response.rawPayload,
        {"text", "utterance", "result", "message", "error_msg", "error"});
    const std::string event =
        extractJsonString(response.rawPayload, {"event", "status", "type"});
    const bool isLast =
        extractJsonBool(response.rawPayload, {"is_last_package", "is_last"});
    if (event == "final" || event == "sentence_end" || event == "completed" ||
        isLast) {
      response.final = true;
    }
    if (response.text.empty() && header.messageType == VolcMessageType::FullServerResponse) {
      response.text = response.rawPayload;
    }
  }

  return response;
}

std::string VolcAsrProtocol::makeConnectHeaders(const VolcAsrConfig& config,
                                                const std::string& requestId) {
  std::ostringstream headers;
  headers << "X-Api-App-Key: " << config.appKey << "\r\n";
  headers << "X-Api-Access-Key: " << config.accessKey << "\r\n";
  headers << "X-Api-Resource-Id: " << config.resourceId << "\r\n";
  headers << "X-Api-Connect-Id: " << requestId << "\r\n";
  headers << "X-Api-Request-Id: " << requestId << "\r\n";
  return headers.str();
}

std::vector<uint8_t> VolcAsrProtocol::makeFrame(VolcMessageType type,
                                                uint8_t flags,
                                                uint8_t serialization,
                                                uint8_t compression,
                                                int32_t sequence,
                                                const uint8_t* payload,
                                                size_t payloadLength) {
  std::vector<uint8_t> frame;
  frame.reserve(12 + payloadLength);
  frame.push_back(static_cast<uint8_t>((kProtocolVersion << 4) | kHeaderSizeWords));
  frame.push_back(static_cast<uint8_t>((static_cast<uint8_t>(type) << 4) |
                                       (flags & 0x0F)));
  frame.push_back(static_cast<uint8_t>((serialization << 4) | (compression & 0x0F)));
  frame.push_back(0x00);
  if (flags == kFlagPositiveSequence || flags == kFlagNegativeSequence) {
    appendInt32(frame, sequence);
  }
  appendInt32(frame, static_cast<int32_t>(payloadLength));
  if (payload && payloadLength > 0) {
    frame.insert(frame.end(), payload, payload + payloadLength);
  }
  return frame;
}

void VolcAsrProtocol::appendInt32(std::vector<uint8_t>& out, int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

uint32_t VolcAsrProtocol::readUint32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

int32_t VolcAsrProtocol::readInt32(const uint8_t* data) {
  return static_cast<int32_t>(readUint32(data));
}

std::string VolcAsrProtocol::buildStartJson(const VolcAsrConfig& config,
                                            const std::string& requestId) {
  std::ostringstream json;
  json << "{";
  json << "\"user\":{\"uid\":\"stick-s3-" << requestId << "\"},";
  json << "\"audio\":{";
  json << "\"format\":\"pcm\",";
  json << "\"codec\":\"raw\",";
  json << "\"rate\":" << config.audio.sampleRate << ",";
  json << "\"bits\":" << static_cast<int>(config.audio.bitsPerSample) << ",";
  json << "\"channel\":" << static_cast<int>(config.audio.channels);
  json << "},";
  json << "\"request\":{";
  json << "\"model_name\":\"bigmodel\",";
  json << "\"enable_punc\":" << (config.enablePunctuation ? "true" : "false")
       << ",";
  json << "\"enable_itn\":" << (config.enableItn ? "true" : "false");
  json << "}";
  json << "}";
  return json.str();
}

std::string VolcAsrProtocol::extractJsonString(
    const std::string& json,
    const std::vector<std::string>& keys) {
  for (const std::string& key : keys) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    while (keyPos != std::string::npos) {
      size_t colon = json.find(':', keyPos + needle.size());
      if (colon == std::string::npos) break;
      size_t valueStart = json.find_first_not_of(" \t\r\n", colon + 1);
      if (valueStart == std::string::npos) break;
      if (json[valueStart] == '"') {
        ++valueStart;
        std::string value;
        bool escaped = false;
        for (size_t i = valueStart; i < json.size(); ++i) {
          const char c = json[i];
          if (!escaped && c == '"') {
            return unescapeJsonString(value);
          }
          value += c;
          escaped = (!escaped && c == '\\');
          if (c != '\\') escaped = false;
        }
      }
      keyPos = json.find(needle, keyPos + needle.size());
    }
  }
  return "";
}

bool VolcAsrProtocol::extractJsonBool(const std::string& json,
                                      const std::vector<std::string>& keys) {
  for (const std::string& key : keys) {
    const std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    while (keyPos != std::string::npos) {
      size_t colon = json.find(':', keyPos + needle.size());
      if (colon == std::string::npos) break;
      size_t valueStart = json.find_first_not_of(" \t\r\n", colon + 1);
      if (valueStart == std::string::npos) break;
      if (json.compare(valueStart, 4, "true") == 0 ||
          json.compare(valueStart, 1, "1") == 0) {
        return true;
      }
      if (json.compare(valueStart, 5, "false") == 0 ||
          json.compare(valueStart, 1, "0") == 0) {
        return false;
      }
      keyPos = json.find(needle, keyPos + needle.size());
    }
  }
  return false;
}

std::string VolcAsrProtocol::unescapeJsonString(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool escaped = false;
  for (char c : value) {
    if (!escaped && c == '\\') {
      escaped = true;
      continue;
    }
    if (escaped) {
      switch (c) {
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case '"':
        case '\\':
        case '/':
          out += c;
          break;
        default:
          out += c;
          break;
      }
      escaped = false;
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace stick_s3_asr
