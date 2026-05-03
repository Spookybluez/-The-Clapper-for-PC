/*
  ESP32 I2S mic clap toggle sensor

  Wiring for a 6-pin I2S mic such as INMP441:
    Mic VDD       -> ESP32 3V3
    Mic GND       -> ESP32 GND
    Mic SCK/BCLK  -> ESP32 GPIO26
    Mic WS/LRCL   -> ESP32 GPIO25
    Mic SD/DOUT   -> ESP32 GPIO33
    Mic L/R       -> ESP32 GND

  Clap once to toggle system ON/OFF. The built-in LED on GPIO2 shows state.
  GPIO14 pulses an optocoupler/relay that is wired across the PC motherboard
  PWR_SW header pins, imitating a momentary front-panel power button press.
*/

#include <Arduino.h>
#include <driver/i2s.h>

const i2s_port_t I2S_PORT = I2S_NUM_0;

const int I2S_SCK_PIN = 26;
const int I2S_WS_PIN = 25;
const int I2S_SD_PIN = 33;
const int STATUS_LED_PIN = 2;
const int PC_POWER_TRIGGER_PIN = 14;

const int SAMPLE_RATE = 16000;
const int BUFFER_SAMPLES = 256;

const unsigned long CALIBRATION_MS = 1800;
const unsigned long CLAP_LOCKOUT_MS = 900;
const unsigned long PRINT_INTERVAL_MS = 250;
const unsigned long PC_POWER_PULSE_MS = 250;

// These values are scaled average-energy units. If normal speech triggers it,
// raise CLAP_THRESHOLD. If claps do not trigger it, lower CLAP_THRESHOLD.
const int CLAP_THRESHOLD = 1800;

int clapThreshold = CLAP_THRESHOLD;
int noiseFloor = 0;
bool systemOn = false;
unsigned long lastClapAt = 0;
unsigned long lastPrintAt = 0;

struct AudioStats {
  int peak;
  int average;
};

int32_t samples[BUFFER_SAMPLES];

void setupI2SMic();
AudioStats readI2SStats();
void calibrateNoiseFloor();
void pulsePcPowerButton();

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(PC_POWER_TRIGGER_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(PC_POWER_TRIGGER_PIN, LOW);

  Serial.println();
  Serial.println("ESP32 I2S clap toggle starting... channel=RIGHT");
  setupI2SMic();
  calibrateNoiseFloor();

  Serial.println("Clap toggle ready.");
  Serial.println("Open Serial Monitor at 115200 baud. Clap once to toggle ON/OFF.");
}

void loop() {
  AudioStats audio = readI2SStats();
  unsigned long now = millis();
  bool clapDetected = audio.average >= clapThreshold;

  if (clapDetected && now - lastClapAt >= CLAP_LOCKOUT_MS) {
    lastClapAt = now;
    systemOn = !systemOn;
    digitalWrite(STATUS_LED_PIN, systemOn ? HIGH : LOW);
    pulsePcPowerButton();

    Serial.print("CLAP avg=");
    Serial.print(audio.average);
    Serial.print(" peak=");
    Serial.print(audio.peak);
    Serial.print(" -> system ");
    Serial.println(systemOn ? "ON" : "OFF");
  }

  if (now - lastPrintAt >= PRINT_INTERVAL_MS) {
    lastPrintAt = now;
    Serial.print("avg=");
    Serial.print(audio.average);
    Serial.print(" peak=");
    Serial.print(audio.peak);
    Serial.print(" threshold=");
    Serial.print(clapThreshold);
    Serial.print(" noise=");
    Serial.print(noiseFloor);
    Serial.print(" system=");
    Serial.println(systemOn ? "ON" : "OFF");
  }
}

void pulsePcPowerButton() {
  digitalWrite(PC_POWER_TRIGGER_PIN, HIGH);
  delay(PC_POWER_PULSE_MS);
  digitalWrite(PC_POWER_TRIGGER_PIN, LOW);
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
    while (true) {
      delay(1000);
    }
  }

  err = i2s_set_pin(I2S_PORT, &pinConfig);
  if (err != ESP_OK) {
    Serial.print("I2S pin setup failed: ");
    Serial.println(err);
    while (true) {
      delay(1000);
    }
  }

  i2s_zero_dma_buffer(I2S_PORT);
}

void calibrateNoiseFloor() {
  Serial.println("Calibrating room noise. Stay quiet for about 2 seconds...");

  unsigned long startedAt = millis();
  long totalPeak = 0;
  int windows = 0;
  int maxPeak = 0;

  while (millis() - startedAt < CALIBRATION_MS) {
    AudioStats audio = readI2SStats();
    totalPeak += audio.average;
    windows++;
    if (audio.average > maxPeak) {
      maxPeak = audio.average;
    }
  }

  noiseFloor = windows > 0 ? totalPeak / windows : 0;
  clapThreshold = CLAP_THRESHOLD;

  Serial.print("Noise floor=");
  Serial.println(noiseFloor);
  Serial.print("Max startup noise=");
  Serial.println(maxPeak);
  Serial.print("Clap threshold=");
  Serial.println(clapThreshold);
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
    // INMP441-style mics usually provide 24-bit audio left-aligned in 32 bits.
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
