/*
  ESP32 USB serial wake/sleep signal bridge

  No WiFi credentials needed. The ESP32 detects a clap and sends a line over
  USB serial:

    WAKE
    SLEEP

  A Windows listener script reads COM10 and performs a dry-run, sleep, or
  shutdown action. This requires Windows to be awake and running the listener.
*/

#include <Arduino.h>
#include <driver/i2s.h>

const i2s_port_t I2S_PORT = I2S_NUM_0;

const int I2S_SCK_PIN = 26;
const int I2S_WS_PIN = 25;
const int I2S_SD_PIN = 33;
const int STATUS_LED_PIN = 2;

const int SAMPLE_RATE = 16000;
const int BUFFER_SAMPLES = 256;

const unsigned long CALIBRATION_MS = 1800;
const unsigned long CLAP_LOCKOUT_MS = 900;
const unsigned long PRINT_INTERVAL_MS = 250;

const int CLAP_THRESHOLD = 1800;

struct AudioStats {
  int peak;
  int average;
};

int32_t samples[BUFFER_SAMPLES];
bool desiredAwake = false;
unsigned long lastClapAt = 0;
unsigned long lastPrintAt = 0;
int noiseFloor = 0;

void setupI2SMic();
AudioStats readI2SStats();
void calibrateNoiseFloor();

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.println();
  Serial.println("ESP32 USB serial wake/sleep bridge starting...");
  setupI2SMic();
  calibrateNoiseFloor();
  Serial.println("READY");
}

void loop() {
  AudioStats audio = readI2SStats();
  unsigned long now = millis();

  if (audio.average >= CLAP_THRESHOLD && now - lastClapAt >= CLAP_LOCKOUT_MS) {
    lastClapAt = now;
    desiredAwake = !desiredAwake;
    digitalWrite(STATUS_LED_PIN, desiredAwake ? HIGH : LOW);

    Serial.print("TRIGGER avg=");
    Serial.print(audio.average);
    Serial.print(" peak=");
    Serial.println(audio.peak);
    Serial.println(desiredAwake ? "WAKE" : "SLEEP");
  }

  if (now - lastPrintAt >= PRINT_INTERVAL_MS) {
    lastPrintAt = now;
    Serial.print("avg=");
    Serial.print(audio.average);
    Serial.print(" peak=");
    Serial.print(audio.peak);
    Serial.print(" threshold=");
    Serial.print(CLAP_THRESHOLD);
    Serial.print(" desired=");
    Serial.println(desiredAwake ? "AWAKE" : "SLEEP");
  }
}

void setupI2SMic() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);
  if (err != ESP_OK) {
    Serial.print("I2S driver install failed: ");
    Serial.println(err);
    while (true) delay(1000);
  }

  err = i2s_set_pin(I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    Serial.print("I2S pin setup failed: ");
    Serial.println(err);
    while (true) delay(1000);
  }

  i2s_zero_dma_buffer(I2S_PORT);
}

void calibrateNoiseFloor() {
  Serial.println("Calibrating room noise. Stay quiet for about 2 seconds...");
  unsigned long startedAt = millis();
  long totalAverage = 0;
  int windows = 0;

  while (millis() - startedAt < CALIBRATION_MS) {
    AudioStats audio = readI2SStats();
    totalAverage += audio.average;
    windows++;
  }

  noiseFloor = windows > 0 ? totalAverage / windows : 0;
  Serial.print("Noise floor=");
  Serial.println(noiseFloor);
}

AudioStats readI2SStats() {
  size_t bytesRead = 0;
  esp_err_t err = i2s_read(I2S_PORT, samples, sizeof(samples), &bytesRead, portMAX_DELAY);
  if (err != ESP_OK || bytesRead == 0) {
    return {0, 0};
  }

  int sampleCount = bytesRead / sizeof(samples[0]);
  int peak = 0;
  int64_t totalMagnitude = 0;

  for (int i = 0; i < sampleCount; i++) {
    int32_t scaled = samples[i] >> 14;
    int magnitude = abs((int)scaled);
    totalMagnitude += magnitude;
    if (magnitude > peak) {
      peak = magnitude;
    }
  }

  int average = sampleCount > 0 ? totalMagnitude / sampleCount : 0;
  return {peak, average};
}
