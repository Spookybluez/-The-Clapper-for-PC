# ESP32 PC Wake/Sleep Bridge

This version does not touch the motherboard `PWR_SW` header. The ESP32 sends signals to the PC over the network.

## What Works When

| PC state | Method | Requirement |
| --- | --- | --- |
| PC off/sleeping | Wake-on-LAN magic packet | Wake-on-LAN enabled in BIOS/UEFI and Windows/NIC settings |
| PC awake | HTTP request to listener | `pc_power_listener.py` running on the PC |

If the PC is fully off and Wake-on-LAN is disabled, the ESP32 cannot turn it on over USB/serial because the operating system is not running.

## ESP32 Setup

Create `arduino_secrets.h` from the example:

```powershell
copy .\arduino_secrets.example.h .\arduino_secrets.h
```

Then edit `arduino_secrets.h`:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* PC_POWER_TOKEN = "MATCH_THE_TOKEN_IN_pc_listener_token.txt";
```

The PC target is currently configured in `PcWakeSleepVoiceBridge.ino`:

```cpp
const byte PC_MAC[6] = {0x34, 0x5A, 0x60, 0x10, 0x9F, 0x36};
const char* PC_SLEEP_URL = "http://192.168.1.191:8787/sleep";
```

The I2S mic wiring stays:

| Mic | ESP32 |
| --- | --- |
| VDD | 3V3 |
| GND | GND |
| SCK | GPIO26 |
| WS | GPIO25 |
| SD | GPIO33 |
| L/R | GND |

## Windows Listener

Create a local listener token file on the PC:

```powershell
[guid]::NewGuid().ToString('N') | Set-Content .\pc_listener_token.txt
```

Use the same token value for `PC_POWER_TOKEN` in `arduino_secrets.h`. The ESP32 sends it in the `X-Clapper-Token` header, and the listener rejects power commands without it.

Dry-run test:

```powershell
python .\pc_power_listener.py --dry-run --token (Get-Content .\pc_listener_token.txt)
```

Real sleep mode:

```powershell
python .\pc_power_listener.py --token (Get-Content .\pc_listener_token.txt)
```

Dry-run is strongly recommended first. Without `--dry-run`, `POST /sleep` will put the PC to sleep.

## ESP32 Serial Test Commands

Open Serial Monitor at `115200` baud and send one of:

```text
WAKE_TEST
SLEEP_TEST
WAKE_IN_30
WAKE_SPAM_120
```

- `WAKE_TEST` sends one Wake-on-LAN packet.
- `SLEEP_TEST` calls the PC listener `/sleep` endpoint.
- `WAKE_IN_30` sends Wake-on-LAN after 30 seconds.
- `WAKE_SPAM_120` sends Wake-on-LAN repeatedly for 2 minutes, useful for sleep/wake testing.

Set `VERBOSE_AUDIO_LOGS = true` in the sketch if you need raw mic tuning output again.

## Voice Recognition

The included ESP32 sketch still uses the clap detector as a placeholder trigger. Exact phrase recognition like "computer wake up" and "computer go to sleep" should be added with an ESP32-S3/ESP-SR flow or a dedicated offline voice-recognition module. The network wake/sleep actions in this folder can remain the output layer.
