# ESP32 Dongle-Style Enclosure

Generated STL files:

- `esp32_dongle_case_bottom.stl`
- `esp32_dongle_case_lid.stl`

Approximate outside dimensions:

- 78 mm long
- 42 mm wide
- 18 mm assembled height

Design assumptions:

- Generic ESP32 DevKit-style board mounted lengthwise
- USB cable exits from the short end
- I2S mic sits near the opposite/top end behind five grille slots
- Bottom tray has rails and standoff pads for foam tape, hot glue, or small printed retention tweaks
- Lid uses a shallow internal lip rather than screws

Print notes:

- Print both parts flat on the bed.
- PLA or PETG is fine.
- Use 0.2 mm layer height.
- No supports should be needed.

If the USB port, ESP32 board, or mic module do not line up, edit `generate_dongle_case_stl.py` and rerun it.
