#pragma once

#include <string>

#if __has_include("config.local.h")
#include "config.local.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef VOLC_APP_KEY
#define VOLC_APP_KEY ""
#endif

#ifndef VOLC_ACCESS_KEY
#define VOLC_ACCESS_KEY ""
#endif

#ifndef VOLC_API_KEY
#define VOLC_API_KEY ""
#endif

#ifndef VOLC_RESOURCE_ID
#define VOLC_RESOURCE_ID "volc.seedasr.sauc.duration"
#endif

#ifndef VOLC_ASR_ENDPOINT
#define VOLC_ASR_ENDPOINT "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel"
#endif

#ifndef PROVISION_AP_PASSWORD
#define PROVISION_AP_PASSWORD "stick1234"
#endif

#ifndef APP_SOFTWARE_VERSION
#define APP_SOFTWARE_VERSION "0.1.0"
#endif

#ifndef EXCALIBUR_ENABLED
#define EXCALIBUR_ENABLED 1
#endif

namespace stick_s3_asr {

struct RuntimeConfig {
  std::string wifiSsid;
  std::string wifiPassword;
  std::string volcApiKey;
  std::string volcAppKey;
  std::string volcAccessKey;
  std::string volcResourceId;
  std::string volcEndpoint;
  std::string provisionApPassword;
  bool excaliburEnabled = EXCALIBUR_ENABLED != 0;
  std::string softwareVersion;
};

inline RuntimeConfig loadRuntimeConfig() {
  RuntimeConfig config;
  config.wifiSsid = WIFI_SSID;
  config.wifiPassword = WIFI_PASSWORD;
  config.volcApiKey = VOLC_API_KEY;
  config.volcAppKey = VOLC_APP_KEY;
  config.volcAccessKey = VOLC_ACCESS_KEY;
  config.volcResourceId = VOLC_RESOURCE_ID;
  config.volcEndpoint = VOLC_ASR_ENDPOINT;
  config.provisionApPassword = PROVISION_AP_PASSWORD;
  config.excaliburEnabled = EXCALIBUR_ENABLED != 0;
  config.softwareVersion = APP_SOFTWARE_VERSION;
  return config;
}

inline bool hasWifiCredentials(const RuntimeConfig& config) {
  return !config.wifiSsid.empty();
}

inline bool hasAsrSecrets(const RuntimeConfig& config) {
  const bool hasNewApiKey =
      !config.volcApiKey.empty() ||
      config.volcAppKey.rfind("api-key-", 0) == 0 ||
      config.volcAccessKey.rfind("api-key-", 0) == 0;
  const bool hasLegacyKeys =
      !config.volcAppKey.empty() && !config.volcAccessKey.empty();
  return (hasNewApiKey || hasLegacyKeys) && !config.volcResourceId.empty() &&
         !config.volcEndpoint.empty();
}

inline bool hasRequiredSecrets(const RuntimeConfig& config) {
  return hasWifiCredentials(config) && hasAsrSecrets(config);
}

inline bool hasExcaliburManagement(const RuntimeConfig& config) {
  return config.excaliburEnabled;
}

}  // namespace stick_s3_asr
