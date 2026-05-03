# ESP32 PC Wake/Sleep Voice Bridge

An ESP32 prototype that listens through an I2S microphone and sends PC wake/sleep commands over the local network.

The current trigger is a calibrated clap detector. The network-control layer is ready for a future phrase recognizer such as "computer wake up" and "computer go to sleep."

## What It Does

- Reads a 6-pin I2S microphone from an ESP32.
- Uses a clap-style trigger to toggle desired PC state.
- Sends Wake-on-LAN packets to wake the PC.
- Sends an HTTP request to a small Windows listener to put the PC to sleep.
- Includes a dongle-style 3D-printable enclosure.

## Project Layout

| Path | Purpose |
| --- | --- |
| `PcWakeSleepVoiceBridge/` | Main ESP32 WiFi + Wake-on-LAN + sleep-listener firmware |
| `PcWakeSleepVoiceBridge/pc_power_listener.py` | Windows HTTP listener for dry-run and real sleep/shutdown actions |
| `PcWakeSleepVoiceBridge/arduino_secrets.example.h` | Template for local WiFi credentials |
| `ClapToggleWakeSensor/` | Earlier standalone I2S clap-toggle sketch |
| `UsbSerialWakeSleepBridge/` | Experimental USB-serial bridge variant |
| `enclosure/` | STL files and generator for the dongle-style case |

## Hardware

- ESP32 DevKit-style board
- 6-pin I2S microphone, such as INMP441-style modules
- USB power supply or USB cable
- PC with Wake-on-LAN-capable Ethernet adapter

I2S microphone wiring:

| Mic Pin | ESP32 Pin |
| --- | --- |
| `VDD` | `3V3` |
| `GND` | `GND` |
| `SCK` | `GPIO26` |
| `WS` | `GPIO25` |
| `SD` | `GPIO33` |
| `L/R` | `GND` |

## Main Firmware Setup

Copy the secrets template:

```powershell
copy .\PcWakeSleepVoiceBridge\arduino_secrets.example.h .\PcWakeSleepVoiceBridge\arduino_secrets.h
```

Edit `arduino_secrets.h` with your WiFi network:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

The PC target is configured in `PcWakeSleepVoiceBridge.ino`:

```cpp
const byte PC_MAC[6] = {0x34, 0x5A, 0x60, 0x10, 0x9F, 0x36};
const char* PC_SLEEP_URL = "http://192.168.1.191:8787/sleep";
```

Upload with Arduino IDE or Arduino CLI using an ESP32 Dev Module target.

## Windows Listener

Start in dry-run mode first:

```powershell
python .\PcWakeSleepVoiceBridge\pc_power_listener.py --dry-run
```

Dry-run endpoints report success without sleeping the PC.

When ready for the real sleep action:

```powershell
python .\PcWakeSleepVoiceBridge\pc_power_listener.py
```

Available endpoints:

- `GET /health`
- `POST /sleep`
- `POST /shutdown`

## Serial Test Commands

Open Serial Monitor at `115200` baud and send:

```text
WAKE_TEST
SLEEP_TEST
WAKE_IN_30
WAKE_SPAM_120
```

- `WAKE_TEST` sends one Wake-on-LAN packet.
- `SLEEP_TEST` calls the PC listener `/sleep` endpoint.
- `WAKE_IN_30` sends Wake-on-LAN after 30 seconds.
- `WAKE_SPAM_120` sends Wake-on-LAN repeatedly for 2 minutes.

## Wake-on-LAN Requirements

The PC must be configured to wake from the network adapter:

- BIOS/UEFI PCIe wake enabled
- Windows network adapter Power Management allows wake
- `Wake on Magic Packet` enabled in the adapter's advanced settings
- Ethernet connected to the active adapter

## Enclosure

Generated STL files:

- `enclosure/esp32_dongle_case_bottom.stl`
- `enclosure/esp32_dongle_case_lid.stl`

The case is a dongle-style shell with USB exiting the short end and mic grille slots on top.

## Safety Notes

- The firmware stores WiFi credentials in `arduino_secrets.h`, which is ignored by git.
- Use `--dry-run` before allowing the listener to sleep or shut down the PC.
- Wake-on-LAN can wake a sleeping/off PC only when BIOS, Windows, and the network adapter support it.
