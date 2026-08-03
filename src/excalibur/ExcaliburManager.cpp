#include "excalibur/ExcaliburManager.h"

#include <cstdio>
#include <cstring>

#ifndef UNIT_TEST
#include <Arduino.h>
#endif

namespace stick_s3_asr {

namespace {

#ifndef UNIT_TEST
ExcaliburManager* activeManager = nullptr;
#endif
char kStickStatusActionName[] = "stick.status";
char kStickRebootActionName[] = "stick.reboot";

#ifndef UNIT_TEST
int handleStickStatus(excalibur_client_t* client,
                      char* payloadJson,
                      char* actionId) {
  (void)payloadJson;
  return ExcaliburManager::handleSdkCommand(
      ExcaliburCommandType::PublishStatus, client, actionId);
}

int handleStickReboot(excalibur_client_t* client,
                      char* payloadJson,
                      char* actionId) {
  (void)payloadJson;
  return ExcaliburManager::handleSdkCommand(
      ExcaliburCommandType::Reboot, client, actionId);
}
#endif

const char* jsonBool(bool value) {
  return value ? "true" : "false";
}

const char* healthForSnapshot(const ExcaliburRuntimeSnapshot& snapshot) {
  if (!snapshot.wifiConnected) return "offline";
  return snapshot.asrReady ? "online" : "degraded";
}

void appendJsonString(std::string& out, const std::string& value) {
  out.push_back('"');
  for (char c : value) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  out.push_back('"');
}

void appendJsonField(std::string& out,
                     const char* name,
                     const std::string& value,
                     bool comma = true) {
  out.push_back('"');
  out += name;
  out += "\":";
  appendJsonString(out, value);
  if (comma) out.push_back(',');
}

void appendJsonField(std::string& out,
                     const char* name,
                     const char* value,
                     bool comma = true) {
  appendJsonField(out, name, std::string(value ? value : ""), comma);
}

template <typename T>
void appendNumberField(std::string& out,
                       const char* name,
                       T value,
                       bool comma = true) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld",
                static_cast<long long>(value));
  out.push_back('"');
  out += name;
  out += "\":";
  out += buffer;
  if (comma) out.push_back(',');
}

void appendBoolField(std::string& out,
                     const char* name,
                     bool value,
                     bool comma = true) {
  out.push_back('"');
  out += name;
  out += "\":";
  out += jsonBool(value);
  if (comma) out.push_back(',');
}

void appendBatteryLevelField(std::string& out,
                             const char* name,
                             int value,
                             bool comma = true) {
  out.push_back('"');
  out += name;
  out += "\":";
  if (value < 0) {
    out += "null";
  } else {
    const int clamped = value > 100 ? 100 : value;
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%d", clamped);
    out += buffer;
  }
  if (comma) out.push_back(',');
}

bool elapsed(uint32_t nowMs, uint32_t lastMs, uint32_t intervalMs) {
  return lastMs == 0 || nowMs - lastMs >= intervalMs;
}

}  // namespace

void ExcaliburManager::begin(const RuntimeConfig& config) {
  enabled_ = hasExcaliburManagement(config);
  softwareVersion_ = config.softwareVersion.empty() ? APP_SOFTWARE_VERSION
                                                    : config.softwareVersion;
#ifndef UNIT_TEST
  activeManager = this;
#endif
}

void ExcaliburManager::loop(uint32_t nowMs,
                            bool wifiOnline,
                            const ExcaliburRuntimeSnapshot& snapshot) {
  if (!enabled_) return;
  if (wifiOnline && !started_) {
    tryStart(nowMs);
  }
  if (!connected()) return;

  ExcaliburRuntimeSnapshot versionedSnapshot = snapshot;
  if (versionedSnapshot.softwareVersion.empty()) {
    versionedSnapshot.softwareVersion = softwareVersion_;
  }
  drainCommands(nowMs, versionedSnapshot);
  publishSnapshot(nowMs, versionedSnapshot);
}

bool ExcaliburManager::connected() const {
#ifndef UNIT_TEST
  return started_ && client_.connection_status == 1;
#else
  return started_;
#endif
}

bool ExcaliburManager::rebootDue(uint32_t nowMs) const {
  return rebootAtMs_ != 0 && nowMs - rebootAtMs_ < 0x80000000UL;
}

void ExcaliburManager::clearRebootRequest() {
  rebootAtMs_ = 0;
}

bool ExcaliburManager::enqueueCommand(ExcaliburCommandType type,
                                      const char* actionId) {
  if (type == ExcaliburCommandType::None || actionId == nullptr ||
      actionId[0] == '\0') {
    return false;
  }
  const size_t actionIdLen = std::strlen(actionId);
  if (actionIdLen >= sizeof(commandQueue_[0].actionId)) return false;

#ifndef UNIT_TEST
  portENTER_CRITICAL(&commandMux_);
#endif
  bool queued = false;
  if (commandCount_ < kCommandQueueCapacity) {
    ExcaliburCommand& slot = commandQueue_[commandTail_];
    slot.type = type;
    std::memset(slot.actionId, 0, sizeof(slot.actionId));
    std::memcpy(slot.actionId, actionId, actionIdLen);
    commandTail_ = (commandTail_ + 1) % kCommandQueueCapacity;
    ++commandCount_;
    queued = true;
  }
#ifndef UNIT_TEST
  portEXIT_CRITICAL(&commandMux_);
#endif
  return queued;
}

bool ExcaliburManager::takeQueuedCommand(ExcaliburCommand& out) {
#ifndef UNIT_TEST
  portENTER_CRITICAL(&commandMux_);
#endif
  bool hasCommand = false;
  if (commandCount_ > 0) {
    out = commandQueue_[commandHead_];
    commandQueue_[commandHead_] = {};
    commandHead_ = (commandHead_ + 1) % kCommandQueueCapacity;
    --commandCount_;
    hasCommand = true;
  }
#ifndef UNIT_TEST
  portEXIT_CRITICAL(&commandMux_);
#endif
  return hasCommand;
}

const char* ExcaliburManager::commandName(ExcaliburCommandType type) {
  switch (type) {
    case ExcaliburCommandType::PublishStatus:
      return "stick.status";
    case ExcaliburCommandType::Reboot:
      return "stick.reboot";
    case ExcaliburCommandType::None:
      break;
  }
  return "unknown";
}

const char* ExcaliburManager::modeName(AppMode mode) {
  switch (mode) {
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
  return "Unknown";
}

std::string ExcaliburManager::buildShadowJson(
    const ExcaliburRuntimeSnapshot& snapshot) {
  std::string out;
  out.reserve(520);
  out.push_back('{');
  appendJsonField(out, "health", healthForSnapshot(snapshot));
  appendJsonField(out, "software_type", "stick-s3-asr");
  appendJsonField(out, "software_version", snapshot.softwareVersion);
  appendJsonField(out, "hardware_type", "M5StickS3");
  out += "\"runtime\":{";
  appendJsonField(out, "mode", modeName(snapshot.mode));
  appendBoolField(out, "wifi_connected", snapshot.wifiConnected);
  appendBoolField(out, "wifi_configured", snapshot.wifiConfigured);
  appendBoolField(out, "asr_ready", snapshot.asrReady);
  appendBoolField(out, "recording", snapshot.recordingActive, false);
  out += "},\"network\":{";
  appendNumberField(out, "rssi", snapshot.wifiRssi, false);
  out += "},\"power\":{";
  appendBatteryLevelField(out, "battery_level", snapshot.batteryLevel);
  appendBoolField(out, "charging", snapshot.batteryCharging, false);
  out += "},\"memory\":{";
  appendNumberField(out, "heap_free", snapshot.freeHeap);
  appendNumberField(out, "psram_free", snapshot.freePsram, false);
  out += "},\"asr\":{";
  appendNumberField(out, "success_count", snapshot.asrSuccessCount);
  appendNumberField(out, "failure_count", snapshot.asrFailureCount, false);
  out += "}}";
  return out;
}

std::string ExcaliburManager::buildSystemTelemetryJson(
    const ExcaliburRuntimeSnapshot& snapshot) {
  std::string out;
  out.reserve(360);
  out.push_back('{');
  appendNumberField(out, "uptime_ms", snapshot.uptimeMs);
  appendJsonField(out, "mode", modeName(snapshot.mode));
  appendBoolField(out, "wifi_connected", snapshot.wifiConnected);
  appendBoolField(out, "wifi_configured", snapshot.wifiConfigured);
  appendBoolField(out, "asr_ready", snapshot.asrReady);
  appendNumberField(out, "heap_free", snapshot.freeHeap);
  appendNumberField(out, "psram_free", snapshot.freePsram);
  appendNumberField(out, "rssi", snapshot.wifiRssi);
  appendNumberField(out, "recording_ms", snapshot.recordingMs);
  appendNumberField(out, "asr_success_count", snapshot.asrSuccessCount);
  appendNumberField(out, "asr_failure_count", snapshot.asrFailureCount, false);
  out.push_back('}');
  return out;
}

std::string ExcaliburManager::buildBatteryTelemetryJson(
    const ExcaliburRuntimeSnapshot& snapshot) {
  std::string out;
  out.reserve(48);
  out.push_back('{');
  appendBatteryLevelField(out, "level", snapshot.batteryLevel);
  appendBoolField(out, "charging", snapshot.batteryCharging, false);
  out.push_back('}');
  return out;
}

bool ExcaliburManager::tryStart(uint32_t nowMs) {
  if (started_) return true;
  if (lastStartAttemptMs_ != 0 &&
      nowMs - lastStartAttemptMs_ < kStartRetryMs) {
    return false;
  }

  lastStartAttemptMs_ = nowMs == 0 ? 1 : nowMs;
  startFailed_ = false;
#ifndef UNIT_TEST
  client_ = {};
  client_.use_device_config_data = false;

  if (excalibur_init(&client_) != EXCALIBUR_SUCCESS) {
    Serial.println("[excalibur] init failed");
    startFailed_ = true;
    return false;
  }

  if (excalibur_add_action_handler(&client_, handleStickStatus,
                                   kStickStatusActionName) !=
          EXCALIBUR_SUCCESS ||
      excalibur_add_action_handler(&client_, handleStickReboot,
                                   kStickRebootActionName) !=
          EXCALIBUR_SUCCESS) {
    Serial.println("[excalibur] action registration failed");
    excalibur_destroy(&client_);
    startFailed_ = true;
    return false;
  }

  if (excalibur_start(&client_) != EXCALIBUR_SUCCESS) {
    Serial.println("[excalibur] start failed");
    excalibur_destroy(&client_);
    startFailed_ = true;
    return false;
  }

  started_ = true;
  Serial.println("[excalibur] started");
  return true;
#else
  if (testHooks_ != nullptr) ++testHooks_->initCalls;
  if (testHooks_ != nullptr && !testHooks_->initSucceeds) {
    startFailed_ = true;
    return false;
  }

  if (testHooks_ != nullptr) {
    testHooks_->registeredActions.push_back(kStickStatusActionName);
    testHooks_->registeredActions.push_back(kStickRebootActionName);
  }
  if (testHooks_ != nullptr && !testHooks_->actionRegistrationSucceeds) {
    ++testHooks_->destroyCalls;
    startFailed_ = true;
    return false;
  }

  if (testHooks_ != nullptr) ++testHooks_->startCalls;
  if (testHooks_ != nullptr && !testHooks_->startSucceeds) {
    ++testHooks_->destroyCalls;
    startFailed_ = true;
    return false;
  }

  started_ = true;
  return true;
#endif
}

void ExcaliburManager::publishSnapshot(
    uint32_t nowMs,
    const ExcaliburRuntimeSnapshot& snapshot) {
  if (elapsed(nowMs, lastShadowAttemptMs_, kShadowPublishMs)) {
    const std::string shadow = buildShadowJson(snapshot);
#ifndef UNIT_TEST
    excalibur_publish_shadow(&client_, const_cast<char*>(shadow.c_str()));
#else
    if (testHooks_ == nullptr || testHooks_->publishShadowSucceeds) {
      if (testHooks_ != nullptr) testHooks_->shadows.push_back(shadow);
    }
#endif
    lastShadowAttemptMs_ = nowMs == 0 ? 1 : nowMs;
  }

  if (elapsed(nowMs, lastTelemetryPublishMs_, kTelemetryPublishMs)) {
    const std::string systemTelemetry = buildSystemTelemetryJson(snapshot);
    const std::string batteryTelemetry = buildBatteryTelemetryJson(snapshot);
#ifndef UNIT_TEST
    excalibur_err_t systemRet = excalibur_publish_telemetry(
        &client_, const_cast<char*>("device_agent_system_stats"),
        systemTelemetrySequence_,
        const_cast<char*>(systemTelemetry.c_str()));
    if (systemRet == EXCALIBUR_SUCCESS) ++systemTelemetrySequence_;
    excalibur_err_t batteryRet = excalibur_publish_telemetry(
        &client_, const_cast<char*>("battery"), batteryTelemetrySequence_,
        const_cast<char*>(batteryTelemetry.c_str()));
    if (batteryRet == EXCALIBUR_SUCCESS) ++batteryTelemetrySequence_;
#else
    if (testHooks_ != nullptr && testHooks_->publishTelemetrySucceeds) {
      testHooks_->telemetry.push_back({"device_agent_system_stats",
                                       systemTelemetrySequence_,
                                       systemTelemetry});
      testHooks_->telemetry.push_back(
          {"battery", batteryTelemetrySequence_, batteryTelemetry});
      ++systemTelemetrySequence_;
      ++batteryTelemetrySequence_;
    }
#endif
    lastTelemetryPublishMs_ = nowMs == 0 ? 1 : nowMs;
  }
}

void ExcaliburManager::drainCommands(
    uint32_t nowMs,
    const ExcaliburRuntimeSnapshot& snapshot) {
  ExcaliburCommand command;
  while (takeQueuedCommand(command)) {
    switch (command.type) {
      case ExcaliburCommandType::PublishStatus: {
        const std::string status = buildSystemTelemetryJson(snapshot);
#ifndef UNIT_TEST
        excalibur_err_t telemetryRet = excalibur_publish_telemetry(
            &client_, const_cast<char*>("device_agent_system_stats"),
            systemTelemetrySequence_,
            const_cast<char*>(status.c_str()));
        if (telemetryRet == EXCALIBUR_SUCCESS) {
          ++systemTelemetrySequence_;
          excalibur_publish_action_completed(&client_, command.actionId);
        } else {
          excalibur_publish_action_status(
              &client_, command.actionId, 0, EXCALIBUR_COMMAND_FAILED,
              const_cast<char*>("Status telemetry publish failed"));
        }
#else
        if (testHooks_ != nullptr) {
          if (testHooks_->publishTelemetrySucceeds) {
            testHooks_->telemetry.push_back({"device_agent_system_stats",
                                             systemTelemetrySequence_,
                                             status});
            ++systemTelemetrySequence_;
            testHooks_->actionStatuses.push_back(
                {command.actionId, "Completed", 100, ""});
          } else {
            testHooks_->actionStatuses.push_back(
                {command.actionId, "Failed", 0,
                 "Status telemetry publish failed"});
          }
        }
#endif
        break;
      }
      case ExcaliburCommandType::Reboot:
#ifndef UNIT_TEST
        if (excalibur_publish_action_completed(&client_, command.actionId) ==
            EXCALIBUR_SUCCESS) {
          rebootAtMs_ = nowMs + kRebootDelayMs;
        } else {
          excalibur_publish_action_status(
              &client_, command.actionId, 0, EXCALIBUR_COMMAND_FAILED,
              const_cast<char*>("Reboot status publish failed"));
        }
#else
        if (testHooks_ != nullptr) {
          if (testHooks_->publishActionStatusSucceeds) {
            testHooks_->actionStatuses.push_back(
                {command.actionId, "Completed", 100, ""});
            rebootAtMs_ = nowMs + kRebootDelayMs;
          } else {
            testHooks_->actionStatuses.push_back(
                {command.actionId, "Failed", 0,
                 "Reboot status publish failed"});
          }
        }
#endif
        break;
      case ExcaliburCommandType::None:
        break;
    }
  }
}

#ifdef UNIT_TEST
int ExcaliburManager::handleTestCommand(ExcaliburCommandType type,
                                        const char* actionId) {
  if (actionId == nullptr) return -1;
  if (testHooks_ != nullptr) {
    testHooks_->actionStatuses.push_back({actionId, "Running", 0, ""});
  }
  if (!enqueueCommand(type, actionId)) {
    if (testHooks_ != nullptr) {
      testHooks_->actionStatuses.push_back(
          {actionId, "Failed", 0, "Command queue full"});
    }
    return -1;
  }
  return 0;
}
#else
int ExcaliburManager::handleSdkCommand(ExcaliburCommandType type,
                                       excalibur_client_t* client,
                                       char* actionId) {
  if (client == nullptr || actionId == nullptr || activeManager == nullptr) {
    return -1;
  }
  excalibur_publish_action_running(client, actionId, 0);
  if (!activeManager->enqueueCommand(type, actionId)) {
    excalibur_publish_action_status(client, actionId, 0,
                                    EXCALIBUR_COMMAND_FAILED,
                                    const_cast<char*>("Command queue full"));
    return -1;
  }
  return 0;
}
#endif

}  // namespace stick_s3_asr
