#ifndef UNIT_TEST

#include "wifi/WifiCredentialStore.h"

namespace stick_s3_asr {

bool WifiCredentialStore::begin() {
  ready_ = prefs_.begin("stick-asr", false);
  return ready_;
}

WifiCredentials WifiCredentialStore::load(const RuntimeConfig& fallbackConfig) {
  WifiCredentials credentials;
  if (ready_) {
    credentials.ssid = prefs_.getString("ssid", "").c_str();
    credentials.password = prefs_.getString("pass", "").c_str();
  }
  if (!credentials.present() && hasWifiCredentials(fallbackConfig)) {
    credentials.ssid = fallbackConfig.wifiSsid;
    credentials.password = fallbackConfig.wifiPassword;
  }
  return credentials;
}

bool WifiCredentialStore::save(const WifiCredentials& credentials) {
  if (!ready_ || !credentials.present()) return false;
  return prefs_.putString("ssid", credentials.ssid.c_str()) > 0 &&
         prefs_.putString("pass", credentials.password.c_str()) ==
             credentials.password.length();
}

void WifiCredentialStore::clear() {
  if (ready_) {
    prefs_.remove("ssid");
    prefs_.remove("pass");
  }
}

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
