# JC4880P443C External Interface Reference

Specs for the eight external connectors on the Guition JC4880P443C (ESP32-P4 + ESP32-C6) board.
Sources: vendor schematic (`../vendor/package/5-Schematic/`, local only), the third-party pinmap and board-constants notes `02_pinmap_and_connectors.md` and `09_board_constants.md` from [seobaeksol/JC4880P443C_I_W](https://github.com/seobaeksol/JC4880P443C_I_W) (local copy `../community/seobaeksol-JC4880P443C_I_W/`; community notes without a licence, not vendor documentation), and verified example firmware. All logic-level I/O on this board is **3.3 V — nothing is 5 V tolerant**.

---

## a) BAT — MX 1.25 2P Lithium Battery (CN4)

| Spec | Detail |
|---|---|
| Connector | MX 1.25 mm pitch, 2-pin (BAT+ / BAT−) |
| Battery type | Single-cell (1S) Li-ion or LiPo, 3.7 V nominal, 4.2 V full-charge termination. No protection circuit on board — use a cell with a built-in PCM |
| Charging | IP5306 power-bank SoC charges the cell from USB 5 V (chip class supports up to ~2.1 A charge; actual rate depends on the USB supply) and boosts the cell to the 5 V `VOUT-BAT` system rail when USB is absent |
| Voltage | Cell: 3.0–4.2 V. Charge input: 5 V from either USB-C port or the UART0 connector's VIN pin |
| Fuel gauge | 68 kΩ / 100 kΩ divider (×0.595) from BAT+ → **GPIO53** (ADC2_CH4, 12 dB attenuation). Vendor example maps 2250–2500 mV at the pin (≈3.78–4.2 V cell) to 0–100 % |
| Protocol | None — raw power pins plus the ADC sense line |
| Limitations | Reversed leads destroy the board (no polarity protection on the connector); IP5306 is a power-bank chip, so very light loads can trigger its auto-shutdown; charge-status LEDs are driven by the IP5306, not firmware; board runs fine with no battery fitted |
| **Good uses for this interface** | Portable/battery-backed operation of the whole board; UPS-style graceful shutdown using the GPIO53 voltage reading; field data loggers and wall-mounted panels that must ride through power cuts |

## b) USB1 — Full-Speed USB Type-C (schematic designator USB2)

| Spec | Detail |
|---|---|
| Connector | USB Type-C receptacle, USB 2.0 full speed |
| Wiring | D+/D− go **directly** to the ESP32-P4 `USB1P1_P/N` pins — this is the P4's built-in **USB-Serial/JTAG** peripheral. (The older claim of a CH340C bridge in the bigbag examples' `docs/device-specs.md` is wrong for this board — there is no bridge chip on the schematic.) Enumerates on this machine as **COM3** |
| Speed | 12 Mbit/s (USB 1.1 full speed) |
| Voltage | VBUS 5 V in (feeds `USB5V_IN`); 5.1 kΩ CC pull-downs → sink/UFP only, no USB-PD, no host role |
| Protocol | USB CDC-ACM serial + JTAG debug, plus ROM download mode for flashing |
| Limitations | Cannot act as USB host; console dies if firmware reconfigures/crashes the USB-Serial/JTAG peripheral; not a high-throughput data path |
| **Good uses for this interface** | Day-to-day development: flashing (`idf.py flash`), serial monitor/logs, OpenOCD JTAG debugging, and powering the board from a PC — all over one cable |

## c) USB3 — High-Speed USB Type-C

| Spec | Detail |
|---|---|
| Connector | USB Type-C receptacle, USB 2.0 high speed |
| Wiring | D+/D− to ESP32-P4 native USB OTG PHY (`ESP_USB_P/N`) |
| Speed | 480 Mbit/s (USB 2.0 high speed) |
| Voltage | VBUS 5 V in (also feeds `USB5V_IN` — either port powers the board); 5.1 kΩ CC pull-downs, no USB-PD negotiation |
| Protocol | USB 2.0 OTG — device or host role, driven by TinyUSB / esp_usb stacks |
| Limitations | Host role needs the firmware to supply/manage VBUS and the CC wiring is sink-oriented, so host mode typically needs a powered adapter; only one USB role at a time; no SuperSpeed |
| **Good uses for this interface** | High-bandwidth USB device classes: mass storage (expose the TF card), UVC webcam streaming from the MIPI-CSI camera, audio class, HID; USB host for flash drives or peripherals; the factory "LoadMode USB" burn port |

## d) JP1 — 2×13 2.54 mm Expansion Header

| Spec | Detail |
|---|---|
| Connector | 26-pin (2×13) male header, 2.54 mm pitch |
| Power pins | 3V3 ×2 (pins 1–2), GND ×3 (pins 3, 4, 14), 3V3 again (pin 16 `VCC3V3`); 5 V is **not** on this header |
| P4 GPIOs | 52, 51, 50, 49, 35, 34, 32, 28 (left column) and 33, 31, 30, 29 (right column) |
| I2C | Pins 21/23: `ES_I2C_SDA`/`ES_I2C_SCL` = **GPIO7/GPIO8**, the board-wide shared I2C bus |
| C6 pins | `C6_U0RXD`, `C6_U0TXD`, `C6_IO9`, `C6_CHIP_PU` — the ESP32-C6's UART0 + boot/enable, for co-processor recovery/reflash |
| Voltage | 3.3 V logic, not 5 V tolerant |
| Speed | GPIO-limited: SPI to tens of MHz, PWM/LEDC/RMT/MCPWM, TWAI (CAN) with an external transceiver, ADC on ADC-capable pins |
| Limitations | **GPIO34/35 are strapping pins** — external circuits must not pull them during reset (GPIO35 is also the BOOT button). GPIO28–31, 34–35, 49–52 double as RMII/Ethernet nets, so heavy use here forfeits future LAN expansion. The C6 pins are not general-purpose I/O. The I2C pins already carry touch (0x5D), codec (0x18) and camera (0x36) |
| **Good uses for this interface** | Prototyping extra peripherals: SPI displays/sensors, relays, rotary encoders, CAN (via transceiver), servo/LED PWM; last-resort ESP32-C6 firmware recovery via its exposed UART0 |

## e) CN2 — MX 1.25 4P UART0

| Spec | Detail |
|---|---|
| Connector | MX 1.25 mm pitch, 4-pin: **1 = VIN (5 V in), 2 = TX0 (GPIO37), 3 = RX0 (GPIO38), 4 = GND** |
| Protocol | UART0 — the ESP32-P4 ROM bootloader and default console port. 115200 8N1 by default |
| Voltage | Logic 3.3 V. VIN is a genuine 5 V **power input** (routes through an AO3401 P-FET into `USB5V_IN`, TVS-protected) — the board can run entirely from this connector |
| Speed | Standard UART rates; console at 115200, flashing commonly up to 921600 |
| Limitations | GPIO37/38 sit in the strapping-pin range — keep the line idle-high during reset; anything printed by the console comes out here, so a connected device must tolerate boot logs; 3.3 V logic only (use a 3.3 V USB-UART adapter, never 5 V TTL) |
| **Good uses for this interface** | Headless installs: one cable for power + console; automated flash/test rigs driving the ROM bootloader; capturing early boot/crash logs when USB isn't available |

## f) J5 — MX 1.25 4P UART (TTL, UART1)

| Spec | Detail |
|---|---|
| Connector | MX 1.25 mm pitch, 4-pin: **1 = 5 V (`USB5V_IN`), 2 = TX1, 3 = RX1, 4 = GND** |
| Wiring | **UART1 on GPIO26 (TX) / GPIO27 (RX) — the exact same UART and pins as the RS-485 port.** The schematic links these nets to the MAX485 through 0 Ω resistors |
| Protocol | Plain 3.3 V TTL UART, any standard baud (examples use 115200) |
| Voltage | 3.3 V logic; pin 1 supplies 5 V **out** to your peripheral (only live when USB/VIN power is present) |
| Speed | Standard UART rates (ESP32-P4 UART supports up to 5 Mbaud, though wiring quality limits practical rates) |
| Limitations | **Mutually exclusive with RS-485** — every byte on UART1 also drives the RS-485 transceiver and vice-versa; GPIO26 is also the xiaozhi firmware's "builtin LED" pin, so never blink it while UART1 is in use; not 5 V tolerant despite the 5 V power pin |
| **Good uses for this interface** | Attaching one 3.3 V TTL serial peripheral — GPS module, fingerprint reader, LoRa/modem module, a second MCU — with power on the same cable, when RS-485 is not being used |

## g) J4 — MX 1.25 4P RS-485

| Spec | Detail |
|---|---|
| Connector | MX 1.25 mm pitch, 4-pin: **1 = 5 V, 2 = A0, 3 = B0, 4 = GND** |
| Transceiver | MAX485 half-duplex, with a 74LVC1G132 + S8550 circuit providing **automatic TX/RX direction switching** — firmware never toggles DE/RE |
| Firmware | UART1, **TX = GPIO26, RX = GPIO27**, `UART_MODE_RS485_HALF_DUPLEX`, RTS = `UART_PIN_NO_CHANGE`, 115200 in the vendor examples (`uart_echo_rs485`) |
| Protocol | RS-485 differential (TIA-485), half duplex; carry Modbus RTU or any UART framing over it |
| Voltage | Bus differential ±1.5–5 V; transceiver runs from `USB5V_IN`. A/B lines protected by SMAJ6.5CA TVS diodes and 0.1 A fuses |
| Speed | MAX485 class: up to 2.5 Mbit/s at short range; ~1200 m at ≤ 100 kbit/s |
| Limitations | Half duplex only; **shares UART1/GPIO26-27 with the J5 TTL UART** (use one or the other); transceiver is powered from the USB 5 V rail, so expect RS-485 to be dead on battery-only power; no on-board 120 Ω termination jumper noted — add external termination on long buses; GPIO26 conflicts with the xiaozhi LED assignment |
| **Good uses for this interface** | Modbus RTU master/slave; long-run, noise-immune links to industrial sensors, VFDs, energy meters; multi-drop networks of up to 32 unit loads across a building |

## h) CN3 — HS 1.0 I2C Expansion

| Spec | Detail |
|---|---|
| Connector | 1.0 mm pitch (vendor lists it as "HS1.0 2P", but the schematic shows **4 signal nets**): **1 = GND, 2 = ESP_3V3, 3 = SCL (GPIO8), 4 = SDA (GPIO7)** |
| Protocol | I2C — this is the board's single **shared bus**, already carrying GT911 touch (0x5D), ES8311 codec (0x18) and OV02C10 camera SCCB (0x36) |
| Voltage | 3.3 V logic and 3.3 V power out; not 5 V tolerant |
| Speed | 100 kHz standard / 400 kHz fast mode. The on-board devices are configured at 400 kHz — external devices must cope with the bus speed the firmware picks |
| Limitations | Address collisions with 0x18/0x36/0x5D will break touch, audio or camera; bus pull-ups are already on board (don't stack strong external ones); I2C is not a cable bus — keep leads to ~10–20 cm; in firmware, **reuse the existing bus handle** (`bsp_i2c_get_handle()`), never create a second driver on GPIO7/8 |
| **Good uses for this interface** | Plug-in 3.3 V I2C sensors: temperature/humidity (SHT4x), IMU, ambient light, RTC, small OLED, IO expanders — anything low-speed that fits the free address space |

---

## Cross-interface conflicts at a glance

| Resource | Shared by | Rule |
|---|---|---|
| UART1 (GPIO26/27) | J5 TTL UART, J4 RS-485, xiaozhi LED (GPIO26) | Pick one consumer; never blink GPIO26 while UART1 is active |
| I2C bus (GPIO7/8) | Touch 0x5D, codec 0x18, camera 0x36, CN3, JP1 pins 21/23 | One driver, one handle; new devices need free addresses |
| GPIO34/35, 28–31, 49–52 | JP1 header ↔ RMII/Ethernet nets; GPIO34/35 also strapping | Keep floating at reset; using them forfeits LAN expansion |
| `USB5V_IN` 5 V rail | Both USB-C ports, CN2 VIN, J5/J4 5 V pins, MAX485 power | Any of the three inputs powers everything; RS-485 needs it |
| GPIO53 (ADC2_CH4) | Battery voltage divider | Reserved for battery sensing |

**Sources:** `../vendor/package/5-Schematic/` (sheets 1_PWR, 4_USB&IO, 5_485), seobaeksol notes `02_pinmap_and_connectors.md` and `09_board_constants.md` (`../community/seobaeksol-JC4880P443C_I_W/`), bigbag `docs/device-specs.md` (`../community/bigbag-JC4880P443C-examples/trimmed/docs/device-specs.md`), the vendor example `uart_echo_rs485` (`../vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/`) and the bigbag example `10_battery_adc`.
