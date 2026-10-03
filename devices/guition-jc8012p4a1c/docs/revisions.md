# Hardware revisions of the JC8012P4A1 (V1 / V2 / V3)

Guition does not publish revision notes. Everything here comes from the people who hit the
differences in the field (EspControl, ESPHome) and from what can be measured on a unit.
Fetched/checked 2026-10-02.

## 1. The matrix

| Revision | How EspControl identifies it | What changed | Firmware consequence | Sources |
|---|---|---|---|---|
| **V1** ("original panel") | rear case marking **2627 or lower** | baseline: JD9365 panel driven at DPI 60 MHz, 1 Gbps (ESPHome) or 1.5 Gbps (vendor BSP), V porches 4/8/20 | ESPHome model `JC8012P4A1`; EspControl `devices/guition-esp32-p4-jc8012p4a1`; the vendor BSP values | EspControl docs `docs/screens/jc8012p4a1.md` (local snapshot) and live page; ESPHome `guition.py` |
| **V2** ("new panel") | rear case marking **2628 or higher**; "The back label may not include a V2 marking" | "Some units of this model have a different LCD panel but still use the same JD9365 driver chip" (ESPHome); needs DPI 70 MHz, V back porch 10, and a different init table | ESPHome model `JC8012P4A1-V2` (1.5 Gbps, V fp 20) or EspControl `-v2` (`model: CUSTOM`, 840 Mbps, V fp 30); EspControl manifest entry "Guition JC8012P4A1 new panel (2628+)" | ESPHome `guition.py` comment block; EspControl `devices/guition-esp32-p4-jc8012p4a1-v2/device/device.yaml`, `devices/manifest.json` |
| **V3** ("production silicon") | esptool / ESPHome log reports **ESP32-P4 v3.x** | the SoC itself: ESP32-P4 rev v3.x instead of v1.x; v1.x and v3.x are binary-incompatible (new power rail on pin 54, USB PHY power fix, register changes: 4.3" notes 01-hardware) | a separate build target (`CONFIG_ESP32P4_REV_MIN_300` class builds, no `SELECTS_REV_LESS_V3`); EspControl added `guition-esp32-p4-jc8012p4a1-v3.factory.bin` with an XTAL / display-clock fix for production silicon | EspControl live page; issue jtenniswood/espcontrol#1939 |

EspControl's current rule (live page, quoted): "If ESPHome logs or esptool chip information
reports ESP32-P4 v3.x production silicon, use the V3 firmware", "Confirm production silicon
before using case dates", "Case dates are only a fallback for panels whose chip information
does not identify V3 silicon."

Inconsistencies in the sources, so you do not trust a single number: the same EspControl
page carries a recovery widget labelled "rear case 2622 or lower" / "2624 or higher" next to
the 2627/2628 rule (local snapshot of `docs/screens/jc8012p4a1.md`), and the Aug-2026 snapshot
of the docs knows only two revisions while the live page knows three. The thresholds are
field-derived, not vendor-published.

Reading the code: `2627`, `2628`, `2633`, `2635` look like year-week date codes (2026 week
27, 28, 33, 35). EspControl calls them "case dates". That reading is [inference] but fits
the timeline below.

## 2. The "2633 / 2635 label" story (issue #1939)

Thread on jtenniswood/espcontrol, September 2026 (fetched via the GitHub API, 2026-10-02):

- 11-12 Sep: a user receives a `JC8012P4A1C_I_W_Y` whose packaging label reads `2635` with a
  `...-V3` suffix while the PCB itself is printed `2633`. The browser flashers (EspControl
  and ESPHome web installers) fail with `Serial data stream stopped`. Flashing the V2
  factory image with esptool gives a dark display and no setup hotspot.
- 12-13 Sep: a second user with identical hardware boots it with a custom ESPHome config;
  the maintainer identifies a display clock issue and prepares a branch with an XTAL fix for
  ESP32-P4 production silicon.
- 17 Sep: a third user reports `Chip is ESP32-P4 (revision v3.2)` and that
  `guition-esp32-p4-jc8012p4a1-v3.factory.bin` flashed with esptool at 0x0 brings up
  "display, touch, WiFi setup, HA discovery and the setup page".
- 21 Sep: the web installer still fails for a fourth user; esptool works.

Lessons: the number on the box and the number on the PCB can differ; a `V3` suffix on the
label did mean v3.x silicon in that case; the browser flashers were the first thing to break
on v3 units; and neither the V1 nor the V2 build is the answer for v3 silicon, a V3 build
is. (The owner's brief summarised this thread as "still needed the V2 build"; the thread's
final state is the V3 build. Read the thread before deciding.)

## 3. How to tell what you have

### By silicon (decisive for V3)

```
esptool.py -p COMx chip_id
  Chip is ESP32-P4 (revision v1.3)     <- this unit: V1 or V2 panel, not V3
  Chip is ESP32-P4 (revision v3.x)     <- V3: build for rev 3 silicon, ignore the case date
```

The boot log says the same in `efuse_init: Chip rev: v1.3`. ESPHome's `engineering_sample:
true` and ESP-IDF's `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` are the v1.x switches;
flashing a rev-3 build onto v1.3 is refused by esptool ("requires chip revision in range
v3.1-v3.99", 4.3" troubleshooting section 2).

### By rear label (V1 vs V2)

Read the four-digit code on the rear case and the PCB. 2627 or lower: V1 timing. 2628 or
higher: V2 timing, unless the panel proves otherwise (next item). Photograph both numbers.

### By panel behaviour (the only hard test for V1 vs V2)

The panel ID reads `LCD ID: 93 65 04` on this unit (JD9365, factory log). Whether V2 panels
return a different ID is [unverified]; nobody has published a V2 boot log with that line.
So the test is empirical: with the P4 build settings otherwise known-good (LDO ch3 at 2.5 V,
rev-1.x options), try in this order and stop at the first stable picture:

1. Vendor BSP: 1.5 Gbps, 60 MHz, H 20/20/40, V 4/8/20, 198-entry jd9365 default table.
2. ESPHome `JC8012P4A1`: 1 Gbps, 60 MHz, same porches, its 193-line table.
3. ESPHome `JC8012P4A1-V2`: 1.5 Gbps, 70 MHz, V 4/10/20, its 200-line table.
4. EspControl V2: 840 Mbps, 70 MHz, V 4/10/30, its init_sequence.

A V1 panel lights up on 1 or 2; a V2 panel is reported to need 3 or 4 (EspControl users,
ESPHome maintainers). A panel that stays black on all four is a wiring/LDO/timing-generation
problem, not a revision problem. Also compare the vendor timing against the panel-maker file
in `4-Driver_IC_Data_Sheet/` (V 4/8/20): that file documents the V1 glass (HKC QP101BS01-1
per its name), and the package contains no second init file, so the mirror predates V2
[inference].

## 4. This unit

| Fact | Value | Source |
|---|---|---|
| ESP32-P4 silicon | **v1.3** | esptool, boot log (2026-10-02) |
| So | not V3 | |
| Factory image compile date | 2026-07-03 (ISO week 27) | boot log `app_init` |
| Factory image timing | the V1 vendor BSP values light this panel (the factory UI runs) | boot log `jd9365: version: 1.0.2`, `Display resolution 800x1280`, owner |
| Rear label | **This unit: rear label = ______ (PCB silkscreen = ______)** | to be filled in by the owner |

Interpretation: a week-27 factory build on a unit whose factory firmware uses V1 timing
points to a V1 panel at the very edge of the V1/V2 window; the rear label decides. If it
reads 2628 or higher while V1 timing works, that is a data point worth posting to the
EspControl thread, because it would break their rule.

## 3. This unit

Read on 2026-10-03: the rear label number is in the **V2** range (2628 or higher); the exact
number was not recorded. Silicon rev v1.3, so not V3. `bedside_clock` was built with
`CONFIG_BSP_JD9365_PANEL_V2` (DPI 70 MHz, vsync 4/10/20, ESPHome's V2 init table) and the
controller answered with `LCD ID: 93 65 04` on the first attempt. The V1 table was not tried
on this unit, so V1 vs V2 has not been shown to matter here; it is the documented setting.
