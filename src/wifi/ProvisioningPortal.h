#pragma once

#ifndef UNIT_TEST

#include <DNSServer.h>
#include <WebServer.h>

#include <string>

#include "wifi/WifiCredentialStore.h"

namespace stick_s3_asr {

class ProvisioningPortal {
 public:
  ProvisioningPortal();

  bool begin(const std::string& apSsid, const std::string& apPassword);
  void loop();
  void stop();

  bool active() const { return active_; }
  const std::string& ssid() const { return apSsid_; }
  const std::string& password() const { return apPassword_; }
  const char* url() const { return "http://192.168.4.1"; }

  bool hasPendingCredentials() const { return pendingCredentials_.present(); }
  WifiCredentials takePendingCredentials();

 private:
  void configureRoutes();
  void handleRoot();
  void handleSave();
  void handleNotFound();
  String htmlPage(const String& message) const;

  DNSServer dns_;
  WebServer server_;
  bool routesConfigured_ = false;
  bool active_ = false;
  std::string apSsid_;
  std::string apPassword_;
  WifiCredentials pendingCredentials_;
};

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
