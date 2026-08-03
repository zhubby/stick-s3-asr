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
  bool batteryCharging = false;
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
  int panelH(AppMode mode) const;
  int footerY() const;
  int textLineLimit() const;
  LovyanGFX& gfx();
  void recreateCanvas();
  void flush();

  void drawFrame(const UiState& state, uint32_t nowMs);
  void drawHeader(const UiState& state, uint32_t nowMs);
  void drawFooter(const UiState& state);
  void drawIdle(const UiState& state);
  void drawPairing(const UiState& state);
  void drawConnecting(const UiState& state);
  void drawRecording(const UiState& state, uint32_t nowMs);
  void drawRecognizing(uint32_t nowMs);
  void drawResult(const UiState& state);
  void drawError(const UiState& state);
  enum class StatusGlyph { Check, Alert, Record, Asr, Text };
  void drawStatusPill(int x, int y, const char* label, uint16_t color);
  void drawStatusIcon(int centerX, int centerY, StatusGlyph glyph, uint16_t color);
  void drawHoldRecordPrompt(int centerX, int centerY, uint16_t color);
  void drawBatteryIcon(int x, int y, int batteryLevel, bool charging, uint32_t nowMs);
  void drawWifiIcon(int x, int y, uint16_t color);
  void drawPageText(const std::string& text, int x, int y, int lineHeight);

  M5Canvas canvas_{&M5.Display};
  bool canvasReady_ = false;
  AppMode lastMode_ = AppMode::Boot;
  uint32_t lastRenderMs_ = 0;
  std::string lastSignature_;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
