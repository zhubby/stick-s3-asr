#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#ifdef UNIT_TEST
#include <vector>
#endif

#include "AppConfig.h"
#include "AppTypes.h"

#ifndef UNIT_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

extern "C" {
#include "excalibur_sdk.h"
}
#else
#ifndef EXCALIBUR_ACTION_ID_STR_LEN
#define EXCALIBUR_ACTION_ID_STR_LEN 37
#endif
#endif

namespace stick_s3_asr {

enum class ExcaliburCommandType {
  None,
  PublishStatus,
  Reboot,
};

struct ExcaliburCommand {
  ExcaliburCommandType type = ExcaliburCommandType::None;
  char actionId[EXCALIBUR_ACTION_ID_STR_LEN] = {};
};

#ifdef UNIT_TEST
struct ExcaliburTestTelemetry {
  std::string stream;
  uint64_t sequence = 0;
  std::string payload;
};

struct ExcaliburTestActionStatus {
  std::string actionId;
  std::string state;
  int progress = 0;
  std::string error;
};

struct ExcaliburTestHooks {
  bool initSucceeds = true;
  bool actionRegistrationSucceeds = true;
  bool startSucceeds = true;
  bool publishShadowSucceeds = true;
  bool publishTelemetrySucceeds = true;
  bool publishActionStatusSucceeds = true;
  int initCalls = 0;
  int startCalls = 0;
  int destroyCalls = 0;
  std::vector<std::string> registeredActions;
  std::vector<std::string> shadows;
  std::vector<ExcaliburTestTelemetry> telemetry;
  std::vector<ExcaliburTestActionStatus> actionStatuses;
};
#endif

struct ExcaliburRuntimeSnapshot {
  AppMode mode = AppMode::Boot;
  bool wifiConnected = false;
  bool wifiConfigured = false;
  bool asrReady = false;
  bool recordingActive = false;
  uint32_t recordingMs = 0;
  int32_t wifiRssi = 0;
  int batteryLevel = -1;
  bool batteryCharging = false;
  uint32_t freeHeap = 0;
  uint32_t freePsram = 0;
  uint32_t uptimeMs = 0;
  uint32_t asrSuccessCount = 0;
  uint32_t asrFailureCount = 0;
  std::string softwareVersion = APP_SOFTWARE_VERSION;
};

class ExcaliburManager {
 public:
  void begin(const RuntimeConfig& config);
  void loop(uint32_t nowMs,
            bool wifiOnline,
            const ExcaliburRuntimeSnapshot& snapshot);

  bool enabled() const { return enabled_; }
  bool started() const { return started_; }
  bool connected() const;
  bool startFailed() const { return startFailed_; }
  bool rebootDue(uint32_t nowMs) const;
  void clearRebootRequest();

#ifdef UNIT_TEST
  void setTestHooks(ExcaliburTestHooks* hooks) { testHooks_ = hooks; }
  int handleTestCommand(ExcaliburCommandType type, const char* actionId);
  bool takeQueuedCommandForTest(ExcaliburCommand& out) {
    return takeQueuedCommand(out);
  }
  size_t queuedCommandCount() const { return commandCount_; }
#endif

  static const char* commandName(ExcaliburCommandType type);
  static const char* modeName(AppMode mode);
  static std::string buildShadowJson(const ExcaliburRuntimeSnapshot& snapshot);
  static std::string buildSystemTelemetryJson(
      const ExcaliburRuntimeSnapshot& snapshot);
  static std::string buildBatteryTelemetryJson(
      const ExcaliburRuntimeSnapshot& snapshot);

#ifndef UNIT_TEST
  static int handleSdkCommand(ExcaliburCommandType type,
                              excalibur_client_t* client,
                              char* actionId);
#endif

 private:
  static constexpr uint32_t kStartRetryMs = 60000;
  static constexpr uint32_t kShadowPublishMs = 30000;
  static constexpr uint32_t kTelemetryPublishMs = 15000;
  static constexpr uint32_t kRebootDelayMs = 750;
  static constexpr size_t kCommandQueueCapacity = 4;

  bool tryStart(uint32_t nowMs);
  void publishSnapshot(uint32_t nowMs,
                       const ExcaliburRuntimeSnapshot& snapshot);
  void drainCommands(uint32_t nowMs,
                     const ExcaliburRuntimeSnapshot& snapshot);
  bool enqueueCommand(ExcaliburCommandType type, const char* actionId);
  bool takeQueuedCommand(ExcaliburCommand& out);

  bool enabled_ = false;
  bool started_ = false;
  bool startFailed_ = false;
  uint32_t lastStartAttemptMs_ = 0;
  uint32_t lastShadowAttemptMs_ = 0;
  uint32_t lastTelemetryPublishMs_ = 0;
  uint32_t rebootAtMs_ = 0;
  uint64_t systemTelemetrySequence_ = 0;
  uint64_t batteryTelemetrySequence_ = 0;
  std::string softwareVersion_ = APP_SOFTWARE_VERSION;
  ExcaliburCommand commandQueue_[kCommandQueueCapacity] = {};
  size_t commandHead_ = 0;
  size_t commandTail_ = 0;
  size_t commandCount_ = 0;

#ifndef UNIT_TEST
  excalibur_client_t client_ = {};
  portMUX_TYPE commandMux_ = portMUX_INITIALIZER_UNLOCKED;
#else
  ExcaliburTestHooks* testHooks_ = nullptr;
#endif
};

}  // namespace stick_s3_asr
