#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "AppConfig.h"
#include "asr/VolcAsrProtocol.h"
#include "audio/AudioBuffer.h"
#include "input/InputController.h"
#include "ui/OrientationController.h"
#include "ui/PageModel.h"

using namespace stick_s3_asr;

void test_long_press_starts_and_release_stops() {
  InputController input(450);
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(true, false, AppMode::Idle, 1000)));
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(true, false, AppMode::Idle, 1400)));
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::StartRecording),
                    static_cast<int>(input.update(true, false, AppMode::Idle, 1450)));
  TEST_ASSERT_TRUE(input.recordingGestureActive());
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::StopRecording),
                    static_cast<int>(input.update(false, false, AppMode::Recording, 1800)));
  TEST_ASSERT_FALSE(input.recordingGestureActive());
}

void test_short_press_is_ignored() {
  InputController input(450);
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(true, false, AppMode::Idle, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(false, false, AppMode::Idle, 200)));
}

void test_next_page_only_in_result_mode() {
  InputController input(450);
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(false, true, AppMode::Idle, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::NextPage),
                    static_cast<int>(input.update(false, true, AppMode::Result, 1)));
}

void test_pairing_mode_ignores_record_and_page_buttons() {
  InputController input(450);
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(false, true, AppMode::Pairing, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(true, false, AppMode::Pairing, 1000)));
  TEST_ASSERT_EQUAL(static_cast<int>(InputEvent::None),
                    static_cast<int>(input.update(true, false, AppMode::Pairing, 1600)));
  TEST_ASSERT_FALSE(input.recordingGestureActive());
}

void test_runtime_config_splits_wifi_and_asr_readiness() {
  RuntimeConfig config;
  config.volcAppKey = "app";
  config.volcAccessKey = "access";
  config.volcResourceId = "volc.seedasr.sauc.duration";
  config.volcEndpoint = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel";

  TEST_ASSERT_TRUE(hasAsrSecrets(config));
  TEST_ASSERT_FALSE(hasWifiCredentials(config));
  TEST_ASSERT_FALSE(hasRequiredSecrets(config));

  config.wifiSsid = "lab";
  TEST_ASSERT_TRUE(hasWifiCredentials(config));
  TEST_ASSERT_TRUE(hasRequiredSecrets(config));
}

void test_orientation_controller_defaults_landscape_and_rotates_with_tilt() {
  OrientationController orientation(1);
  TEST_ASSERT_EQUAL_UINT8(1, orientation.rotation());
  TEST_ASSERT_TRUE(orientation.landscape());

  TEST_ASSERT_FALSE(orientation.update(false, 0.0f, 1.0f, 100));
  TEST_ASSERT_EQUAL_UINT8(1, orientation.rotation());

  TEST_ASSERT_TRUE(orientation.update(true, 0.0f, 1.0f, 100));
  TEST_ASSERT_EQUAL_UINT8(0, orientation.rotation());
  TEST_ASSERT_FALSE(orientation.landscape());

  TEST_ASSERT_FALSE(orientation.update(true, -1.0f, 0.0f, 200));
  TEST_ASSERT_EQUAL_UINT8(0, orientation.rotation());

  TEST_ASSERT_TRUE(orientation.update(true, -1.0f, 0.0f, 900));
  TEST_ASSERT_EQUAL_UINT8(3, orientation.rotation());
  TEST_ASSERT_TRUE(orientation.landscape());
}

void test_page_model_rebuilds_when_layout_changes() {
  PageModel pages(8, 2);
  pages.setText("一二三四五六七八九十十一十二");
  const size_t narrowCount = pages.pageCount();
  TEST_ASSERT_GREATER_THAN(1, narrowCount);

  pages.nextPage();
  pages.setLayout(24, 3);
  TEST_ASSERT_EQUAL_UINT32(0, pages.pageIndex());
  TEST_ASSERT_LESS_THAN(narrowCount, pages.pageCount());
}

void test_page_model_wraps_utf8_and_cycles_pages() {
  PageModel pages(6, 2);
  pages.setText("你好世界测试abc123下一页");
  TEST_ASSERT_GREATER_THAN(1, pages.pageCount());
  const std::string first = pages.page();
  pages.nextPage();
  TEST_ASSERT_NOT_EQUAL(0, std::strcmp(first.c_str(), pages.page().c_str()));
  for (size_t i = 1; i < pages.pageCount(); ++i) {
    pages.nextPage();
  }
  TEST_ASSERT_EQUAL_STRING(first.c_str(), pages.page().c_str());
}

void test_audio_ring_buffer_tracks_capacity_and_overflow() {
  AudioRingBuffer ring(4);
  const uint8_t input[] = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL_UINT32(4, ring.write(input, sizeof(input)));
  TEST_ASSERT_EQUAL_UINT32(1, ring.overflowCount());
  uint8_t output[3] = {};
  TEST_ASSERT_EQUAL_UINT32(3, ring.read(output, sizeof(output)));
  TEST_ASSERT_EQUAL_UINT8(1, output[0]);
  TEST_ASSERT_EQUAL_UINT8(2, output[1]);
  TEST_ASSERT_EQUAL_UINT8(3, output[2]);
  const uint8_t more[] = {6, 7};
  TEST_ASSERT_EQUAL_UINT32(2, ring.write(more, sizeof(more)));
  uint8_t rest[3] = {};
  TEST_ASSERT_EQUAL_UINT32(3, ring.read(rest, sizeof(rest)));
  TEST_ASSERT_EQUAL_UINT8(4, rest[0]);
  TEST_ASSERT_EQUAL_UINT8(6, rest[1]);
  TEST_ASSERT_EQUAL_UINT8(7, rest[2]);
}

void test_audio_ring_buffer_write_all_preserves_existing_bytes_on_overflow() {
  AudioRingBuffer ring(4);
  const uint8_t first[] = {1, 2, 3};
  const uint8_t tooLarge[] = {9, 9};
  TEST_ASSERT_TRUE(ring.writeAll(first, sizeof(first)));
  TEST_ASSERT_FALSE(ring.writeAll(tooLarge, sizeof(tooLarge)));
  TEST_ASSERT_EQUAL_UINT32(1, ring.overflowCount());
  TEST_ASSERT_EQUAL_UINT32(3, ring.available());
  uint8_t out[3] = {};
  TEST_ASSERT_EQUAL_UINT32(3, ring.read(out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8(1, out[0]);
  TEST_ASSERT_EQUAL_UINT8(2, out[1]);
  TEST_ASSERT_EQUAL_UINT8(3, out[2]);
}

void test_pcm_peak_handles_negative_samples() {
  const int16_t samples[] = {-100, 12, 32000, -1234};
  TEST_ASSERT_EQUAL_UINT16(32000, pcmPeak(samples, 4));
}

void test_volc_full_request_frame_contains_json_payload() {
  VolcAsrConfig config;
  config.appKey = "app";
  config.accessKey = "access";
  const auto frame = VolcAsrProtocol::makeFullClientRequest(config, "abc", 1);
  VolcFrameHeader header;
  std::string error;
  TEST_ASSERT_TRUE(VolcAsrProtocol::parseHeader(frame.data(), frame.size(), header, error));
  TEST_ASSERT_EQUAL_UINT8(1, header.version);
  TEST_ASSERT_EQUAL(static_cast<int>(VolcMessageType::FullClientRequest),
                    static_cast<int>(header.messageType));
  TEST_ASSERT_EQUAL_UINT8(VolcAsrProtocol::kSerializationJson, header.serialization);
  const std::string payload(reinterpret_cast<const char*>(frame.data() + header.payloadOffset),
                            header.payloadSize);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"rate\":16000"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, payload.find("\"model_name\":\"bigmodel\""));
}

void test_volc_audio_final_frame_uses_negative_sequence() {
  const uint8_t pcm[] = {0x01, 0x02};
  const auto frame = VolcAsrProtocol::makeAudioRequest(pcm, sizeof(pcm), 3, true);
  VolcFrameHeader header;
  std::string error;
  TEST_ASSERT_TRUE(VolcAsrProtocol::parseHeader(frame.data(), frame.size(), header, error));
  TEST_ASSERT_EQUAL(static_cast<int>(VolcMessageType::AudioOnlyRequest),
                    static_cast<int>(header.messageType));
  TEST_ASSERT_EQUAL(-3, header.sequence);
  TEST_ASSERT_EQUAL_UINT32(sizeof(pcm), header.payloadSize);
}

void test_volc_response_parses_text() {
  const std::string payload = "{\"event\":\"final\",\"text\":\"你好\"}";
  std::vector<uint8_t> frame = {
      0x11,
      static_cast<uint8_t>((static_cast<uint8_t>(VolcMessageType::FullServerResponse) << 4) |
                           VolcAsrProtocol::kFlagPositiveSequence),
      static_cast<uint8_t>(VolcAsrProtocol::kSerializationJson << 4),
      0x00,
      0x00,
      0x00,
      0x00,
      0x01,
      0x00,
      0x00,
      0x00,
      static_cast<uint8_t>(payload.size()),
  };
  frame.insert(frame.end(), payload.begin(), payload.end());
  const VolcResponse response = VolcAsrProtocol::parseResponse(frame.data(), frame.size());
  TEST_ASSERT_TRUE(response.error.empty());
  TEST_ASSERT_TRUE(response.final);
  TEST_ASSERT_EQUAL_STRING("你好", response.text.c_str());
}

void test_volc_response_final_flag_marks_completion_without_negative_sequence() {
  const std::string payload = "{\"text\":\"完成\"}";
  std::vector<uint8_t> frame = {
      0x11,
      static_cast<uint8_t>((static_cast<uint8_t>(VolcMessageType::FullServerResponse) << 4) |
                           0x02),
      static_cast<uint8_t>(VolcAsrProtocol::kSerializationJson << 4),
      0x00,
      0x00,
      0x00,
      0x00,
      static_cast<uint8_t>(payload.size()),
  };
  frame.insert(frame.end(), payload.begin(), payload.end());
  const VolcResponse response = VolcAsrProtocol::parseResponse(frame.data(), frame.size());
  TEST_ASSERT_TRUE(response.error.empty());
  TEST_ASSERT_TRUE(response.final);
  TEST_ASSERT_EQUAL_STRING("完成", response.text.c_str());
}

void test_volc_response_json_last_package_marks_completion() {
  const std::string payload = "{\"is_last_package\":true,\"text\":\"最后\"}";
  std::vector<uint8_t> frame = {
      0x11,
      static_cast<uint8_t>((static_cast<uint8_t>(VolcMessageType::FullServerResponse) << 4) |
                           VolcAsrProtocol::kFlagNoSequence),
      static_cast<uint8_t>(VolcAsrProtocol::kSerializationJson << 4),
      0x00,
      0x00,
      0x00,
      0x00,
      static_cast<uint8_t>(payload.size()),
  };
  frame.insert(frame.end(), payload.begin(), payload.end());
  const VolcResponse response = VolcAsrProtocol::parseResponse(frame.data(), frame.size());
  TEST_ASSERT_TRUE(response.final);
  TEST_ASSERT_EQUAL_STRING("最后", response.text.c_str());
}

void test_volc_error_response_parses_code_and_message() {
  const std::string message = "auth failed";
  std::vector<uint8_t> frame = {
      0x11,
      static_cast<uint8_t>(static_cast<uint8_t>(VolcMessageType::ErrorResponse) << 4),
      0x00,
      0x00,
      0x00,
      0x00,
      0x01,
      0x91,
      0x00,
      0x00,
      0x00,
      static_cast<uint8_t>(message.size()),
  };
  frame.insert(frame.end(), message.begin(), message.end());
  const VolcResponse response = VolcAsrProtocol::parseResponse(frame.data(), frame.size());
  TEST_ASSERT_EQUAL_UINT32(401, response.errorCode);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, response.error.find("401"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, response.error.find("auth failed"));
}

void test_volc_truncated_payload_is_rejected_safely() {
  const std::vector<uint8_t> frame = {
      0x11,
      static_cast<uint8_t>(static_cast<uint8_t>(VolcMessageType::FullServerResponse) << 4),
      static_cast<uint8_t>(VolcAsrProtocol::kSerializationJson << 4),
      0x00,
      0xFF,
      0xFF,
      0xFF,
      0xFF,
  };
  const VolcResponse response = VolcAsrProtocol::parseResponse(frame.data(), frame.size());
  TEST_ASSERT_EQUAL_STRING("payload truncated", response.error.c_str());
}

void test_page_model_empty_new_text_and_newlines() {
  PageModel pages(6, 2);
  TEST_ASSERT_EQUAL_STRING("等待语音结果", pages.page().c_str());
  pages.setText("第一行\n第二行\n第三行");
  TEST_ASSERT_EQUAL_UINT32(2, pages.pageCount());
  TEST_ASSERT_EQUAL_STRING("第一行\n第二行", pages.page().c_str());
  pages.nextPage();
  TEST_ASSERT_EQUAL_STRING("第三行", pages.page().c_str());
  pages.setText("新文本");
  TEST_ASSERT_EQUAL_UINT32(0, pages.pageIndex());
  TEST_ASSERT_EQUAL_STRING("新文本", pages.page().c_str());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_long_press_starts_and_release_stops);
  RUN_TEST(test_short_press_is_ignored);
  RUN_TEST(test_next_page_only_in_result_mode);
  RUN_TEST(test_pairing_mode_ignores_record_and_page_buttons);
  RUN_TEST(test_runtime_config_splits_wifi_and_asr_readiness);
  RUN_TEST(test_orientation_controller_defaults_landscape_and_rotates_with_tilt);
  RUN_TEST(test_page_model_rebuilds_when_layout_changes);
  RUN_TEST(test_page_model_wraps_utf8_and_cycles_pages);
  RUN_TEST(test_audio_ring_buffer_tracks_capacity_and_overflow);
  RUN_TEST(test_audio_ring_buffer_write_all_preserves_existing_bytes_on_overflow);
  RUN_TEST(test_pcm_peak_handles_negative_samples);
  RUN_TEST(test_volc_full_request_frame_contains_json_payload);
  RUN_TEST(test_volc_audio_final_frame_uses_negative_sequence);
  RUN_TEST(test_volc_response_parses_text);
  RUN_TEST(test_volc_response_final_flag_marks_completion_without_negative_sequence);
  RUN_TEST(test_volc_response_json_last_package_marks_completion);
  RUN_TEST(test_volc_error_response_parses_code_and_message);
  RUN_TEST(test_volc_truncated_payload_is_rejected_safely);
  RUN_TEST(test_page_model_empty_new_text_and_newlines);
  return UNITY_END();
}
