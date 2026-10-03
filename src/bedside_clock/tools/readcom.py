"""Non-interactive serial capture for the board (the tool docs/02 refers to).

usage: readcom.py COMx <seconds> [reset]
  reset  toggles RTS so the capture starts with the boot log.

Run it from the IDF Python env (it has pyserial):
  & $env:USERPROFILE\\.espressif\\python_env\\idf5.5_py3.12_env\\Scripts\\python.exe tools\\readcom.py COMx 40 reset
  (%USERPROFILE%\\.espressif\\... from cmd.exe; the env dir name follows your IDF version)

Typical use while exercising the device over the HTTP API (docs/11 §10): start this in the
background writing to a file, drive playback with Invoke-RestMethod, then grep the file for
`0x7180`, `Navidrome`, `song_cache`, `Guru`. Only one process can hold the port - close
`idf.py monitor` first."""
import sys
import time

import serial

port, secs = sys.argv[1], float(sys.argv[2])
do_reset = len(sys.argv) > 3 and sys.argv[3] == "reset"
with serial.Serial(port, 115200, timeout=0.2) as s:
    if do_reset:
        s.dtr = False
        s.rts = True
        time.sleep(0.1)
        s.rts = False
        time.sleep(0.05)
    s.reset_input_buffer()
    end = time.time() + secs
    buf = b""
    while time.time() < end:
        buf += s.read(4096)
    sys.stdout.write(buf.decode("utf-8", "replace"))
