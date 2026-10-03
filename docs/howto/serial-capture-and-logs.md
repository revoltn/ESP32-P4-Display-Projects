# Serial capture, device logs and core dumps

How to get evidence out of the board, from the interactive monitor to a symbolised backtrace of
a hang. The clock app also keeps its own logs that you can read over WiFi, which matters
because its USB host port and the PC port cannot always be connected at the same time.

## 1. idf.py monitor (interactive)

```powershell
idf.py -p COM3 monitor                 # default build
idf.py -B build_mww -p COM3 monitor    # the microWakeWord variant: same -B as the build
```

| Key | Action |
|---|---|
| Ctrl-] | Exit |
| Ctrl-T Ctrl-H | Help: all shortcuts |
| Ctrl-T Ctrl-R | Reset the board (toggles RTS, you get the boot log) |
| Ctrl-T Ctrl-A | `app-flash` and resume |
| Ctrl-T Ctrl-F | `flash` and resume |
| Ctrl-T Ctrl-L | Toggle logging to a file |

The monitor decodes panic backtraces using the ELF in the build directory, which is why the
`-B` must match. It also owns the port: nothing else can open COM3 while it runs.

## 2. readcom.py (scripted capture)

`src/bedside_clock/tools/readcom.py` is a 30-line pyserial reader for captures that have to
run unattended, in the background, or from a script. It needs pyserial, which the IDF Python
environment has, so run it from an exported shell (where `python` is the IDF interpreter):

```powershell
cd src\bedside_clock
python tools\readcom.py COM3 15 reset        # 15 s, starting with a reset so the boot log is included
python tools\readcom.py COM3 100 > boot.log  # 100 s to a file
```

```bash
python tools/readcom.py /dev/ttyACM0 15 reset
```

Arguments: port, seconds, optional `reset` (pulses RTS before capturing). Rules:

- Close `idf.py monitor` first; only one process may hold the port.
- For a capture while you drive the device over the HTTP API, start it in the background
  writing to a file, then issue the API calls:

```powershell
$cap = Start-Process -FilePath python -ArgumentList "tools\readcom.py COM3 120" `
       -RedirectStandardOutput capture.log -NoNewWindow -PassThru
# ... Invoke-RestMethod calls here ...
$cap.WaitForExit()
Select-String -Path capture.log -Pattern '0x7180|Guru|Navidrome|song_cache|ui_watch'
```

The reset pulse also resets the clock; if you need the log of a state the device is already in,
leave `reset` off.

## 3. Logs the clock keeps for you (no cable needed)

`bedside_clock` assumes nobody is watching the serial port when things go wrong at 06:30. It
therefore keeps three things readable from the device and over the HTTP API
([http-api.md](http-api.md); all calls need the `X-Api-Key` header):

| Source | What it holds | Where to read it |
|---|---|---|
| **ui_log** (RAM, 16 KB in PSRAM) | The app's own one-line messages since boot: WiFi, server probes, stream failures with classified cause, settings repairs | Settings > Specifications on the device; `GET /api/v1/log` (text/plain) |
| **evlog** event journal | 40 one-line events that **survive reboots and crashes**: boots with reset reason and crash count, time sync, WiFi edges, every alarm decision, UI stalls, download failures. Held in a `__NOINIT_ATTR` ring in internal RAM (survives everything but power-on) and mirrored to NVS about 2 s after each append (a power cut loses at most the last seconds). The newest 8 lines are echoed into ui_log at boot | Settings > Specifications; `GET /api/v1/events` (JSON) |
| **status** | Heap headroom that predicts the next failure: `heap.internal_free`, `internal_largest`, `dma_free`, `dma_largest` (I2S and SDIO descriptors need DMA-capable internal RAM; below about 12 KB largest, the next sample-rate change can fail), `psram_free`; plus WiFi, playback, alarm and USB audio state | `GET /api/v1/status` |
| **USB survey** | What a plugged-in USB speaker advertises and which format actually started | `GET /api/v1/usb` (text/plain) |

Journal lines look like `09-19 06:30:02 alarm: ringing 06:30 (mode 1)`, or `+000012s ...` when
the entry predates a valid clock. Every `evlog()` call also goes to the serial log at INFO.

## 4. Crashes: reset reasons, the crash-loop breaker, core dumps

At boot the clock logs the reset reason (`Reset reason: power-on` or
`Abnormal reset (task watchdog), consecutive crash count 2`). Panics, interrupt and task
watchdogs, and brownouts count as abnormal; the counter clears after 30 s of stable uptime.
At three in a row the device boots in **safe mode**: voice and camera stay off and the HTTP
API does not auto-start until it is re-enabled in Settings. The status line reads
`Recovered from crash (N)`.

The task watchdog is set to 60 s and configured to panic (`CONFIG_ESP_TASK_WDT_PANIC=y`), so
a trip becomes an `ESP_RST_TASK_WDT` reboot that the breaker can count instead of a silent
log line.

### Core dumps

`bedside_clock` writes an ELF core dump of every panic to the 128 KB `coredump` partition
(`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`). At the next boot it says so:

```
W app_main: core dump from an earlier crash is stored in flash
```

and ui_log shows `Crash dump stored in flash - read it with: idf.py coredump-info -p COM3`.
The dump survives until the next panic overwrites it, so pull it promptly:

```powershell
idf.py -p COM3 coredump-info                  # summary + backtraces of all tasks
idf.py -B build_mww -p COM3 coredump-info     # if the crashing build was the mww variant
idf.py -p COM3 coredump-debug                 # opens gdb on the dump
```

The ELF in the build directory must be the one that was running (compare `app_init: Compile
time` in the boot log with your last build). If you have rebuilt since, flash and reproduce,
or keep a copy of `build/bedside_v01.elf` next to every image you flash.

The sample and the bench have no coredump partition; their panics print a backtrace that
`idf.py monitor` decodes live.

## 5. A frozen UI: ui_watch

A dead screen with music still playing is not a crash, so no core dump would be written.
`ui_watch.c` runs a 250 ms heartbeat on the LVGL task and checks it from an independent
`esp_timer` every 2 s. When the heartbeat stops it logs the LVGL task's FreeRTOS state
(`state=B` blocked on something it should never wait for, `R` running or starved) and asks
the worker to print `vTaskGetRunTimeStats()` so the task eating the CPU is named. Touch events
are traced at INFO as well, which separates "LVGL is dead" from "the touch controller is
dead". A stall that outlives 30 s is treated as unrecoverable: the app aborts on purpose, the
core dump records every task's backtrace, and the clock reboots into a working state. The
2026-09-06 freeze (PPA rotate waiting forever on rev v1.3 silicon) was diagnosed this way.

## 6. Live backtrace over USB-JTAG without rebooting

The same USB cable that gives you COM3 also exposes the P4's built-in JTAG ("USB JTAG/serial
debug unit", WinUSB, no extra hardware). For a hang this is the definitive tool: every task's
symbolised backtrace while the firmware is still in the hung state. These are the author's
bench notes (2026-09-06); the recipe is not yet part of the board documents, so expect to
adapt paths.

```powershell
# terminal 1 (exported IDF shell)
openocd -f board/esp32p4-builtin.cfg

# terminal 2
riscv32-esp-elf-gdb -batch -ex "target extended-remote :3333" `
    -ex "info threads" -ex "thread apply all bt" -ex detach build\bedside_v01.elf
```

Gotchas seen on Windows:

- The first gdb connect may fail with `Remote replied unexpectedly to 'vMustReplyEmpty'`
  while OpenOCD logs `Memory protection is enabled. Reset target`; the firmware keeps running,
  just reconnect.
- After gdb detaches the cores may stay halted (serial goes silent, the API times out). Send
  `resume` then `shutdown` on OpenOCD's telnet port 4444, or hard-reset with
  `esptool.py --port COM3 --before default_reset --after hard_reset read_mac`.
- Symbols must match the flashed binary; snapshot the ELF before anyone rebuilds.

## 7. Reading the esp_hosted handshake block

Whatever the wireless symptom, read this block in the boot log first:

```
I transport: Identified slave [esp32c6]
I transport: capabilities: 0xd
I transport: Features supported are:
I transport:     * WLAN
I transport:       - HCI over SDIO
I transport:       - BLE only
W transport: Version mismatch: Host [2.11.0] < Co-proc [2.12.0] ==> Upgrade host
```

It states the slave chip, its esp_hosted version relative to the host component, whether BT
over SDIO is compiled in, and that Bluetooth Classic is absent. The clock additionally logs
`C6 co-processor: esp_hosted X.Y.Z` when WiFi starts. A version mismatch is tolerated by WiFi
and fatal for BLE; see [troubleshooting.md](../troubleshooting.md).

## 8. What to attach to a bug report

A boot log from reset (`readcom.py COM3 20 reset`), `GET /api/v1/events`, `GET /api/v1/log`,
and `coredump-info` output if the device said a dump is stored. Redaction rules and the full
list: [reporting-issues.md](../reporting-issues.md).
