#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>

#include <memory>
#include <string>
#include <vector>

#include "AppConfig.h"
#include "AppTypes.h"
#include "asr/VolcAsrClient.h"
#include "audio/MicRecorder.h"
#include "input/InputController.h"
#include "ui/DisplayUi.h"
#include "ui/OrientationController.h"
#include "ui/PageModel.h"
#include "wifi/ProvisioningPortal.h"
#include "wifi/WifiCredentialStore.h"

using namespace stick_s3_asr;

namespace {

constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kOrientationSampleMs = 100;
constexpr size_t kPortraitColumns = 16;
constexpr size_t kPortraitLines = 6;
constexpr size_t kLandscapeColumns = 34;
constexpr size_t kLandscapeLines = 3;

RuntimeConfig runtimeConfig;
bool asrReady = false;
bool wifiConfigured = false;
AppMode mode = AppMode::Boot;
std::string errorText;
std::string provisioningMessage;

DisplayUi displayUi;
InputController inputController(450);
OrientationController orientationController(1);
PageModel pageModel(kLandscapeColumns, kLandscapeLines);
MicRecorder recorder;
VolcAsrClient asrClient;
WifiCredentialStore wifiCredentialStore;
ProvisioningPortal provisioningPortal;

std::vector<uint8_t> txBuffer;
uint32_t wifiAttemptStartedMs = 0;
uint32_t lastWifiRetryMs = 0;
uint32_t lastOrientationSampleMs = 0;
bool asrFinishRequested = false;
bool waitingForAsrReady = false;

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

std::string makeRequestId() {
  char requestId[32];
  snprintf(requestId, sizeof(requestId), "%08lx%08lx",
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFFFFULL),
           static_cast<unsigned long>(millis()));
  return requestId;
}

VolcAsrConfig makeAsrConfig() {
  VolcAsrConfig config;
  config.endpoint = runtimeConfig.volcEndpoint;
  config.appKey = runtimeConfig.volcAppKey;
  config.accessKey = runtimeConfig.volcAccessKey;
  config.resourceId = runtimeConfig.volcResourceId;
  config.audio = AudioFormat{};
  config.enablePunctuation = true;
  config.enableItn = true;
  return config;
}

std::string makeProvisioningSsid() {
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "StickS3-ASR-%04lX",
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFULL));
  return ssid;
}

void applyWifiCredentials(const WifiCredentials& credentials) {
  runtimeConfig.wifiSsid = credentials.ssid;
  runtimeConfig.wifiPassword = credentials.password;
  wifiConfigured = hasWifiCredentials(runtimeConfig);
}

void applyPageLayout() {
  if (orientationController.landscape()) {
    pageModel.setLayout(kLandscapeColumns, kLandscapeLines);
  } else {
    pageModel.setLayout(kPortraitColumns, kPortraitLines);
  }
}

void updateDisplayOrientation(uint32_t nowMs) {
  if (!M5.Imu.isEnabled()) return;
  if (lastOrientationSampleMs != 0 && nowMs - lastOrientationSampleMs < kOrientationSampleMs) {
    return;
  }
  lastOrientationSampleMs = nowMs;

  M5.Imu.update();
  float accelX = 0.0f;
  float accelY = 0.0f;
  float accelZ = 0.0f;
  if (!M5.Imu.getAccel(&accelX, &accelY, &accelZ)) return;

  if (orientationController.update(true, accelX, accelY, nowMs)) {
    displayUi.setRotation(orientationController.rotation());
    applyPageLayout();
  }
}

void setError(const std::string& message) {
  errorText = message;
  mode = AppMode::Error;
  waitingForAsrReady = false;
  recorder.cancel();
  asrClient.cancel();
  inputController.resetRecordingGesture();
}

void stopProvisioning() {
  if (provisioningPortal.active()) {
    provisioningPortal.stop();
  }
  provisioningMessage.clear();
}

void startProvisioning(const std::string& message) {
  if (provisioningPortal.active()) {
    mode = AppMode::Pairing;
    return;
  }

  recorder.cancel();
  asrClient.cancel();
  inputController.resetRecordingGesture();
  WiFi.disconnect(false, false);

  provisioningMessage = message;
  if (!provisioningPortal.begin(makeProvisioningSsid(), runtimeConfig.provisionApPassword)) {
    setError("AP failed");
    return;
  }
  mode = AppMode::Pairing;
}

void startWifi(uint32_t nowMs) {
  if (!wifiConfigured || wifiConnected()) return;
  stopProvisioning();
  WiFi.mode(WIFI_STA);
  WiFi.begin(runtimeConfig.wifiSsid.c_str(), runtimeConfig.wifiPassword.c_str());
  wifiAttemptStartedMs = nowMs;
  lastWifiRetryMs = nowMs;
  if (mode == AppMode::Boot || mode == AppMode::Idle || mode == AppMode::Pairing) {
    mode = AppMode::Connecting;
  }
}

void maintainWifi(uint32_t nowMs) {
  if (provisioningPortal.active()) {
    provisioningPortal.loop();
    if (provisioningPortal.hasPendingCredentials()) {
      const WifiCredentials credentials = provisioningPortal.takePendingCredentials();
      if (!wifiCredentialStore.save(credentials)) {
        setError("Wi-Fi save failed");
        return;
      }
      applyWifiCredentials(credentials);
      stopProvisioning();
      WiFi.mode(WIFI_STA);
      startWifi(nowMs);
    }
    return;
  }

  if (!wifiConfigured) {
    startProvisioning("AP setup");
    return;
  }

  if (wifiConnected()) {
    if (mode == AppMode::Boot || (mode == AppMode::Connecting && !waitingForAsrReady)) {
      mode = AppMode::Idle;
    }
    return;
  }

  if (mode == AppMode::Recording || mode == AppMode::Recognizing) {
    setError("Wi-Fi lost");
    return;
  }

  if (wifiAttemptStartedMs == 0 || nowMs - lastWifiRetryMs > kWifiRetryMs) {
    startWifi(nowMs);
    return;
  }

  if (mode == AppMode::Connecting &&
      nowMs - wifiAttemptStartedMs > kWifiConnectTimeoutMs) {
    startProvisioning("Wi-Fi failed");
  }
}

void beginRecording(uint32_t nowMs) {
  if (!asrReady) {
    setError("ASR key missing");
    return;
  }
  if (!wifiConfigured) {
    startProvisioning("AP setup");
    return;
  }
  if (!wifiConnected()) {
    setError("Wi-Fi offline");
    startWifi(nowMs);
    return;
  }

  pageModel.clear();
  errorText.clear();
  asrFinishRequested = false;
  waitingForAsrReady = false;
  asrClient.cancel();
  if (!asrClient.begin(makeAsrConfig(), makeRequestId())) {
    setError(asrClient.result().error.empty() ? "ASR start failed" : asrClient.result().error);
    return;
  }
  waitingForAsrReady = true;
  mode = AppMode::Connecting;
}

void stopRecording(uint32_t nowMs) {
  (void)nowMs;
  if (waitingForAsrReady) {
    setError("Hold until REC");
    return;
  }
  if (mode == AppMode::Recording) {
    recorder.requestStop();
    mode = AppMode::Recognizing;
  }
}

void drainAudioToAsr() {
  if (!asrClient.readyForAudio() || txBuffer.empty()) return;

  while (recorder.queuedBytes() > 0 && asrClient.readyForAudio()) {
    const size_t target =
        recorder.queuedBytes() >= recorder.chunkBytes() ? recorder.chunkBytes()
                                                        : recorder.queuedBytes();
    if (target == 0 || target > txBuffer.size()) break;
    const size_t read = recorder.readPcm(txBuffer.data(), target);
    if (read == 0) break;
    if (!asrClient.sendAudio(txBuffer.data(), read)) {
      setError("Audio send failed");
      return;
    }
  }
}

void finalizeAsrIfReady() {
  if (asrFinishRequested || !recorder.finished()) return;
  if (!asrClient.readyForAudio()) return;
  asrFinishRequested = true;
  if (!asrClient.finish()) {
    setError(asrClient.result().error.empty() ? "ASR finish failed" : asrClient.result().error);
  }
}

void updateSpeechFlow(uint32_t nowMs) {
  if (mode != AppMode::Recording && mode != AppMode::Recognizing &&
      !(mode == AppMode::Connecting && waitingForAsrReady)) {
    return;
  }

  asrClient.loop(nowMs);

  if (asrClient.failed()) {
    setError(asrClient.result().error.empty() ? "ASR failed" : asrClient.result().error);
    return;
  }

  if (waitingForAsrReady) {
    if (!asrClient.readyForAudio()) return;
    waitingForAsrReady = false;
    if (!recorder.start(nowMs)) {
      setError("Mic failed");
      return;
    }
    mode = AppMode::Recording;
    return;
  }

  recorder.update(nowMs);
  if (recorder.overflowed()) {
    setError("Network slow");
    return;
  }

  if (recorder.timedOut() && mode == AppMode::Recording) {
    mode = AppMode::Recognizing;
  }

  drainAudioToAsr();
  finalizeAsrIfReady();

  if (asrClient.done()) {
    if (asrClient.failed()) {
      setError(asrClient.result().error);
      return;
    }
    const std::string text =
        asrClient.result().text.empty() ? "No speech" : asrClient.result().text;
    pageModel.setText(text);
    mode = AppMode::Result;
    asrClient.cancel();
    inputController.resetRecordingGesture();
  }
}

UiState buildUiState(uint32_t nowMs) {
  UiState state;
  state.mode = mode;
  state.wifiConnected = wifiConnected();
  state.wifiConfigured = wifiConfigured;
  state.asrReady = asrReady;
  state.pairingActive = provisioningPortal.active();
  state.batteryCharging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  state.batteryLevel = M5.Power.getBatteryLevel();
  state.peak = recorder.lastPeak();
  state.recordingMs = recorder.recordedMs(nowMs);
  state.pageIndex = pageModel.pageIndex();
  state.pageCount = pageModel.pageCount();
  state.pageText = pageModel.page();
  state.errorText = errorText.empty() ? provisioningMessage : errorText;
  state.pairingSsid = provisioningPortal.ssid();
  state.pairingPassword = provisioningPortal.password();
  state.pairingUrl = provisioningPortal.url();
  return state;
}

void handleInput(uint32_t nowMs) {
  const InputEvent event = inputController.update(M5.BtnA.isPressed(),
                                                  M5.BtnB.wasPressed(),
                                                  mode,
                                                  nowMs);
  switch (event) {
    case InputEvent::StartRecording:
      beginRecording(nowMs);
      break;
    case InputEvent::StopRecording:
      stopRecording(nowMs);
      break;
    case InputEvent::NextPage:
      pageModel.nextPage();
      break;
    case InputEvent::None:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  auto m5config = M5.config();
  m5config.internal_mic = true;
  m5config.internal_imu = true;
  m5config.internal_spk = false;
  m5config.clear_display = true;
  m5config.fallback_board = m5::board_t::board_M5StickS3;
  M5.begin(m5config);

  displayUi.begin(orientationController.rotation());
  applyPageLayout();
  runtimeConfig = loadRuntimeConfig();
  asrReady = hasAsrSecrets(runtimeConfig);
  wifiCredentialStore.begin();
  applyWifiCredentials(wifiCredentialStore.load(runtimeConfig));

  const AudioFormat audioFormat;
  if (!recorder.begin(audioFormat, 100, 20, 20)) {
    errorText = "Mic init failed";
    mode = AppMode::Error;
  } else {
    txBuffer.assign(recorder.chunkBytes(), 0);
    mode = wifiConfigured ? AppMode::Connecting : AppMode::Idle;
  }

  if (wifiConfigured) {
    startWifi(millis());
  } else {
    startProvisioning("AP setup");
  }
}

void loop() {
  const uint32_t nowMs = millis();
  M5.update();

  updateDisplayOrientation(nowMs);
  maintainWifi(nowMs);
  handleInput(nowMs);
  updateSpeechFlow(nowMs);
  displayUi.render(buildUiState(nowMs), nowMs);

  delay(2);
}
