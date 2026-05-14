#include "audio.h"

#include <ESP_I2S.h>
#include <Wire.h>
#include <math.h>

#include "esp_err.h"
#include "pin_config.h"
#include "official/es8311.h"

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr int kVoiceVolume = 85;
constexpr es8311_mic_gain_t kMicGain = static_cast<es8311_mic_gain_t>(3);
constexpr size_t kFramesPerChunk = 256;
constexpr size_t kChannelCount = 2;
constexpr float kBeepFrequencyHz = 880.0f;
constexpr float kBeepDurationSeconds = 0.18f;
constexpr float kBeepAmplitude = 0.22f;

I2SClass i2s;

esp_err_t codec_init() {
  es8311_handle_t codec = es8311_create(0, ES8311_ADDRRES_0);
  if (codec == nullptr) {
    return ESP_FAIL;
  }

  const es8311_clock_config_t clockConfig = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = static_cast<int>(kSampleRate * 256),
      .sample_frequency = static_cast<int>(kSampleRate),
  };

  esp_err_t err = es8311_init(
      codec,
      &clockConfig,
      ES8311_RESOLUTION_16,
      ES8311_RESOLUTION_16);
  if (err != ESP_OK) {
    return err;
  }

  err = es8311_sample_frequency_config(
      codec,
      clockConfig.mclk_frequency,
      clockConfig.sample_frequency);
  if (err != ESP_OK) {
    return err;
  }

  err = es8311_microphone_config(codec, false);
  if (err != ESP_OK) {
    return err;
  }

  err = es8311_voice_volume_set(codec, kVoiceVolume, nullptr);
  if (err != ESP_OK) {
    return err;
  }

  return es8311_microphone_gain_set(codec, kMicGain);
}
}  // namespace

bool audio_play_startup_beep() {
  pinMode(AUDIO_PA, OUTPUT);
  digitalWrite(AUDIO_PA, HIGH);

  i2s.setPins(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO, I2S_MCK_IO);
  if (!i2s.begin(
          I2S_MODE_STD,
          kSampleRate,
          I2S_DATA_BIT_WIDTH_16BIT,
          I2S_SLOT_MODE_STEREO,
          I2S_STD_SLOT_BOTH)) {
    Serial.println("Audio init failed: I2S begin failed");
    return false;
  }

  Wire.begin(IIC_SDA, IIC_SCL);

  const esp_err_t codecErr = codec_init();
  if (codecErr != ESP_OK) {
    Serial.printf("Audio init failed: ES8311 error %d\n", static_cast<int>(codecErr));
    return false;
  }

  const size_t totalFrames = static_cast<size_t>(kSampleRate * kBeepDurationSeconds);
  int16_t buffer[kFramesPerChunk * kChannelCount];

  for (size_t frameBase = 0; frameBase < totalFrames; frameBase += kFramesPerChunk) {
    const size_t frameCount = min(kFramesPerChunk, totalFrames - frameBase);

    for (size_t i = 0; i < frameCount; ++i) {
      const float t = static_cast<float>(frameBase + i) / static_cast<float>(kSampleRate);
      const float envelope = 1.0f - (static_cast<float>(frameBase + i) / static_cast<float>(totalFrames));
      const float value = sinf(2.0f * PI * kBeepFrequencyHz * t) * kBeepAmplitude * envelope;
      const int16_t sample = static_cast<int16_t>(value * 32767.0f);

      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }

    const size_t bytesToWrite = frameCount * kChannelCount * sizeof(int16_t);
    const size_t bytesWritten =
        i2s.write(reinterpret_cast<const uint8_t *>(buffer), bytesToWrite);
    if (bytesWritten != bytesToWrite) {
      Serial.println("Audio beep failed: incomplete I2S write");
      return false;
    }
  }

  Serial.println("Startup beep played");
  return true;
}
