# StickS3 ASR

M5Stack StickS3 firmware for push-and-hold speech-to-text with ByteDance Volcengine/Doubao streaming ASR.

## Hardware Interaction

- Hold `KEY1 / BtnA` to record.
- Release `KEY1 / BtnA` to stop recording and wait for transcription.
- Press `KEY2 / BtnB` to move to the next text page.
- The hardware reset key remains a reset key.

## Setup

1. Copy `include/config.example.h` to `include/config.local.h`.
2. Fill in Volcengine ASR credentials. Wi-Fi can stay empty if you want to use device pairing.
3. Use `volc.seedasr.sauc.duration` for Doubao streaming ASR 2.0 hourly resources. Use `volc.bigasr.sauc.duration` only for older ASR 1.0 applications.
4. Build and upload with PlatformIO:

```bash
pio run -e m5stack-sticks3
pio run -e m5stack-sticks3 -t upload
```

## Wi-Fi Pairing

When no Wi-Fi is saved, or when the saved Wi-Fi cannot connect, StickS3 opens a pairing hotspot:

- SSID: shown on the screen, formatted like `StickS3-ASR-4B1C`
- Password: `PROVISION_AP_PASSWORD`, default `stick1234`
- Portal: `http://192.168.4.1`

Connect your phone to that hotspot, open the portal, enter the target Wi-Fi name and password, then submit. The device saves the credentials to ESP32 NVS and reconnects automatically. The saved Wi-Fi survives resets and firmware uploads unless NVS is erased.

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
