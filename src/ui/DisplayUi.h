#pragma once

#ifndef UNIT_TEST

#include <cstddef>
#include <cstdint>
#include <string>

#include <M5Unified.h>

#include "AppTypes.h"

namespace stick_s3_asr {

struct UiState {
  AppMode mode = AppMode::Boot;
  bool wifiConnected = false;
  bool wifiConfigured = false;
  bool asrReady = false;
  bool pairingActive = false;
  int batteryLevel = -1;
  uint16_t peak = 0;
  uint32_t recordingMs = 0;
  size_t pageIndex = 0;
  size_t pageCount = 1;
  std::string pageText;
  std::string errorText;
  std::string pairingSsid;
  std::string pairingPassword;
  std::string pairingUrl;
};

class DisplayUi {
 public:
  void begin(uint8_t initialRotation = 1);
  void setRotation(uint8_t rotation);
  void render(const UiState& state, uint32_t nowMs);

 private:
  std::string makeSignature(const UiState& state) const;
  bool landscape() const;
  int panelY() const;
  int panelH() const;
  int footerY() const;
  int textLineLimit() const;

  void drawFrame(const UiState& state);
  void drawHeader(const UiState& state);
  void drawFooter(const UiState& state);
  void drawIdle(const UiState& state);
  void drawPairing(const UiState& state);
  void drawConnecting(const UiState& state);
  void drawRecording(const UiState& state, uint32_t nowMs);
  void drawRecognizing(uint32_t nowMs);
  void drawResult(const UiState& state);
  void drawError(const UiState& state);
  void drawStatusPill(int x, int y, const char* label, uint16_t color);
  void drawWifiIcon(int cx, int cy, uint16_t color);
  void drawPageText(const std::string& text, int x, int y, int lineHeight);

  AppMode lastMode_ = AppMode::Boot;
  uint32_t lastRenderMs_ = 0;
  std::string lastSignature_;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
