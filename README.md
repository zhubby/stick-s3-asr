# StickS3 ASR

M5Stack StickS3 firmware for push-and-hold speech-to-text with ByteDance Volcengine/Doubao streaming ASR.

## Hardware Interaction

- Hold `KEY1 / BtnA` to record.
- Release `KEY1 / BtnA` to stop recording and wait for transcription.
- Press `KEY2 / BtnB` on the result screen to return to the record-ready screen.
- The hardware reset key remains a reset key.

## Setup

1. Copy `include/config.example.h` to `include/config.local.h`.
2. Fill in Volcengine ASR credentials. For the new console, set `VOLC_API_KEY`; for the legacy console, set `VOLC_APP_KEY` and `VOLC_ACCESS_KEY`. Wi-Fi can stay empty if you want to use device pairing.
3. Use `volc.seedasr.sauc.duration` for Doubao streaming ASR 2.0 hourly resources. Use `volc.bigasr.sauc.duration` only for older ASR 1.0 applications.
4. Build and upload with PlatformIO:

```bash
pio run -e m5stack-sticks3
pio run -e m5stack-sticks3 -t upload
```

The Excalibur ESP-IDF SDK is resolved by ESP-IDF Component Manager from `src/idf_component.yml`. `dependencies.lock` pins the resolved SDK commit for repeatable builds. When updating the SDK, commit and push the Excalibur repo first, then rebuild this firmware to refresh the lock file.

## Wi-Fi Pairing

When no Wi-Fi is saved, or when the saved Wi-Fi cannot connect, StickS3 opens a pairing hotspot:

- SSID: shown on the screen, formatted like `StickS3-ASR-4B1C`
- Password: `PROVISION_AP_PASSWORD`, default `stick1234`
- Portal: `http://192.168.4.1`

Connect your phone to that hotspot, open the portal, enter the target Wi-Fi name and password, then submit. The device saves the credentials to ESP32 NVS and reconnects automatically. The saved Wi-Fi survives resets and firmware uploads unless NVS is erased.

## Excalibur Device Management

This firmware can connect to the Excalibur native MQTT device protocol after Wi-Fi is online. It publishes device shadow, `device_agent_system_stats`, and `battery` telemetry, and registers `stick.status` and `stick.reboot` commands. ASR transcripts are not sent to Excalibur telemetry or shadow.

The SDK dependency is pulled from `git@github.com:zhubby/excalibur.git`, path `sdk/excalibur-esp-idf-sdk`, using the commit resolved in `dependencies.lock`.

Development provisioning uses the Excalibur Console dev-auth JSON:

1. Create or select a device in Excalibur Console.
2. Download the device dev-auth JSON.
3. Save it locally as `data/device_config.json`. This file is ignored by git because it contains device private key material.
4. Build and upload the SPIFFS image:

```bash
pio run -e m5stack-sticks3 -t buildfs
pio run -e m5stack-sticks3 -t uploadfs
```

The SDK reads `/spiffs/device_config.json`. Wi-Fi pairing remains separate and still uses the StickS3 AP portal.

Excalibur OTA is intentionally not enabled in this v1 integration. Enable it only after adding firmware hash enforcement and rollout validation.

## Tests

Core logic is covered by native PlatformIO tests:

```bash
pio test -e native
```

Firmware build:

```bash
pio run -e m5stack-sticks3
```

## Notes

The ASR WebSocket protocol uses Volcengine's binary frame envelope. This firmware sends uncompressed JSON/audio frames by setting the protocol compression nibble to `NO_COMPRESSION`; if your resource requires gzip-only payloads, add gzip compression in `src/asr/VolcAsrProtocol.cpp`.

TLS is validated with the DigiCert Global Root G2 certificate embedded in `src/asr/VolcRootCa.h`. Re-check the certificate chain if Volcengine changes the ASR endpoint.
