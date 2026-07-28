#ifndef UNIT_TEST

#include "ui/DisplayUi.h"

namespace stick_s3_asr {

namespace {
constexpr uint16_t kBg = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kPanel2 = 0x2145;
constexpr uint16_t kLine = 0x3A69;
constexpr uint16_t kText = 0xEF7D;
constexpr uint16_t kMuted = 0x8C71;
constexpr uint16_t kCyan = 0x27FF;
constexpr uint16_t kGreen = 0x6FE8;
constexpr uint16_t kAmber = 0xFDC0;
constexpr uint16_t kRed = 0xF9E7;
}

void DisplayUi::begin(uint8_t initialRotation) {
  M5.Display.setRotation(initialRotation & 3U);
  M5.Display.setBrightness(160);
  M5.Display.fillScreen(kBg);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(kText, kBg);
  M5.Display.setFont(&fonts::efontCN_12);
  M5.Display.drawString("StickS3 ASR", 18, M5.Display.height() / 2 - 16);
  M5.Display.drawString("Booting...", 28, M5.Display.height() / 2 + 6);
}

void DisplayUi::setRotation(uint8_t rotation) {
  rotation &= 3U;
  if (M5.Display.getRotation() == rotation) return;
  M5.Display.setRotation(rotation);
  M5.Display.fillScreen(kBg);
  lastSignature_.clear();
  lastRenderMs_ = 0;
}

void DisplayUi::render(const UiState& state, uint32_t nowMs) {
  const bool animated =
      state.mode == AppMode::Recording || state.mode == AppMode::Recognizing;
  const std::string signature = makeSignature(state);
  if (!animated && signature == lastSignature_) {
    return;
  }

  const uint32_t interval =
      state.mode == AppMode::Recording || state.mode == AppMode::Recognizing ? 80 : 180;
  if (state.mode == lastMode_ && nowMs - lastRenderMs_ < interval) {
    return;
  }
  lastMode_ = state.mode;
  lastRenderMs_ = nowMs;
  lastSignature_ = signature;

  M5.Display.startWrite();
  drawFrame(state);
  switch (state.mode) {
    case AppMode::Boot:
    case AppMode::Idle:
      drawIdle(state);
      break;
    case AppMode::Pairing:
      drawPairing(state);
      break;
    case AppMode::Connecting:
      drawConnecting(state);
      break;
    case AppMode::Recording:
      drawRecording(state, nowMs);
      break;
    case AppMode::Recognizing:
      drawRecognizing(nowMs);
      break;
    case AppMode::Result:
      drawResult(state);
      break;
    case AppMode::Error:
      drawError(state);
      break;
  }
  drawFooter(state);
  M5.Display.endWrite();
}

bool DisplayUi::landscape() const {
  return M5.Display.width() > M5.Display.height();
}

int DisplayUi::panelY() const {
  return landscape() ? 32 : 37;
}

int DisplayUi::panelH() const {
  return landscape() ? 78 : 164;
}

int DisplayUi::footerY() const {
  return M5.Display.height() - (landscape() ? 22 : 32);
}

int DisplayUi::textLineLimit() const {
  return landscape() ? 3 : 6;
}

std::string DisplayUi::makeSignature(const UiState& state) const {
  std::string sig;
  sig.reserve(64 + state.pageText.size() + state.errorText.size());
  sig += std::to_string(static_cast<int>(state.mode));
  sig += '|';
  sig += state.wifiConnected ? '1' : '0';
  sig += '|';
  sig += state.wifiConfigured ? '1' : '0';
  sig += '|';
  sig += state.asrReady ? '1' : '0';
  sig += '|';
  sig += state.pairingActive ? '1' : '0';
  sig += '|';
  sig += std::to_string(state.batteryLevel / 5);
  sig += '|';
  sig += std::to_string(state.pageIndex);
  sig += '/';
  sig += std::to_string(state.pageCount);
  sig += '|';
  sig += state.pageText;
  sig += '|';
  sig += state.errorText;
  sig += '|';
  sig += state.pairingSsid;
  sig += '|';
  sig += state.pairingPassword;
  sig += '|';
  sig += state.pairingUrl;
  return sig;
}

void DisplayUi::drawFrame(const UiState& state) {
  auto& g = M5.Display;
  const int w = g.width();
  const int h = g.height();
  const int headH = landscape() ? 23 : 25;
  const int pY = panelY();
  const int pH = panelH();
  const int fY = footerY();
  (void)h;
  g.fillScreen(kBg);
  g.fillRoundRect(6, 5, w - 12, headH, 5, kPanel);
  g.drawRoundRect(6, 5, w - 12, headH, 5, kLine);
  drawHeader(state);
  g.fillRoundRect(7, pY, w - 14, pH, 6, kPanel);
  g.drawRoundRect(7, pY, w - 14, pH, 6, kLine);
  g.fillRoundRect(7, fY, w - 14, landscape() ? 17 : 25, 5, kPanel2);
}

void DisplayUi::drawHeader(const UiState& state) {
  auto& g = M5.Display;
  const int w = g.width();
  g.setFont(&fonts::Font2);
  g.setTextSize(1);
  g.setTextColor(kText, kPanel);
  g.drawString("STICK ASR", 12, 10);

  const int battery = state.batteryLevel;
  const uint16_t batteryColor = battery < 0 ? kMuted : (battery < 20 ? kAmber : kGreen);
  const int batteryX = w - 31;
  const int batteryY = 13;
  const int wifiX = batteryX - 16;
  drawWifiIcon(wifiX, 17,
               state.wifiConnected ? kGreen : (state.pairingActive ? kAmber : kMuted));

  g.drawRoundRect(batteryX, batteryY, 18, 8, 2, batteryColor);
  g.fillRect(batteryX + 18, batteryY + 2, 2, 4, batteryColor);
  if (battery >= 0) {
    const int fill = battery > 95 ? 14 : battery * 14 / 100;
    g.fillRect(batteryX + 2, batteryY + 2, fill, 4, batteryColor);
  }
}

void DisplayUi::drawFooter(const UiState& state) {
  auto& g = M5.Display;
  const int fY = footerY();
  g.setFont(&fonts::efontCN_10);
  g.setTextColor(kMuted, kPanel2);
  if (state.mode == AppMode::Result) {
    g.drawString("A REC", 14, fY + (landscape() ? 3 : 7));
    g.drawString("B PAGE", landscape() ? 158 : 79, fY + (landscape() ? 3 : 7));
  } else if (state.mode == AppMode::Pairing) {
    g.drawString("AP SETUP", 14, fY + (landscape() ? 3 : 7));
  } else {
    g.drawString("A HOLD", 14, fY + (landscape() ? 3 : 7));
  }
}

void DisplayUi::drawIdle(const UiState& state) {
  auto& g = M5.Display;
  const bool ready = state.asrReady && state.wifiConfigured;
  if (landscape()) {
    drawStatusPill(13, 43, ready ? "READY" : "SETUP", ready ? kCyan : kAmber);
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(kText, kPanel);
    g.drawString("ASR", 75, 43);
    g.setFont(&fonts::efontCN_12);
    g.setTextColor(kMuted, kPanel);
    const char* line =
        !state.asrReady ? "NO KEY"
                        : (!state.wifiConfigured ? "AP SETUP" : "HOLD A");
    g.drawString(line, 18, 74);
    g.drawRoundRect(155, 42, 66, 20, 10, ready ? kCyan : kAmber);
    g.setTextColor(ready ? kCyan : kAmber, kPanel);
    g.drawString(ready ? "CLOUD" : "SETUP", 168, 46);
    return;
  }

  drawStatusPill(17, 50, ready ? "READY" : "SETUP", ready ? kCyan : kAmber);
  g.setFont(&fonts::efontCN_16);
  g.setTextColor(kText, kPanel);
  g.drawString("ASR", 50, 78);
  g.setFont(&fonts::efontCN_12);
  g.setTextColor(kMuted, kPanel);
  if (!state.asrReady) {
    g.drawString("NO KEY", 43, 111);
  } else if (!state.wifiConfigured) {
    g.drawString("AP SETUP", 34, 111);
  } else {
    g.drawString("HOLD A", 40, 111);
    g.drawString("RELEASE", 37, 132);
  }

  g.drawRoundRect(31, 165, 73, 20, 10, ready ? kCyan : kAmber);
  g.setTextColor(ready ? kCyan : kAmber, kPanel);
  g.drawString(ready ? "CLOUD" : "SETUP", 48, 169);
}

void DisplayUi::drawPairing(const UiState& state) {
  auto& g = M5.Display;
  if (landscape()) {
    drawStatusPill(13, 40, "PAIR", kAmber);
    g.setFont(&fonts::efontCN_12);
    g.setTextColor(kText, kPanel);
    g.drawString("OPEN PORTAL", 72, 42);
    g.setFont(&fonts::Font2);
    g.setTextColor(kCyan, kPanel);
    g.drawString(state.pairingSsid.empty() ? "StickS3-ASR" : state.pairingSsid.c_str(),
                 18, 66);
    g.setFont(&fonts::efontCN_10);
    g.setTextColor(kMuted, kPanel);
    g.drawString("PASS", 18, 88);
    g.setTextColor(kText, kPanel);
    g.drawString(state.pairingPassword.empty() ? "NONE" : state.pairingPassword.c_str(),
                 47, 88);
    g.setTextColor(kMuted, kPanel);
    g.drawString("URL", 128, 88);
    g.setTextColor(kCyan, kPanel);
    g.drawString("192.168.4.1", 157, 88);
    return;
  }

  drawStatusPill(17, 50, "PAIR", kAmber);
  g.setFont(&fonts::efontCN_14);
  g.setTextColor(kText, kPanel);
  g.drawString("OPEN AP", 36, 72);

  g.setFont(&fonts::Font2);
  g.setTextColor(kCyan, kPanel);
  g.drawString(state.pairingSsid.empty() ? "StickS3-ASR" : state.pairingSsid.c_str(),
               17, 103);

  g.setFont(&fonts::efontCN_10);
  g.setTextColor(kMuted, kPanel);
  g.drawString("PASS", 17, 128);
  g.setTextColor(kText, kPanel);
  g.drawString(state.pairingPassword.empty() ? "NONE" : state.pairingPassword.c_str(),
               46, 128);

  g.setTextColor(kMuted, kPanel);
  g.drawString("URL", 17, 153);
  g.setTextColor(kCyan, kPanel);
  g.drawString(state.pairingUrl.empty() ? "192.168.4.1" : state.pairingUrl.c_str(),
               17, 174);
  g.setTextColor(kMuted, kPanel);
  g.drawString("SAVE TO JOIN", 28, 191);
}

void DisplayUi::drawConnecting(const UiState& state) {
  auto& g = M5.Display;
  if (landscape()) {
    drawStatusPill(13, 43, "NET", kAmber);
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(kText, kPanel);
    g.drawString(state.wifiConnected ? "ASR" : "Wi-Fi", 76, 43);
    g.setTextColor(kMuted, kPanel);
    g.setFont(&fonts::efontCN_12);
    g.drawString(state.wifiConnected ? "HOLD" : "AP IF FAIL", 18, 75);
    return;
  }

  drawStatusPill(17, 50, "NET", kAmber);
  g.setFont(&fonts::efontCN_14);
  g.setTextColor(kText, kPanel);
  g.drawString(state.wifiConnected ? "ASR" : "Wi-Fi", 45, 87);
  g.setTextColor(kMuted, kPanel);
  g.setFont(&fonts::efontCN_12);
  g.drawString(state.wifiConnected ? "HOLD" : "AP IF FAIL", 35, 122);
}

void DisplayUi::drawRecording(const UiState& state, uint32_t nowMs) {
  auto& g = M5.Display;
  if (landscape()) {
    drawStatusPill(13, 40, "REC", kRed);
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(kText, kPanel);
    g.drawString("LISTEN", 75, 41);

    const int centerY = 82;
    const int peak = static_cast<int>(state.peak) * 26 / 32768;
    for (int i = 0; i < 16; ++i) {
      const int x = 18 + i * 8;
      const int phase = static_cast<int>((nowMs / 80 + i * 3) % 14);
      const int h = 7 + ((phase < 7 ? phase : 14 - phase) * 2) + peak / 3;
      g.drawFastVLine(x, centerY - h / 2, h, kCyan);
      g.drawFastVLine(x + 1, centerY - h / 2, h, kCyan);
    }

    g.setFont(&fonts::Font4);
    g.setTextColor(kCyan, kPanel);
    char seconds[8];
    snprintf(seconds, sizeof(seconds), "%02lu",
             static_cast<unsigned long>(state.recordingMs / 1000));
    g.drawString(seconds, 178, 61);
    g.setFont(&fonts::efontCN_10);
    g.setTextColor(kMuted, kPanel);
    g.drawString("RELEASE", 174, 92);
    return;
  }

  drawStatusPill(17, 50, "REC", kRed);
  g.setFont(&fonts::efontCN_14);
  g.setTextColor(kText, kPanel);
  g.drawString("LISTEN", 38, 72);

  const int centerY = 128;
  const int peak = static_cast<int>(state.peak) * 36 / 32768;
  for (int i = 0; i < 9; ++i) {
    const int x = 22 + i * 10;
    const int phase = static_cast<int>((nowMs / 80 + i * 3) % 14);
    const int h = 8 + ((phase < 7 ? phase : 14 - phase) * 3) + peak / 3;
    g.drawFastVLine(x, centerY - h / 2, h, kCyan);
    g.drawFastVLine(x + 1, centerY - h / 2, h, kCyan);
  }

  g.setFont(&fonts::Font4);
  g.setTextColor(kCyan, kPanel);
  char seconds[8];
  snprintf(seconds, sizeof(seconds), "%02lu", static_cast<unsigned long>(state.recordingMs / 1000));
  g.drawString(seconds, 50, 160);
  g.setFont(&fonts::efontCN_10);
  g.setTextColor(kMuted, kPanel);
  g.drawString("RELEASE", 43, 187);
}

void DisplayUi::drawRecognizing(uint32_t nowMs) {
  auto& g = M5.Display;
  if (landscape()) {
    drawStatusPill(13, 43, "ASR", kAmber);
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(kText, kPanel);
    g.drawString("ASR", 76, 43);
    for (int i = 0; i < 4; ++i) {
      const bool active = ((nowMs / 180) % 4) == static_cast<uint32_t>(i);
      g.fillCircle(82 + i * 18, 82, active ? 5 : 3, active ? kCyan : kLine);
    }
    g.setFont(&fonts::efontCN_10);
    g.setTextColor(kMuted, kPanel);
    g.drawString("WAIT", 178, 78);
    return;
  }

  drawStatusPill(17, 50, "ASR", kAmber);
  g.setFont(&fonts::efontCN_14);
  g.setTextColor(kText, kPanel);
  g.drawString("ASR", 52, 83);
  for (int i = 0; i < 4; ++i) {
    const bool active = ((nowMs / 180) % 4) == static_cast<uint32_t>(i);
    g.fillCircle(47 + i * 14, 131, active ? 5 : 3, active ? kCyan : kLine);
  }
  g.setFont(&fonts::efontCN_10);
  g.setTextColor(kMuted, kPanel);
  g.drawString("WAIT", 51, 170);
}

void DisplayUi::drawResult(const UiState& state) {
  auto& g = M5.Display;
  drawStatusPill(landscape() ? 13 : 17, landscape() ? 40 : 50, "TEXT", kGreen);
  g.setFont(&fonts::efontCN_10);
  g.setTextColor(kMuted, kPanel);
  char pageLabel[20];
  snprintf(pageLabel, sizeof(pageLabel), "%u/%u",
           static_cast<unsigned>(state.pageIndex + 1),
           static_cast<unsigned>(state.pageCount));
  g.drawString(pageLabel, landscape() ? 203 : 99, landscape() ? 42 : 52);
  drawPageText(state.pageText, landscape() ? 15 : 15, landscape() ? 62 : 73,
               landscape() ? 16 : 20);
}

void DisplayUi::drawError(const UiState& state) {
  auto& g = M5.Display;
  if (landscape()) {
    drawStatusPill(13, 40, "ERR", kAmber);
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(kAmber, kPanel);
    g.drawString("ERROR", 76, 42);
    g.setFont(&fonts::efontCN_10);
    g.setTextColor(kText, kPanel);
    drawPageText(state.errorText.empty() ? "Error" : state.errorText, 18, 68, 15);
    return;
  }

  drawStatusPill(17, 50, "ERR", kAmber);
  g.setFont(&fonts::efontCN_14);
  g.setTextColor(kAmber, kPanel);
  g.drawString("ERROR", 39, 75);
  g.setFont(&fonts::efontCN_10);
  g.setTextColor(kText, kPanel);
  drawPageText(state.errorText.empty() ? "Error" : state.errorText, 17, 111, 17);
}

void DisplayUi::drawStatusPill(int x, int y, const char* label, uint16_t color) {
  auto& g = M5.Display;
  g.fillRoundRect(x, y, 47, 17, 8, kPanel2);
  g.drawRoundRect(x, y, 47, 17, 8, color);
  g.setFont(&fonts::Font2);
  g.setTextColor(color, kPanel2);
  g.drawString(label, x + 9, y + 2);
}

void DisplayUi::drawWifiIcon(int cx, int cy, uint16_t color) {
  auto& g = M5.Display;
  g.fillRoundRect(cx - 12, cy - 8, 22, 17, 5, kPanel2);
  g.drawRoundRect(cx - 12, cy - 8, 22, 17, 5, kLine);

  g.drawLine(cx - 7, cy - 2, cx - 5, cy - 4, color);
  g.drawFastHLine(cx - 4, cy - 5, 9, color);
  g.drawLine(cx + 5, cy - 4, cx + 7, cy - 2, color);

  g.drawLine(cx - 5, cy + 1, cx - 3, cy - 1, color);
  g.drawFastHLine(cx - 2, cy - 2, 5, color);
  g.drawLine(cx + 3, cy - 1, cx + 5, cy + 1, color);

  g.drawLine(cx - 3, cy + 4, cx - 1, cy + 2, color);
  g.drawLine(cx + 1, cy + 2, cx + 3, cy + 4, color);
  g.fillCircle(cx, cy + 5, 2, color);
}

void DisplayUi::drawPageText(const std::string& text, int x, int y, int lineHeight) {
  auto& g = M5.Display;
  g.setFont(&fonts::efontCN_12);
  g.setTextColor(kText, kPanel);
  size_t start = 0;
  int line = 0;
  const int maxLines = textLineLimit();
  while (start <= text.size() && line < maxLines) {
    const size_t end = text.find('\n', start);
    const std::string current =
        end == std::string::npos ? text.substr(start) : text.substr(start, end - start);
    g.drawString(current.c_str(), x, y + line * lineHeight);
    if (end == std::string::npos) break;
    start = end + 1;
    ++line;
  }
}

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
