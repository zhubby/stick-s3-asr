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

#ifndef VOLC_RESOURCE_ID
#define VOLC_RESOURCE_ID "volc.seedasr.sauc.duration"
#endif

#ifndef VOLC_ASR_ENDPOINT
#define VOLC_ASR_ENDPOINT "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel"
#endif

#ifndef PROVISION_AP_PASSWORD
#define PROVISION_AP_PASSWORD "stick1234"
#endif

namespace stick_s3_asr {

struct RuntimeConfig {
  std::string wifiSsid;
  std::string wifiPassword;
  std::string volcAppKey;
  std::string volcAccessKey;
  std::string volcResourceId;
  std::string volcEndpoint;
  std::string provisionApPassword;
};

inline RuntimeConfig loadRuntimeConfig() {
  return {
      WIFI_SSID,
      WIFI_PASSWORD,
      VOLC_APP_KEY,
      VOLC_ACCESS_KEY,
      VOLC_RESOURCE_ID,
      VOLC_ASR_ENDPOINT,
      PROVISION_AP_PASSWORD,
  };
}

inline bool hasWifiCredentials(const RuntimeConfig& config) {
  return !config.wifiSsid.empty();
}

inline bool hasAsrSecrets(const RuntimeConfig& config) {
  return !config.volcAppKey.empty() && !config.volcAccessKey.empty() &&
         !config.volcResourceId.empty() && !config.volcEndpoint.empty();
}

inline bool hasRequiredSecrets(const RuntimeConfig& config) {
  return hasWifiCredentials(config) && hasAsrSecrets(config);
}

}  // namespace stick_s3_asr
