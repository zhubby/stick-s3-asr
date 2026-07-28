#pragma once

#ifndef UNIT_TEST

#include <Preferences.h>

#include <string>

#include "AppConfig.h"

namespace stick_s3_asr {

struct WifiCredentials {
  std::string ssid;
  std::string password;

  bool present() const { return !ssid.empty(); }
};

class WifiCredentialStore {
 public:
  bool begin();
  WifiCredentials load(const RuntimeConfig& fallbackConfig);
  bool save(const WifiCredentials& credentials);
  void clear();

 private:
  Preferences prefs_;
  bool ready_ = false;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
