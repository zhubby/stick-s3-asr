#ifndef UNIT_TEST

#include "wifi/ProvisioningPortal.h"

#include <WiFi.h>

namespace stick_s3_asr {

namespace {
constexpr uint16_t kDnsPort = 53;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}
}  // namespace

ProvisioningPortal::ProvisioningPortal() : server_(80) {}

bool ProvisioningPortal::begin(const std::string& apSsid,
                               const std::string& apPassword) {
  if (active_) return true;

  apSsid_ = apSsid;
  apPassword_ = apPassword;
  pendingCredentials_ = {};

  if (!routesConfigured_) {
    configureRoutes();
    routesConfigured_ = true;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  const bool apStarted =
      apPassword_.empty()
          ? WiFi.softAP(apSsid_.c_str())
          : WiFi.softAP(apSsid_.c_str(), apPassword_.c_str());
  if (!apStarted) {
    return false;
  }

  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(kDnsPort, "*", kApIp);
  server_.begin();
  active_ = true;
  return true;
}

void ProvisioningPortal::loop() {
  if (!active_) return;
  dns_.processNextRequest();
  server_.handleClient();
}

void ProvisioningPortal::stop() {
  if (!active_) return;
  server_.stop();
  dns_.stop();
  WiFi.softAPdisconnect(true);
  active_ = false;
}

WifiCredentials ProvisioningPortal::takePendingCredentials() {
  WifiCredentials credentials = pendingCredentials_;
  pendingCredentials_ = {};
  return credentials;
}

void ProvisioningPortal::configureRoutes() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

void ProvisioningPortal::handleRoot() {
  server_.send(200, "text/html; charset=utf-8", htmlPage(""));
}

void ProvisioningPortal::handleSave() {
  const String ssid = server_.arg("ssid");
  const String password = server_.arg("password");

  if (ssid.length() == 0 || ssid.length() > 32) {
    server_.send(400, "text/html; charset=utf-8",
                 htmlPage("Wi-Fi 名称长度需要在 1-32 个字符之间"));
    return;
  }
  if (password.length() > 63) {
    server_.send(400, "text/html; charset=utf-8",
                 htmlPage("Wi-Fi 密码不能超过 63 个字符"));
    return;
  }

  pendingCredentials_.ssid = ssid.c_str();
  pendingCredentials_.password = password.c_str();
  server_.send(200, "text/html; charset=utf-8",
               htmlPage("已保存，设备正在连接 Wi-Fi"));
}

void ProvisioningPortal::handleNotFound() {
  server_.sendHeader("Location", String(url()), true);
  server_.send(302, "text/plain; charset=utf-8", "");
}

String ProvisioningPortal::htmlPage(const String& message) const {
  String page;
  page.reserve(2800);
  page += F("<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>StickS3 ASR 配网</title><style>");
  page += F(":root{color-scheme:dark}body{margin:0;background:#081112;color:#edf7f4;");
  page += F("font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}");
  page += F("main{max-width:420px;margin:0 auto;padding:28px 20px}");
  page += F("h1{font-size:25px;margin:0 0 8px}p{color:#9fb0ad;line-height:1.55}");
  page += F("form{margin-top:22px}.field{margin:14px 0}label{display:block;");
  page += F("font-size:13px;color:#9fb0ad;margin-bottom:7px}");
  page += F("input{width:100%;box-sizing:border-box;border:1px solid #2d4846;");
  page += F("border-radius:8px;background:#101d1e;color:#fff;padding:13px 12px;");
  page += F("font-size:16px}button{width:100%;border:0;border-radius:8px;");
  page += F("background:#28dfc4;color:#062220;font-weight:700;padding:14px;");
  page += F("font-size:16px;margin-top:10px}.msg{border:1px solid #56631c;");
  page += F("background:#272b13;color:#ffe68a;border-radius:8px;padding:10px 12px}");
  page += F(".meta{margin-top:22px;font-size:13px;color:#7e918d}</style></head><body>");
  page += F("<main><h1>StickS3 ASR 配网</h1>");
  page += F("<p>选择当前环境的 Wi-Fi，保存后设备会自动切回语音识别界面。</p>");
  if (message.length() > 0) {
    page += F("<div class=\"msg\">");
    page += htmlEscape(message);
    page += F("</div>");
  }
  page += F("<form method=\"post\" action=\"/save\">");
  page += F("<div class=\"field\"><label>Wi-Fi 名称</label>");
  page += F("<input name=\"ssid\" maxlength=\"32\" autocomplete=\"off\" required></div>");
  page += F("<div class=\"field\"><label>Wi-Fi 密码</label>");
  page += F("<input name=\"password\" type=\"password\" maxlength=\"63\"></div>");
  page += F("<button type=\"submit\">保存并连接</button></form>");
  page += F("<p class=\"meta\">配网地址：192.168.4.1</p>");
  page += F("</main></body></html>");
  return page;
}

}  // namespace stick_s3_asr

#endif  // UNIT_TEST
