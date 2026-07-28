#pragma once

#ifndef UNIT_TEST

#include <Arduino.h>
#include <WebSocketsClient.h>

#include <string>

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

  bool readyForAudio() const { return state_ == State::Open && startSent_; }
  bool done() const { return state_ == State::Completed || state_ == State::Failed; }
  bool failed() const { return state_ == State::Failed; }
  State state() const { return state_; }
  const TranscriptionResult& result() const { return result_; }

 private:
  void handleEvent(WStype_t type, uint8_t* payload, size_t length);
  void handleResponse(const uint8_t* payload, size_t length);
  void handleTextResponse(const uint8_t* payload, size_t length);
  bool sendStartFrame();
  bool parseEndpoint(const std::string& endpoint);
  void fail(const std::string& message);

  WebSocketsClient webSocket_;
  VolcAsrConfig config_;
  std::string requestId_;
  String extraHeaders_;
  String host_;
  String path_ = "/";
  uint16_t port_ = 443;
  State state_ = State::Disconnected;
  bool startSent_ = false;
  bool finalSent_ = false;
  int32_t sequence_ = 1;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastAudioMs_ = 0;
  TranscriptionResult result_;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
