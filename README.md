# payload-datalogger

Wirelessly configurable thermocouple data logging PCB implementing four MCP96RL00T on-board thermocouple to I2C converter ICs, an ESP32-C3 MCU, and flash storage.

## Screenshots

![3d](screenshots/3d.png)
![pcb](screenshots/pcb.png)
![schematic root](screenshots/sch_root.png)
![schematic thermocouple](screenshots/sch_tc.png)

## BOM

See [this spreadsheet].(https://docs.google.com/spreadsheets/d/12Mr7GmqM8tnob1ocJkmMWgOL1p1aouqN9iYZXRkocOg/edit?usp=sharing)

**BOM cost:** $65 (on DigiKey, for two boards with attrition)
Does not include cables.

Additionally, batteries should be purchased elsewhere such as the suggested Amazon links to remain economical.

## Firmware

Firmware source code is located in the `src/payload-datalogger` directory.
Source code, for ease of modification by all team members, relies on the Arduino hardware abstraction layer (HAL).
The PlatformIO IDE and toolchain is required to build and debug the firmware, meaning that the minimum prerequisites for payload datalogger firmware development are:
- [Visual Studio Code](https://code.visualstudio.com/download)
- [PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)