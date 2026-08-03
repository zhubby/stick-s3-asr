#pragma once

#ifndef UNIT_TEST

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <string>
#include <vector>

#include "AppTypes.h"
#include "asr/VolcAsrProtocol.h"

namespace stick_s3_asr {

class VolcAsrClient {
 public:
  enum class State {
    Disconnected,
    Connecting,
    Open,
    Recognizing,
    Completed,
    Failed,
  };

  bool begin(const VolcAsrConfig& config, const std::string& requestId);
  void loop(uint32_t nowMs);
  bool sendAudio(const uint8_t* pcm, size_t length);
  bool finish();
  void cancel();

  bool readyForAudio() const {
    return state_ == State::Open && startSent_ && startAcked_;
  }
  bool done() const { return state_ == State::Completed || state_ == State::Failed; }
  bool failed() const { return state_ == State::Failed; }
  State state() const { return state_; }
  const TranscriptionResult& result() const { return result_; }
  bool recoverableNetworkError() const { return recoverableNetworkFailure_; }
  bool connectedOnce() const { return connectedOnce_; }
  uint32_t responseCount() const { return responseCount_; }

 private:
  void handleResponse(const uint8_t* payload, size_t length);
  void handleTextResponse(const uint8_t* payload, size_t length);
  bool connectWebSocket();
  bool readHandshakeResponse(const std::string& key);
  bool readHttpLine(std::string& line, uint32_t timeoutMs);
  bool readExact(uint8_t* out, size_t length, uint32_t timeoutMs);
  bool readWebSocketFrame(uint8_t& opcode, std::vector<uint8_t>& payload);
  void handleWebSocketFrame(uint8_t opcode, const uint8_t* payload, size_t length);
  bool sendWebSocketFrame(uint8_t opcode, const uint8_t* payload, size_t length);
  bool writeAll(const uint8_t* data, size_t length);
  bool sendStartFrame();
  bool parseEndpoint(const std::string& endpoint);
  void fail(const std::string& message, bool recoverableNetwork = false);

  WiFiClientSecure client_;
  VolcAsrConfig config_;
  std::string requestId_;
  String extraHeaders_;
  String host_;
  String path_ = "/";
  uint16_t port_ = 443;
  State state_ = State::Disconnected;
  bool startSent_ = false;
  bool startAcked_ = false;
  bool startPending_ = false;
  bool finalSent_ = false;
  bool connectedOnce_ = false;
  int32_t sequence_ = 1;
  uint32_t startSentMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastAudioMs_ = 0;
  uint32_t responseCount_ = 0;
  bool recoverableNetworkFailure_ = false;
  TranscriptionResult result_;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
