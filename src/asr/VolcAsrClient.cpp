#ifndef UNIT_TEST

#include "asr/VolcAsrClient.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include "asr/VolcRootCa.h"

namespace stick_s3_asr {

namespace {
constexpr uint32_t kConnectTimeoutMs = 12000;
constexpr uint32_t kHandshakeTimeoutMs = 12000;
constexpr uint32_t kSocketIoTimeoutMs = 5000;
constexpr uint32_t kFrameReadTimeoutMs = 1200;
constexpr uint32_t kStartAckTimeoutMs = 12000;
constexpr uint32_t kFinalTimeoutMs = 20000;
constexpr size_t kMaxServerPayloadBytes = 32768;
constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

uint32_t elapsedMs(uint32_t nowMs, uint32_t startedMs) {
  return nowMs >= startedMs ? nowMs - startedMs : 0;
}

bool disconnectIsRecoverable(const std::string& reason) {
  return reason.find("HTTP 401") == std::string::npos &&
         reason.find("HTTP 403") == std::string::npos;
}

std::string base64Encode(const uint8_t* data, size_t length) {
  if (!data && length > 0) return "";
  const size_t outCapacity = ((length + 2U) / 3U) * 4U + 1U;
  std::vector<unsigned char> out(outCapacity, 0);
  size_t outLength = 0;
  if (mbedtls_base64_encode(out.data(),
                            out.size(),
                            &outLength,
                            data,
                            length) != 0) {
    return "";
  }
  return std::string(reinterpret_cast<const char*>(out.data()), outLength);
}

std::string makeWebSocketKey() {
  uint8_t randomBytes[16] = {};
  for (uint8_t& byte : randomBytes) {
    byte = static_cast<uint8_t>(esp_random() & 0xFFU);
  }
  return base64Encode(randomBytes, sizeof(randomBytes));
}

std::string makeWebSocketAccept(const std::string& key) {
  const std::string input = key + kWebSocketGuid;
  uint8_t digest[20] = {};
  if (mbedtls_sha1_ret(reinterpret_cast<const unsigned char*>(input.data()),
                       input.size(),
                       digest) != 0) {
    return "";
  }
  return base64Encode(digest, sizeof(digest));
}

std::string trimAscii(const std::string& value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

void appendUint16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendUint64(std::vector<uint8_t>& out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFU));
  }
}

uint16_t readUint16(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) |
         static_cast<uint16_t>(data[1]);
}

uint64_t readUint64(const uint8_t* data) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value = (value << 8) | static_cast<uint64_t>(data[i]);
  }
  return value;
}
}

bool VolcAsrClient::begin(const VolcAsrConfig& config,
                          const std::string& requestId) {
  cancel();
  config_ = config;
  requestId_ = requestId;
  result_ = {};
  sequence_ = 1;
  startSent_ = false;
  startAcked_ = false;
  startPending_ = false;
  finalSent_ = false;
  connectedOnce_ = false;
  responseCount_ = 0;
  startSentMs_ = 0;
  recoverableNetworkFailure_ = false;

  state_ = State::Connecting;
  lastActivityMs_ = millis();
  lastAudioMs_ = lastActivityMs_;

  if (!parseEndpoint(config_.endpoint)) {
    fail("Bad ASR endpoint");
    return false;
  }

  IPAddress resolvedIp;
  if (WiFi.hostByName(host_.c_str(), resolvedIp)) {
    const String ipText = resolvedIp.toString();
    Serial.printf("[asr] dns host=%s ip=%s\n", host_.c_str(), ipText.c_str());
  } else {
    Serial.printf("[asr] dns failed host=%s\n", host_.c_str());
  }

  extraHeaders_ =
      VolcAsrProtocol::makeConnectHeaders(config_, requestId_).c_str();
  Serial.printf("[asr] connect host=%s port=%u path=%s transport=custom-wss\n",
                host_.c_str(),
                static_cast<unsigned>(port_),
                path_.c_str());
  return connectWebSocket();
}

void VolcAsrClient::loop(uint32_t nowMs) {
  if (state_ == State::Disconnected || state_ == State::Completed ||
      state_ == State::Failed) {
    return;
  }

  if (state_ == State::Open && startPending_ && !startSent_) {
    startPending_ = false;
    sendStartFrame();
  }

  while (client_.available() > 0 &&
         state_ != State::Completed && state_ != State::Failed) {
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
    if (!readWebSocketFrame(opcode, payload)) {
      fail("ASR frame read failed", true);
      return;
    }
    handleWebSocketFrame(opcode,
                         payload.empty() ? nullptr : payload.data(),
                         payload.size());
  }

  if (state_ != State::Completed && state_ != State::Failed &&
      !client_.connected() && client_.available() == 0) {
    fail("ASR disconnected", true);
    return;
  }

  if (state_ == State::Connecting &&
      elapsedMs(nowMs, lastActivityMs_) > kConnectTimeoutMs) {
    fail("ASR connect timeout", true);
  } else if (state_ == State::Open && startSent_ && !startAcked_ &&
             elapsedMs(nowMs, startSentMs_) > kStartAckTimeoutMs) {
    fail("ASR start timeout", true);
  } else if (state_ == State::Recognizing &&
             elapsedMs(nowMs, lastAudioMs_) > kFinalTimeoutMs) {
    fail("ASR timeout", true);
  }
}

bool VolcAsrClient::sendAudio(const uint8_t* pcm, size_t length) {
  if (!readyForAudio() || !pcm || length == 0) return false;
  const auto frame =
      VolcAsrProtocol::makeAudioRequest(pcm, length, ++sequence_, false);
  const bool ok = sendWebSocketFrame(0x2, frame.data(), frame.size());
  if (ok) lastAudioMs_ = millis();
  return ok;
}

bool VolcAsrClient::finish() {
  if (finalSent_ || state_ == State::Completed || state_ == State::Failed) {
    return true;
  }
  if (!startSent_ || !client_.connected()) {
    fail("ASR not ready", true);
    return false;
  }
  const auto frame = VolcAsrProtocol::makeAudioRequest(nullptr, 0, ++sequence_, true);
  finalSent_ = sendWebSocketFrame(0x2, frame.data(), frame.size());
  if (!finalSent_) {
    fail("ASR final failed", true);
    return false;
  }
  state_ = State::Recognizing;
  lastAudioMs_ = millis();
  return true;
}

void VolcAsrClient::cancel() {
  client_.stop();
  state_ = State::Disconnected;
  startSent_ = false;
  startAcked_ = false;
  startPending_ = false;
  finalSent_ = false;
  connectedOnce_ = false;
  responseCount_ = 0;
  startSentMs_ = 0;
  recoverableNetworkFailure_ = false;
}

void VolcAsrClient::handleResponse(const uint8_t* payload, size_t length) {
  const VolcResponse response = VolcAsrProtocol::parseResponse(payload, length);
  ++responseCount_;
  Serial.printf("[asr] response type=%u seq=%ld final=%d textLen=%u rawLen=%u\n",
                static_cast<unsigned>(response.messageType),
                static_cast<long>(response.sequence),
                response.final ? 1 : 0,
                static_cast<unsigned>(response.text.size()),
                static_cast<unsigned>(response.rawPayload.size()));
  if (!response.error.empty()) {
    fail(response.error);
    return;
  }
  if (!startAcked_ &&
      (response.messageType == VolcMessageType::FullServerResponse ||
       response.messageType == VolcMessageType::ServerAck)) {
    startAcked_ = true;
    Serial.println("[asr] start acknowledged");
  }
  if (!response.text.empty()) {
    result_.text = response.text;
    result_.final = response.final;
  }
  if (response.final) {
    result_.final = true;
    state_ = State::Completed;
    client_.stop();
  }
}

void VolcAsrClient::handleTextResponse(const uint8_t* payload, size_t length) {
  const std::string text =
      payload ? std::string(reinterpret_cast<const char*>(payload),
                            reinterpret_cast<const char*>(payload) + length)
              : "";
  VolcResponse response;
  response.rawPayload = text;
  response.text = VolcAsrProtocol::parseResponse(payload, length).text;
  if (response.text.empty()) {
    result_.text = text;
  } else {
    result_.text = response.text;
  }
}

bool VolcAsrClient::connectWebSocket() {
  client_.stop();
  client_.setCACert(kVolcRootCaPem);
  client_.setTimeout(kSocketIoTimeoutMs / 1000U);
  client_.setHandshakeTimeout(kHandshakeTimeoutMs / 1000U);

  if (!client_.connect(host_.c_str(), port_)) {
    fail("ASR connect failed", true);
    return false;
  }

  const std::string key = makeWebSocketKey();
  if (key.empty()) {
    fail("ASR websocket key failed", true);
    return false;
  }

  std::ostringstream request;
  request << "GET " << path_.c_str() << " HTTP/1.1\r\n";
  request << "Host: " << host_.c_str();
  if (port_ != 443) {
    request << ":" << static_cast<unsigned>(port_);
  }
  request << "\r\n";
  request << "Upgrade: websocket\r\n";
  request << "Connection: Upgrade\r\n";
  request << "Sec-WebSocket-Key: " << key << "\r\n";
  request << "Sec-WebSocket-Version: 13\r\n";
  request << extraHeaders_.c_str();
  request << "\r\n";

  const std::string handshake = request.str();
  if (!writeAll(reinterpret_cast<const uint8_t*>(handshake.data()),
                handshake.size())) {
    fail("ASR handshake send failed", true);
    return false;
  }

  if (!readHandshakeResponse(key)) {
    return false;
  }

  Serial.println("[asr] websocket connected");
  connectedOnce_ = true;
  state_ = State::Open;
  startPending_ = true;
  lastActivityMs_ = millis();
  lastAudioMs_ = lastActivityMs_;
  return true;
}

bool VolcAsrClient::readHandshakeResponse(const std::string& key) {
  const std::string expectedAccept = makeWebSocketAccept(key);
  if (expectedAccept.empty()) {
    fail("ASR accept key failed", true);
    return false;
  }

  std::string line;
  if (!readHttpLine(line, kHandshakeTimeoutMs)) {
    fail("ASR handshake timeout", true);
    return false;
  }

  Serial.printf("[asr] handshake status=%s\n", line.c_str());
  if (line.find(" 101 ") == std::string::npos &&
      line.rfind("HTTP/1.1 101", 0) != 0 &&
      line.rfind("HTTP/1.0 101", 0) != 0) {
    fail("ASR handshake failed: " + line, disconnectIsRecoverable(line));
    return false;
  }

  bool upgradeOk = false;
  bool connectionOk = false;
  bool acceptOk = false;
  while (readHttpLine(line, kHandshakeTimeoutMs)) {
    if (line.empty()) break;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string name = lowerAscii(trimAscii(line.substr(0, colon)));
    const std::string value = trimAscii(line.substr(colon + 1));
    const std::string lowerValue = lowerAscii(value);
    if (name == "upgrade" && lowerValue == "websocket") {
      upgradeOk = true;
    } else if (name == "connection" &&
               lowerValue.find("upgrade") != std::string::npos) {
      connectionOk = true;
    } else if (name == "sec-websocket-accept" && value == expectedAccept) {
      acceptOk = true;
    }
  }

  if (!upgradeOk || !connectionOk || !acceptOk) {
    Serial.printf("[asr] handshake invalid upgrade=%d connection=%d accept=%d\n",
                  upgradeOk ? 1 : 0,
                  connectionOk ? 1 : 0,
                  acceptOk ? 1 : 0);
    fail("ASR handshake invalid", true);
    return false;
  }

  return true;
}

bool VolcAsrClient::readHttpLine(std::string& line, uint32_t timeoutMs) {
  line.clear();
  const uint32_t startedMs = millis();
  while (elapsedMs(millis(), startedMs) <= timeoutMs) {
    while (client_.available() > 0) {
      const int value = client_.read();
      if (value < 0) break;
      const char c = static_cast<char>(value);
      if (c == '\r') continue;
      if (c == '\n') return true;
      if (line.size() >= 512) return false;
      line += c;
    }
    if (!client_.connected() && client_.available() == 0) return false;
    delay(1);
  }
  return false;
}

bool VolcAsrClient::readExact(uint8_t* out, size_t length, uint32_t timeoutMs) {
  if (!out && length > 0) return false;
  size_t offset = 0;
  const uint32_t startedMs = millis();
  while (offset < length && elapsedMs(millis(), startedMs) <= timeoutMs) {
    const int available = client_.available();
    if (available > 0) {
      const size_t wanted =
          std::min(length - offset, static_cast<size_t>(available));
      const int read = client_.read(out + offset, wanted);
      if (read > 0) {
        offset += static_cast<size_t>(read);
        continue;
      }
    }
    if (!client_.connected() && client_.available() == 0) return false;
    delay(1);
  }
  return offset == length;
}

bool VolcAsrClient::readWebSocketFrame(uint8_t& opcode,
                                       std::vector<uint8_t>& payload) {
  uint8_t header[2] = {};
  if (!readExact(header, sizeof(header), kFrameReadTimeoutMs)) return false;

  const bool fin = (header[0] & 0x80U) != 0;
  opcode = header[0] & 0x0FU;
  const bool masked = (header[1] & 0x80U) != 0;
  uint64_t payloadLength = header[1] & 0x7FU;

  if (payloadLength == 126) {
    uint8_t extended[2] = {};
    if (!readExact(extended, sizeof(extended), kFrameReadTimeoutMs)) return false;
    payloadLength = readUint16(extended);
  } else if (payloadLength == 127) {
    uint8_t extended[8] = {};
    if (!readExact(extended, sizeof(extended), kFrameReadTimeoutMs)) return false;
    payloadLength = readUint64(extended);
  }

  if (!fin || payloadLength > kMaxServerPayloadBytes) {
    Serial.printf("[asr] bad websocket frame fin=%d opcode=%u len=%lu\n",
                  fin ? 1 : 0,
                  static_cast<unsigned>(opcode),
                  static_cast<unsigned long>(payloadLength));
    return false;
  }

  uint8_t maskKey[4] = {};
  if (masked && !readExact(maskKey, sizeof(maskKey), kFrameReadTimeoutMs)) {
    return false;
  }

  payload.assign(static_cast<size_t>(payloadLength), 0);
  if (!payload.empty() &&
      !readExact(payload.data(), payload.size(), kFrameReadTimeoutMs)) {
    return false;
  }

  if (masked) {
    for (size_t i = 0; i < payload.size(); ++i) {
      payload[i] ^= maskKey[i % 4U];
    }
  }

  return true;
}

void VolcAsrClient::handleWebSocketFrame(uint8_t opcode,
                                         const uint8_t* payload,
                                         size_t length) {
  lastActivityMs_ = millis();
  switch (opcode) {
    case 0x1:
      handleTextResponse(payload, length);
      break;
    case 0x2:
      handleResponse(payload, length);
      break;
    case 0x8:
      {
        std::string reason;
        if (payload && length > 2) {
          reason.assign(reinterpret_cast<const char*>(payload + 2),
                        reinterpret_cast<const char*>(payload) + length);
        }
        Serial.printf("[asr] websocket disconnected state=%u reason=%s\n",
                      static_cast<unsigned>(state_),
                      reason.empty() ? "-" : reason.c_str());
        if (state_ != State::Completed && state_ != State::Disconnected &&
            state_ != State::Failed) {
          const std::string message =
              reason.empty() ? "ASR disconnected" : "ASR disconnected: " + reason;
          fail(message, disconnectIsRecoverable(reason));
        }
      }
      break;
    case 0x9:
      sendWebSocketFrame(0xA, payload, length);
      break;
    case 0xA:
      break;
    default:
      Serial.printf("[asr] websocket ignored opcode=%u len=%u\n",
                    static_cast<unsigned>(opcode),
                    static_cast<unsigned>(length));
      break;
  }
}

bool VolcAsrClient::sendWebSocketFrame(uint8_t opcode,
                                       const uint8_t* payload,
                                       size_t length) {
  if (!client_.connected() || (!payload && length > 0)) return false;

  std::vector<uint8_t> frame;
  frame.reserve(14 + length);
  frame.push_back(static_cast<uint8_t>(0x80U | (opcode & 0x0FU)));
  if (length < 126) {
    frame.push_back(static_cast<uint8_t>(0x80U | length));
  } else if (length <= 0xFFFFU) {
    frame.push_back(static_cast<uint8_t>(0x80U | 126U));
    appendUint16(frame, static_cast<uint16_t>(length));
  } else {
    frame.push_back(static_cast<uint8_t>(0x80U | 127U));
    appendUint64(frame, static_cast<uint64_t>(length));
  }

  uint8_t maskKey[4] = {};
  uint32_t maskWord = esp_random();
  for (uint8_t& byte : maskKey) {
    byte = static_cast<uint8_t>(maskWord & 0xFFU);
    maskWord >>= 8;
  }
  frame.insert(frame.end(), maskKey, maskKey + sizeof(maskKey));

  for (size_t i = 0; i < length; ++i) {
    frame.push_back(payload[i] ^ maskKey[i % 4U]);
  }

  return writeAll(frame.data(), frame.size());
}

bool VolcAsrClient::writeAll(const uint8_t* data, size_t length) {
  if (!data && length > 0) return false;
  size_t offset = 0;
  const uint32_t startedMs = millis();
  while (offset < length && elapsedMs(millis(), startedMs) <= kSocketIoTimeoutMs) {
    const size_t written = client_.write(data + offset, length - offset);
    if (written > 0) {
      offset += written;
      continue;
    }
    if (!client_.connected()) return false;
    delay(1);
  }
  return offset == length;
}

bool VolcAsrClient::sendStartFrame() {
  const auto frame =
      VolcAsrProtocol::makeFullClientRequest(config_, requestId_, sequence_);
  startSent_ = sendWebSocketFrame(0x2, frame.data(), frame.size());
  if (startSent_) {
    startSentMs_ = millis();
  }
  Serial.printf("[asr] start sent ok=%d bytes=%u\n",
                startSent_ ? 1 : 0,
                static_cast<unsigned>(frame.size()));
  if (!startSent_) {
    fail("ASR init failed", true);
  }
  return startSent_;
}

bool VolcAsrClient::parseEndpoint(const std::string& endpoint) {
  std::string rest = endpoint;
  if (rest.rfind("wss://", 0) == 0) {
    rest.erase(0, 6);
    port_ = 443;
  } else {
    return false;
  }

  const size_t slash = rest.find('/');
  const std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
  path_ = slash == std::string::npos ? "/" : rest.substr(slash).c_str();
  if (hostPort.empty()) return false;

  const size_t colon = hostPort.find(':');
  if (colon == std::string::npos) {
    host_ = hostPort.c_str();
  } else {
    const std::string host = hostPort.substr(0, colon);
    const std::string portText = hostPort.substr(colon + 1);
    if (host.empty() || portText.empty()) return false;
    uint32_t parsedPort = 0;
    for (char c : portText) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
      parsedPort = parsedPort * 10U + static_cast<uint32_t>(c - '0');
      if (parsedPort > 65535U) return false;
    }
    if (parsedPort == 0) return false;
    host_ = host.c_str();
    port_ = static_cast<uint16_t>(parsedPort);
  }
  return host_.length() > 0 && path_.length() > 0;
}

void VolcAsrClient::fail(const std::string& message, bool recoverableNetwork) {
  Serial.printf("[asr] fail %s recovery=%d\n",
                message.c_str(),
                recoverableNetwork ? 1 : 0);
  result_.error = message;
  result_.final = true;
  recoverableNetworkFailure_ = recoverableNetwork;
  state_ = State::Failed;
  client_.stop();
}

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
