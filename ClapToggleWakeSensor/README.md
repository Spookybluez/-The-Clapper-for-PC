# ESP32 I2S Clap Toggle Sensor

Open `ClapToggleWakeSensor.ino` in Arduino IDE.

## Wiring

| Module | ESP32 |
| --- | --- |
| Mic VDD | 3V3 |
| Mic GND | GND |
| Mic SCK | GPIO26 |
| Mic WS | GPIO25 |
| Mic SD | GPIO33 |
| Mic L/R | GND |
| Built-in LED | GPIO2 |
| PC power trigger output | GPIO14 |

Tie `L/R` to `GND` for the left audio channel. If readings stay near zero after wiring, move `L/R` to `3V3` and change the sketch channel from `I2S_CHANNEL_FMT_ONLY_LEFT` to `I2S_CHANNEL_FMT_ONLY_RIGHT`.

## Arduino IDE Settings

- Board: your ESP32 dev board, often `ESP32 Dev Module`
- Serial Monitor: `115200 baud`

## Tuning

The sketch prints `peak`, `threshold`, `noise`, and `system` about four times per second.

- If normal room noise toggles the system, raise `CLAP_THRESHOLD`.
- If claps do not toggle it, lower `CLAP_THRESHOLD`.
- The sketch calibrates room noise for about two seconds at startup, so stay quiet after reset.

## Speaker Note

Do not connect the bare speaker directly to an ESP32 GPIO. Use a small audio amplifier module, then we can add an audible ON/OFF chirp safely.

## PC Power Button Wiring

Use an optocoupler or a small relay module to imitate the PC case power button.

Preferred optocoupler wiring:

| ESP32 side | Optocoupler input |
| --- | --- |
| GPIO14 | 220 ohm resistor -> optocoupler LED anode |
| GND | optocoupler LED cathode |

| PC side | Optocoupler output |
| --- | --- |
| Motherboard `PWR_SW` pin 1 | optocoupler transistor collector |
| Motherboard `PWR_SW` pin 2 | optocoupler transistor emitter |

If it does not trigger, swap the collector/emitter wires on the motherboard side. Leave the existing case power-button cable connected if you can fit both, or use a splitter.

Do not connect the ESP32 GPIO directly to the motherboard header. Do not switch AC wall power. This sketch sends a short 250 ms button press only.
