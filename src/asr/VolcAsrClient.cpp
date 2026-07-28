#ifndef UNIT_TEST

#include "asr/VolcAsrClient.h"

#include <algorithm>
#include <cctype>

#include "asr/VolcRootCa.h"

namespace stick_s3_asr {

namespace {
constexpr uint32_t kConnectTimeoutMs = 12000;
constexpr uint32_t kFinalTimeoutMs = 20000;
}

bool VolcAsrClient::begin(const VolcAsrConfig& config,
                          const std::string& requestId) {
  cancel();
  config_ = config;
  requestId_ = requestId;
  result_ = {};
  sequence_ = 1;
  startSent_ = false;
  finalSent_ = false;

  if (!parseEndpoint(config_.endpoint)) {
    fail("ASR endpoint 格式无效");
    return false;
  }

  extraHeaders_ =
      VolcAsrProtocol::makeConnectHeaders(config_, requestId_).c_str();
  webSocket_.setExtraHeaders(extraHeaders_.c_str());
  webSocket_.setReconnectInterval(0);
  webSocket_.enableHeartbeat(15000, 3000, 2);
  webSocket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    handleEvent(type, payload, length);
  });

  webSocket_.beginSslWithCA(host_.c_str(), port_, path_.c_str(), kVolcRootCaPem, "");

  state_ = State::Connecting;
  lastActivityMs_ = millis();
  lastAudioMs_ = lastActivityMs_;
  return true;
}

void VolcAsrClient::loop(uint32_t nowMs) {
  if (state_ == State::Disconnected || state_ == State::Completed ||
      state_ == State::Failed) {
    return;
  }

  webSocket_.loop();

  if (state_ == State::Connecting && nowMs - lastActivityMs_ > kConnectTimeoutMs) {
    fail("ASR 连接超时");
  } else if (state_ == State::Recognizing &&
             nowMs - lastAudioMs_ > kFinalTimeoutMs) {
    fail("ASR 识别超时");
  }
}

bool VolcAsrClient::sendAudio(const uint8_t* pcm, size_t length) {
  if (!readyForAudio() || !pcm || length == 0) return false;
  const auto frame =
      VolcAsrProtocol::makeAudioRequest(pcm, length, ++sequence_, false);
  const bool ok = webSocket_.sendBIN(frame.data(), frame.size());
  if (ok) lastAudioMs_ = millis();
  return ok;
}

bool VolcAsrClient::finish() {
  if (finalSent_ || state_ == State::Completed || state_ == State::Failed) {
    return true;
  }
  if (!startSent_ || !webSocket_.isConnected()) {
    fail("ASR 未连接，无法结束录音");
    return false;
  }
  const auto frame = VolcAsrProtocol::makeAudioRequest(nullptr, 0, ++sequence_, true);
  finalSent_ = webSocket_.sendBIN(frame.data(), frame.size());
  if (!finalSent_) {
    fail("ASR 结束帧发送失败");
    return false;
  }
  state_ = State::Recognizing;
  lastAudioMs_ = millis();
  return true;
}

void VolcAsrClient::cancel() {
  webSocket_.disconnect();
  state_ = State::Disconnected;
  startSent_ = false;
  finalSent_ = false;
}

void VolcAsrClient::handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  lastActivityMs_ = millis();
  switch (type) {
    case WStype_CONNECTED:
      state_ = State::Open;
      sendStartFrame();
      break;
    case WStype_DISCONNECTED:
      if (state_ != State::Completed && state_ != State::Disconnected &&
          state_ != State::Failed) {
        fail("ASR 连接已断开");
      }
      break;
    case WStype_BIN:
      handleResponse(payload, length);
      break;
    case WStype_TEXT:
      handleTextResponse(payload, length);
      break;
    case WStype_ERROR:
      fail("ASR WebSocket 错误");
      break;
    default:
      break;
  }
}

void VolcAsrClient::handleResponse(const uint8_t* payload, size_t length) {
  const VolcResponse response = VolcAsrProtocol::parseResponse(payload, length);
  if (!response.error.empty()) {
    fail(response.error);
    return;
  }
  if (!response.text.empty()) {
    result_.text = response.text;
    result_.final = response.final;
  }
  if (response.final) {
    result_.final = true;
    state_ = State::Completed;
    webSocket_.disconnect();
  }
}

void VolcAsrClient::handleTextResponse(const uint8_t* payload, size_t length) {
  std::string text(reinterpret_cast<const char*>(payload),
                   reinterpret_cast<const char*>(payload) + length);
  VolcResponse response;
  response.rawPayload = text;
  response.text = VolcAsrProtocol::parseResponse(payload, length).text;
  if (response.text.empty()) {
    result_.text = text;
  } else {
    result_.text = response.text;
  }
}

bool VolcAsrClient::sendStartFrame() {
  const auto frame =
      VolcAsrProtocol::makeFullClientRequest(config_, requestId_, sequence_);
  startSent_ = webSocket_.sendBIN(frame.data(), frame.size());
  if (!startSent_) {
    fail("ASR 初始化帧发送失败");
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

void VolcAsrClient::fail(const std::string& message) {
  result_.error = message;
  result_.final = true;
  state_ = State::Failed;
  webSocket_.disconnect();
}

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
