#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>

#include <memory>
#include <string>
#include <vector>

#include "AppConfig.h"
#include "AppRuntime.h"
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
constexpr uint32_t kWifiReconnectProvisioningMs = 60000;
constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kOrientationSampleMs = 100;
constexpr uint32_t kStatusLogMs = 5000;
constexpr size_t kMaxAudioChunksPerLoop = 2;
constexpr uint16_t kRecordingChunkMs = 100;
constexpr uint16_t kMaxRecordingSeconds = 60;
constexpr size_t kRecordingQueuedChunks =
    (static_cast<size_t>(kMaxRecordingSeconds) * 1000U) / kRecordingChunkMs + 10U;
constexpr const char* kNoSpeechText = "No speech";
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
std::string deferredRecordingErrorText;
bool recoverableNetworkError = false;
bool wifiEverConnected = false;

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
uint32_t lastStatusLogMs = 0;
bool asrFinishRequested = false;
bool waitingForAsrReady = false;
bool asrSessionStarted = false;
uint32_t sessionAudioBytesSent = 0;
uint32_t sessionAsrResponseCount = 0;
unsigned sessionAsrState = 0;
uint16_t sessionPeakMax = 0;
bool sessionAsrConnected = false;
AppMode lastLoggedMode = AppMode::Boot;
wl_status_t lastLoggedWifiStatus = WL_IDLE_STATUS;
bool lastLoggedRecoverableError = false;
bool lastLoggedBtnA = false;
bool lastLoggedBtnB = false;
bool lastLoggedBtnPwr = false;

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

const char* modeName(AppMode value) {
  switch (value) {
    case AppMode::Boot:
      return "Boot";
    case AppMode::Idle:
      return "Idle";
    case AppMode::Pairing:
      return "Pairing";
    case AppMode::Connecting:
      return "Connecting";
    case AppMode::Recording:
      return "Recording";
    case AppMode::Recognizing:
      return "Recognizing";
    case AppMode::Result:
      return "Result";
    case AppMode::Error:
      return "Error";
  }
  return "?";
}

const char* wifiStatusName(wl_status_t value) {
  switch (value) {
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID";
    case WL_SCAN_COMPLETED:
      return "SCAN_DONE";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "LOST";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
  }
  return "?";
}

uint32_t timestampOrOne(uint32_t nowMs) {
  return nowMs == 0 ? 1 : nowMs;
}

void clearRecoverableNetworkError() {
  if (!recoverableNetworkError) return;
  errorText.clear();
  recoverableNetworkError = false;
}

void markWifiConnected() {
  const bool shouldLog = !wifiEverConnected || wifiAttemptStartedMs != 0 ||
                         recoverableNetworkError;
  wifiEverConnected = true;
  wifiAttemptStartedMs = 0;
  lastWifiRetryMs = 0;
  clearRecoverableNetworkError();
  if (shouldLog) {
    Serial.printf("[net] connected rssi=%ld\n", static_cast<long>(WiFi.RSSI()));
  }
}

ErrorRecovery asrErrorRecovery() {
  return asrClient.recoverableNetworkError() ? ErrorRecovery::RecoverableNetwork
                                             : ErrorRecovery::Fatal;
}

bool lowLatencyAudioActive() {
  return (mode == AppMode::Recording &&
          (recorder.active() || waitingForAsrReady)) ||
         (mode == AppMode::Connecting && waitingForAsrReady) ||
         (mode == AppMode::Recognizing && !asrFinishRequested);
}

void logRuntimeState(uint32_t nowMs, bool force = false) {
  const wl_status_t wifiStatus = WiFi.status();
  const bool changed = mode != lastLoggedMode ||
                       wifiStatus != lastLoggedWifiStatus ||
                       recoverableNetworkError != lastLoggedRecoverableError;
  if (!force && !changed &&
      lastStatusLogMs != 0 && nowMs - lastStatusLogMs < kStatusLogMs) {
    return;
  }

  lastStatusLogMs = nowMs;
  lastLoggedMode = mode;
  lastLoggedWifiStatus = wifiStatus;
  lastLoggedRecoverableError = recoverableNetworkError;
  Serial.printf("[state] t=%lu mode=%s wifi=%s portal=%d recoverable=%d waitAsr=%d final=%d queued=%u rssi=%ld heap=%lu temp=%.1f\n",
                static_cast<unsigned long>(nowMs),
                modeName(mode),
                wifiStatusName(wifiStatus),
                provisioningPortal.active() ? 1 : 0,
                recoverableNetworkError ? 1 : 0,
                waitingForAsrReady ? 1 : 0,
                asrFinishRequested ? 1 : 0,
                static_cast<unsigned>(recorder.queuedBytes()),
                wifiStatus == WL_CONNECTED ? static_cast<long>(WiFi.RSSI()) : 0L,
                static_cast<unsigned long>(ESP.getFreeHeap()),
                temperatureRead());
}

void logButtonState(uint32_t nowMs) {
  const bool btnA = M5.BtnA.isPressed();
  const bool btnB = M5.BtnB.isPressed();
  const bool btnPwr = M5.BtnPWR.isPressed();
  if (btnA == lastLoggedBtnA && btnB == lastLoggedBtnB &&
      btnPwr == lastLoggedBtnPwr) {
    return;
  }

  lastLoggedBtnA = btnA;
  lastLoggedBtnB = btnB;
  lastLoggedBtnPwr = btnPwr;
  Serial.printf("[button] t=%lu A=%d B=%d PWR=%d\n",
                static_cast<unsigned long>(nowMs),
                btnA ? 1 : 0,
                btnB ? 1 : 0,
                btnPwr ? 1 : 0);
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
  config.apiKey = runtimeConfig.volcApiKey;
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

void setError(const std::string& message,
              ErrorRecovery recovery = ErrorRecovery::Fatal) {
  errorText = message;
  recoverableNetworkError = errorRecoveryIsNetworkRecoverable(recovery);
  Serial.printf("[error] %s recovery=%s\n",
                message.c_str(),
                recoverableNetworkError ? "network" : "fatal");
  mode = AppMode::Error;
  waitingForAsrReady = false;
  recorder.cancel();
  asrClient.cancel();
  inputController.resetRecordingGesture(M5.BtnA.isPressed());
}

bool hasDeferredRecordingError() {
  return !deferredRecordingErrorText.empty();
}

void clearDeferredRecordingError() {
  deferredRecordingErrorText.clear();
}

void showRecognitionResult(const std::string& text,
                           const char* reason = nullptr) {
  const std::string displayText = text.empty() ? kNoSpeechText : text;
  const uint32_t responseCount =
      asrClient.responseCount() > 0 ? asrClient.responseCount() : sessionAsrResponseCount;
  const bool connected = asrClient.connectedOnce() || sessionAsrConnected;
  const unsigned asrState =
      static_cast<unsigned>(asrClient.state()) != 0
          ? static_cast<unsigned>(asrClient.state())
          : sessionAsrState;
  pageModel.setText(displayText);
  errorText.clear();
  clearDeferredRecordingError();
  recoverableNetworkError = false;
  waitingForAsrReady = false;
  asrFinishRequested = false;
  asrSessionStarted = false;
  recorder.cancel();
  asrClient.cancel();
  mode = AppMode::Result;
  inputController.resetRecordingGesture(M5.BtnA.isPressed());
  Serial.printf("[result] reason=%s textLen=%u sent=%lu peak=%u connected=%d responses=%lu asrState=%u\n",
                (reason && reason[0] != '\0') ? reason : "complete",
                static_cast<unsigned>(displayText.size()),
                static_cast<unsigned long>(sessionAudioBytesSent),
                static_cast<unsigned>(sessionPeakMax),
                connected ? 1 : 0,
                static_cast<unsigned long>(responseCount),
                asrState);
}

void deferRecordingError(const std::string& message,
                         ErrorRecovery recovery) {
  if (!hasDeferredRecordingError()) {
    deferredRecordingErrorText = message;
    errorText = message;
    recoverableNetworkError = errorRecoveryIsNetworkRecoverable(recovery);
    Serial.printf("[error] %s recovery=%s deferred=recording\n",
                  message.c_str(),
                  recoverableNetworkError ? "network" : "fatal");
  }
  sessionAsrConnected = sessionAsrConnected || asrClient.connectedOnce();
  if (asrClient.responseCount() > sessionAsrResponseCount) {
    sessionAsrResponseCount = asrClient.responseCount();
  }
  sessionAsrState = static_cast<unsigned>(asrClient.state());
  waitingForAsrReady = false;
  asrFinishRequested = false;
  asrSessionStarted = false;
  recorder.cancel();
  asrClient.cancel();
}

void finishDeferredRecordingError() {
  const std::string message =
      deferredRecordingErrorText.empty() ? "ASR failed" : deferredRecordingErrorText;
  clearDeferredRecordingError();
  showRecognitionResult("", message.c_str());
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
  Serial.printf("[net] start provisioning reason=%s\n", message.c_str());
  if (!provisioningPortal.begin(makeProvisioningSsid(), runtimeConfig.provisionApPassword)) {
    setError("AP failed");
    return;
  }
  mode = AppMode::Pairing;
}

void startWifi(uint32_t nowMs, bool force = false) {
  if (!wifiConfigured || (!force && wifiConnected())) return;

  const bool keepProvisioningActive = provisioningPortal.active();
  const bool recoverableError = mode == AppMode::Error && recoverableNetworkError;
  if (recoverableError) {
    clearRecoverableNetworkError();
  }

  WiFi.mode(keepProvisioningActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(runtimeConfig.wifiSsid.c_str(), runtimeConfig.wifiPassword.c_str());
  Serial.printf("[net] start sta force=%d ap=%d mode=%s\n",
                force ? 1 : 0,
                keepProvisioningActive ? 1 : 0,
                modeName(mode));
  wifiAttemptStartedMs = timestampOrOne(nowMs);
  lastWifiRetryMs = timestampOrOne(nowMs);
  if (!keepProvisioningActive &&
      (mode == AppMode::Boot || mode == AppMode::Idle ||
       mode == AppMode::Pairing || mode == AppMode::Result ||
       recoverableError)) {
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
      startWifi(nowMs, true);
      return;
    }
    if (wifiConfigured) {
      if (wifiConnected()) {
        markWifiConnected();
        stopProvisioning();
        if (!waitingForAsrReady) {
          mode = AppMode::Idle;
        }
        return;
      }
      if (wifiAttemptStartedMs == 0 || nowMs - lastWifiRetryMs > kWifiRetryMs) {
        startWifi(nowMs);
      }
    }
    return;
  }

  if (!wifiConfigured) {
    startProvisioning("AP setup");
    return;
  }

  if (wifiConnected()) {
    const bool wasRecoverableNetworkError = recoverableNetworkError;
    markWifiConnected();
    if (mode == AppMode::Error && wasRecoverableNetworkError && !waitingForAsrReady) {
      mode = AppMode::Idle;
      return;
    }
    if (mode == AppMode::Boot || (mode == AppMode::Connecting && !waitingForAsrReady)) {
      mode = AppMode::Idle;
    }
    return;
  }

  if (mode == AppMode::Recording || mode == AppMode::Recognizing ||
      (mode == AppMode::Connecting && waitingForAsrReady)) {
    if (mode == AppMode::Recording) {
      deferRecordingError("Wi-Fi lost", ErrorRecovery::RecoverableNetwork);
    } else {
      showRecognitionResult("", "Wi-Fi lost");
    }
    return;
  }

  if (wifiAttemptStartedMs == 0 || nowMs - lastWifiRetryMs > kWifiRetryMs) {
    startWifi(nowMs);
    return;
  }

  if (mode == AppMode::Connecting && wifiAttemptStartedMs != 0) {
    const uint32_t fallbackMs =
        wifiEverConnected ? kWifiReconnectProvisioningMs : kWifiConnectTimeoutMs;
    if (nowMs - wifiAttemptStartedMs > fallbackMs) {
      startProvisioning(wifiEverConnected ? "Wi-Fi lost" : "Wi-Fi failed");
      startWifi(nowMs);
    }
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
    setError("Wi-Fi offline", ErrorRecovery::RecoverableNetwork);
    startWifi(nowMs);
    return;
  }

  pageModel.clear();
  errorText.clear();
  clearDeferredRecordingError();
  asrFinishRequested = false;
  waitingForAsrReady = false;
  asrSessionStarted = false;
  sessionAudioBytesSent = 0;
  sessionAsrResponseCount = 0;
  sessionAsrState = 0;
  sessionPeakMax = 0;
  sessionAsrConnected = false;
  asrClient.cancel();
  if (!recorder.start(nowMs)) {
    setError("Mic failed");
    return;
  }
  mode = AppMode::Recording;
}

void stopRecording(uint32_t nowMs) {
  (void)nowMs;
  if (mode == AppMode::Recording) {
    if (hasDeferredRecordingError()) {
      finishDeferredRecordingError();
      return;
    }
    recorder.requestStop();
    mode = AppMode::Recognizing;
  }
}

void returnToRecordReady() {
  recorder.cancel();
  asrClient.cancel();
  pageModel.clear();
  errorText.clear();
  clearDeferredRecordingError();
  recoverableNetworkError = false;
  asrFinishRequested = false;
  waitingForAsrReady = false;
  mode = wifiConnected() ? AppMode::Idle : AppMode::Connecting;
  inputController.resetRecordingGesture(M5.BtnA.isPressed());
}

void drainAudioToAsr() {
  if (!asrClient.readyForAudio() || txBuffer.empty()) return;

  size_t sentChunks = 0;
  while (recorder.queuedBytes() > 0 && asrClient.readyForAudio() &&
         sentChunks < kMaxAudioChunksPerLoop) {
    const size_t target =
        recorder.queuedBytes() >= recorder.chunkBytes() ? recorder.chunkBytes()
                                                        : recorder.queuedBytes();
    if (target == 0 || target > txBuffer.size()) break;
    const size_t read = recorder.readPcm(txBuffer.data(), target);
    if (read == 0) break;
    if (!asrClient.sendAudio(txBuffer.data(), read)) {
      if (mode == AppMode::Recording) {
        deferRecordingError("Audio send failed", ErrorRecovery::RecoverableNetwork);
      } else {
        showRecognitionResult("", "Audio send failed");
      }
      return;
    }
    sessionAudioBytesSent += static_cast<uint32_t>(read);
    ++sentChunks;
  }
}

void startAsrForBufferedAudio() {
  if (asrSessionStarted) return;
  if (!wifiConnected()) {
    showRecognitionResult("", "Wi-Fi offline");
    return;
  }
  asrSessionStarted = true;
  asrFinishRequested = false;
  waitingForAsrReady = false;
  asrClient.cancel();
  if (!asrClient.begin(makeAsrConfig(), makeRequestId())) {
    showRecognitionResult(
        "",
        asrClient.result().error.empty() ? "ASR start failed"
                                         : asrClient.result().error.c_str());
    return;
  }
  waitingForAsrReady = !asrClient.readyForAudio();
}

void finalizeAsrIfReady() {
  if (!asrSessionStarted) return;
  if (asrFinishRequested || !recorder.finished()) return;
  if (!asrClient.readyForAudio()) return;
  asrFinishRequested = true;
  if (!asrClient.finish()) {
    showRecognitionResult(
        "",
        asrClient.result().error.empty() ? "ASR finish failed"
                                         : asrClient.result().error.c_str());
  }
}

void updateSpeechFlow(uint32_t nowMs) {
  if (mode != AppMode::Recording && mode != AppMode::Recognizing &&
      !(mode == AppMode::Connecting && waitingForAsrReady)) {
    return;
  }

  if (mode == AppMode::Recording && hasDeferredRecordingError()) {
    return;
  }

  if (mode == AppMode::Recognizing && !asrSessionStarted) {
    startAsrForBufferedAudio();
    if (mode != AppMode::Recognizing) return;
  }

  if (asrSessionStarted) {
    asrClient.loop(nowMs);
  }

  if (asrSessionStarted && asrClient.failed()) {
    const std::string message =
        asrClient.result().error.empty() ? "ASR failed" : asrClient.result().error;
    const ErrorRecovery recovery = asrErrorRecovery();
    if (mode == AppMode::Recording) {
      deferRecordingError(message, recovery);
    } else {
      showRecognitionResult("", message.c_str());
    }
    return;
  }

  if (asrSessionStarted && waitingForAsrReady) {
    waitingForAsrReady = !asrClient.readyForAudio();
  }

  recorder.update(nowMs);
  if (recorder.lastPeak() > sessionPeakMax) {
    sessionPeakMax = recorder.lastPeak();
  }
  if (recorder.overflowed()) {
    if (mode == AppMode::Recording) {
      deferRecordingError("Network slow", ErrorRecovery::RecoverableNetwork);
    } else {
      showRecognitionResult("", "Network slow");
    }
    return;
  }

  if (recorder.timedOut() && mode == AppMode::Recording) {
    mode = AppMode::Recognizing;
  }

  drainAudioToAsr();
  finalizeAsrIfReady();

  if (asrSessionStarted && asrClient.done()) {
    if (asrClient.failed()) {
      showRecognitionResult(
          "",
          asrClient.result().error.empty() ? "ASR failed"
                                           : asrClient.result().error.c_str());
      return;
    }
    showRecognitionResult(asrClient.result().text);
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
                                                  M5.BtnB.isPressed(),
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
      if (mode == AppMode::Result) {
        pageModel.nextPage();
      }
      break;
    case InputEvent::ReturnToRecording:
      returnToRecordReady();
      break;
    case InputEvent::None:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("[boot] StickS3 ASR starting");

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
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  wifiCredentialStore.begin();
  applyWifiCredentials(wifiCredentialStore.load(runtimeConfig));
  Serial.printf("[boot] wifiConfigured=%d asrReady=%d\n",
                wifiConfigured ? 1 : 0,
                asrReady ? 1 : 0);
  Serial.printf("[boot] psram total=%lu free=%lu heap=%lu\n",
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<unsigned long>(ESP.getFreePsram()),
                static_cast<unsigned long>(ESP.getFreeHeap()));

  const AudioFormat audioFormat;
  if (!recorder.begin(audioFormat,
                      kRecordingChunkMs,
                      kMaxRecordingSeconds,
                      kRecordingQueuedChunks)) {
    errorText = "Mic init failed";
    mode = AppMode::Error;
  } else {
    txBuffer.assign(recorder.chunkBytes(), 0);
    mode = wifiConfigured ? AppMode::Connecting : AppMode::Idle;
    Serial.printf("[boot] recorder chunk=%u maxSec=%u buffer=%lu psram=%d\n",
                  static_cast<unsigned>(recorder.chunkBytes()),
                  static_cast<unsigned>(kMaxRecordingSeconds),
                  static_cast<unsigned long>(recorder.bufferCapacityBytes()),
                  recorder.bufferAllocatedInPsram() ? 1 : 0);
  }

  if (wifiConfigured) {
    startWifi(millis());
  } else {
    startProvisioning("AP setup");
  }
  logRuntimeState(millis(), true);
}

void loop() {
  uint32_t nowMs = millis();
  M5.update();
  logButtonState(nowMs);

  updateDisplayOrientation(nowMs);
  maintainWifi(nowMs);
  handleInput(nowMs);
  nowMs = millis();
  updateSpeechFlow(nowMs);
  displayUi.render(buildUiState(nowMs), nowMs);
  logRuntimeState(nowMs);

  delay(appLoopDelayMs(mode, lowLatencyAudioActive(), provisioningPortal.active()));
}
