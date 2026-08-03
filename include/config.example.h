#pragma once

// Copy this file to include/config.local.h and fill in real values.
// include/config.local.h is ignored by git.

// Optional. Leave Wi-Fi empty to configure it from the device AP portal.
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Volcengine / Doubao streaming ASR credentials.
// New console: fill VOLC_API_KEY.
// Legacy console: fill VOLC_APP_KEY and VOLC_ACCESS_KEY.
#define VOLC_API_KEY "your-api-key"
#define VOLC_APP_KEY "your-app-key-or-app-id"
#define VOLC_ACCESS_KEY "your-access-key-or-access-token"

// Streaming ASR 2.0 hourly SKU. Use volc.bigasr.sauc.duration for older ASR 1.0 apps.
#define VOLC_RESOURCE_ID "volc.seedasr.sauc.duration"
#define VOLC_ASR_ENDPOINT "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel"

// SoftAP password shown on the device during Wi-Fi pairing.
#define PROVISION_AP_PASSWORD "stick1234"
