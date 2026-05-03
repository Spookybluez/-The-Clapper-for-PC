/*
  ESP32 PC wake/sleep signal bridge

  This sketch keeps the I2S mic clap detector from the prototype, but replaces
  the physical PC power-button output with network signals:

    - "Wake" action: send Wake-on-LAN magic packet to the PC MAC address.
    - "Sleep" action: call a tiny HTTP listener running on the PC.

  Later, replace the clap detector with real speech recognition or an external
  voice-recognition module. The network control pieces can stay the same.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

#include "arduino_secrets.h"

const byte PC_MAC[6] = {0x34, 0x5A, 0x60, 0x10, 0x9F, 0x36};
const char* PC_SLEEP_URL = "http://192.168.1.191:8787/sleep";

const i2s_port_t I2S_PORT = I2S_NUM_0;

const int I2S_SCK_PIN = 26;
const int I2S_WS_PIN = 25;
const int I2S_SD_PIN = 33;
const int STATUS_LED_PIN = 2;

const int SAMPLE_RATE = 16000;
const int BUFFER_SAMPLES = 256;

const unsigned long CALIBRATION_MS = 1800;
const unsigned long CLAP_LOCKOUT_MS = 1200;
const unsigned long PRINT_INTERVAL_MS = 2000;

const int CLAP_THRESHOLD = 1800;
const bool VERBOSE_AUDIO_LOGS = false;

struct AudioStats {
  int peak;
  int average;
};

int32_t samples[BUFFER_SAMPLES];
int clapThreshold = CLAP_THRESHOLD;
int noiseFloor = 0;
bool pcWantedAwake = false;
unsigned long lastClapAt = 0;
unsigned long lastPrintAt = 0;
unsigned long scheduledWakeAt = 0;
unsigned long wakeSpamUntil = 0;
unsigned long nextWakeSpamAt = 0;

void connectWiFi();
void setupI2SMic();
AudioStats readI2SStats();
void calibrateNoiseFloor();
void sendWakeOnLan();
void sendSleepRequest();
void handleSerialCommand();
void printStatus(const AudioStats& audio);

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.println();
  Serial.println("ESP32 PC wake/sleep bridge starting.");
  connectWiFi();
  setupI2SMic();
  calibrateNoiseFloor();

  Serial.println("READY: clap toggles WAKE/SLEEP. Commands: WAKE_TEST, SLEEP_TEST, WAKE_IN_30, WAKE_SPAM_120.");
}

void loop() {
  handleSerialCommand();

  if (scheduledWakeAt != 0 && (long)(millis() - scheduledWakeAt) >= 0) {
    scheduledWakeAt = 0;
    Serial.println("Scheduled wake firing now.");
    sendWakeOnLan();
  }

  if (wakeSpamUntil != 0 && (long)(millis() - wakeSpamUntil) < 0 &&
      (long)(millis() - nextWakeSpamAt) >= 0) {
    nextWakeSpamAt = millis() + 10000UL;
    Serial.println("Repeated wake firing now.");
    sendWakeOnLan();
  } else if (wakeSpamUntil != 0 && (long)(millis() - wakeSpamUntil) >= 0) {
    wakeSpamUntil = 0;
    nextWakeSpamAt = 0;
    Serial.println("Repeated wake window ended.");
  }

  AudioStats audio = readI2SStats();
  unsigned long now = millis();
  bool clapDetected = audio.average >= clapThreshold;

  if (clapDetected && now - lastClapAt >= CLAP_LOCKOUT_MS) {
    lastClapAt = now;
    pcWantedAwake = !pcWantedAwake;
    digitalWrite(STATUS_LED_PIN, pcWantedAwake ? HIGH : LOW);

    if (pcWantedAwake) {
      Serial.print("VOICE_TRIGGER -> WAKE avg=");
      Serial.print(audio.average);
      Serial.print(" peak=");
      Serial.println(audio.peak);
      sendWakeOnLan();
    } else {
      Serial.print("VOICE_TRIGGER -> SLEEP avg=");
      Serial.print(audio.average);
      Serial.print(" peak=");
      Serial.println(audio.peak);
      sendSleepRequest();
    }
  }

  if (VERBOSE_AUDIO_LOGS && now - lastPrintAt >= PRINT_INTERVAL_MS) {
    lastPrintAt = now;
    printStatus(audio);
  }
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();
  command.toUpperCase();

  if (command == "WAKE_TEST") {
    Serial.println("Serial command -> WAKE_TEST");
    sendWakeOnLan();
  } else if (command.startsWith("WAKE_IN_")) {
    int seconds = command.substring(8).toInt();
    if (seconds <= 0) {
      seconds = 30;
    }
    scheduledWakeAt = millis() + (unsigned long)seconds * 1000UL;
    Serial.print("Serial command -> scheduled WAKE in seconds: ");
    Serial.println(seconds);
  } else if (command.startsWith("WAKE_SPAM_")) {
    int seconds = command.substring(10).toInt();
    if (seconds <= 0) {
      seconds = 120;
    }
    wakeSpamUntil = millis() + (unsigned long)seconds * 1000UL;
    nextWakeSpamAt = millis() + 5000UL;
    Serial.print("Serial command -> repeated WAKE window seconds: ");
    Serial.println(seconds);
  } else if (command == "SLEEP_TEST") {
    Serial.println("Serial command -> SLEEP_TEST");
    sendSleepRequest();
  } else if (command.length() > 0) {
    Serial.print("Unknown serial command: ");
    Serial.println(command);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000UL) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed. Commands will retry when needed.");
    return;
  }

  Serial.print("WiFi connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());
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
  Serial.print("Trigger threshold=");
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

void sendWakeOnLan() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wake-on-LAN skipped: WiFi is not connected.");
    return;
  }

  byte packet[102];
  memset(packet, 0xFF, 6);

  for (int i = 1; i <= 16; i++) {
    memcpy(&packet[i * 6], PC_MAC, 6);
  }

  WiFiUDP udp;
  udp.beginPacket(IPAddress(255, 255, 255, 255), 9);
  udp.write(packet, sizeof(packet));
  udp.endPacket();
  udp.stop();

  Serial.println("Wake-on-LAN packet sent.");
}

void sendSleepRequest() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sleep request skipped: WiFi is not connected.");
    return;
  }

  Serial.print("WiFi status: ");
  Serial.println(WiFi.status());
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("POST ");
  Serial.println(PC_SLEEP_URL);

  HTTPClient http;
  http.begin(PC_SLEEP_URL);
  http.addHeader("X-Clapper-Token", PC_POWER_TOKEN);
  int status = http.POST("");
  Serial.print("Sleep request HTTP status: ");
  Serial.println(status);
  if (status < 0) {
    Serial.print("HTTP error: ");
    Serial.println(http.errorToString(status));
  }
  http.end();
}

void printStatus(const AudioStats& audio) {
  Serial.print("avg=");
  Serial.print(audio.average);
  Serial.print(" peak=");
  Serial.print(audio.peak);
  Serial.print(" threshold=");
  Serial.print(clapThreshold);
  Serial.print(" desired=");
  Serial.println(pcWantedAwake ? "AWAKE" : "SLEEP");
}
