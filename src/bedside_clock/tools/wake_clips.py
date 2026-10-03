#!/usr/bin/env python3
"""Wake-word training-data flywheel for bedside_clock.

Talks to the clock's HTTP API (settings: API enabled + key) and does one of:

  pull     download every wake / recording clip the device still holds in RAM
           that we have not saved yet -> <out>/<kind>_<verdict>/<id>_<stamp>.wav
           plus one JSON line per clip in <out>/clips.jsonl. Run it from a
           scheduled task every hour: the device keeps only the last 16 clips.
  record   guided recording session for POSITIVES through the device's own
           mic + gain + AFE: prompts you, records N seconds, saves the WAV into
           <out>/rec_positive/. Use --label to record negatives / confusables.
  clear    forget the clips on the device.

Examples
  python wake_clips.py --host <device-ip> --key <api-key> pull
  python wake_clips.py --host <device-ip> --key <api-key> record --count 30 --seconds 3
  python wake_clips.py --host <device-ip> --key <api-key> record --count 20 --label confusable_hi_there --prompt "say: hi there"
  (--key may be omitted when BEDSIDE_API_KEY is set in the environment)

Standard library only.
"""
import argparse, json, os, sys, time, urllib.request, urllib.error

def api(host, key, path, method="GET", body=None, timeout=30):
    req = urllib.request.Request(f"http://{host}{path}", method=method,
                                 headers={"X-Api-Key": key})
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, data, timeout=timeout) as r:
        ctype = r.headers.get("Content-Type", "")
        payload = r.read()
        if ctype.startswith("application/json"):
            return json.loads(payload), r.headers
        return payload, r.headers

def stamp(epoch, up):
    if epoch:
        return time.strftime("%Y%m%d-%H%M%S", time.localtime(epoch))
    return f"up{int(up)}"

def cmd_pull(a):
    seen_path = os.path.join(a.out, "seen.json")
    seen = {}
    if os.path.exists(seen_path):
        seen = json.load(open(seen_path))
    lst, _ = api(a.host, a.key, "/api/v1/voice/clips")
    new = 0
    for it in lst["items"]:
        # ids restart at 1 after a reboot: key on id + timestamp
        k = f"{it['id']}:{it.get('t', 0)}:{it.get('up', 0)}"
        if k in seen:
            continue
        if it.get("verdict") == "pending":
            continue  # come back next time
        wav, _ = api(a.host, a.key, f"/api/v1/voice/clips/{it['id']}")
        sub = f"{it['kind']}_{it.get('verdict', 'none')}"
        d = os.path.join(a.out, sub)
        os.makedirs(d, exist_ok=True)
        fn = os.path.join(d, f"{stamp(it.get('t', 0), it.get('up', 0))}_{it['id']}.wav")
        with open(fn, "wb") as f:
            f.write(wav)
        it["file"] = os.path.relpath(fn, a.out)
        with open(os.path.join(a.out, "clips.jsonl"), "a", encoding="utf-8") as f:
            f.write(json.dumps(it) + "\n")
        seen[k] = it["file"]
        new += 1
        print(f"saved {it['file']}  verdict={it.get('verdict')} text={it.get('text')!r}")
    json.dump(seen, open(seen_path, "w"), indent=0)
    print(f"{new} new clip(s), {lst['total']} on device")

def cmd_record(a):
    d = os.path.join(a.out, a.label)
    os.makedirs(d, exist_ok=True)
    print(f"Recording {a.count} x {a.seconds} s into {d}. Ctrl-C to stop.")
    n = 0
    while n < a.count:
        try:
            input(f"[{n + 1}/{a.count}] press Enter, then {a.prompt} ")
        except (EOFError, KeyboardInterrupt):
            break
        try:
            wav, hdr = api(a.host, a.key, "/api/v1/voice/record", "POST",
                           {"seconds": a.seconds}, timeout=a.seconds + 20)
        except urllib.error.HTTPError as e:
            print(f"  device said {e.code}: {e.read().decode(errors='replace')}")
            time.sleep(1)
            continue
        fn = os.path.join(d, f"{time.strftime('%Y%m%d-%H%M%S')}_{hdr.get('X-Clip-Id', '0')}.wav")
        with open(fn, "wb") as f:
            f.write(wav)
        n += 1
        print(f"  saved {fn} ({len(wav)} bytes)")
    print(f"{n} clip(s) recorded")

def cmd_session(a):
    """Hands-free positives: record back-to-back 8 s clips for --minutes while the
    speaker repeats the phrase every few seconds; split_utterances.py cuts them up."""
    d = os.path.join(a.out, a.label)
    os.makedirs(d, exist_ok=True)
    end = time.time() + a.minutes * 60
    n = 0
    print(f"session: {a.minutes} min of {a.seconds} s clips into {d}")
    while time.time() < end:
        try:
            wav, hdr = api(a.host, a.key, "/api/v1/voice/record", "POST",
                           {"seconds": a.seconds}, timeout=a.seconds + 20)
        except urllib.error.HTTPError as e:
            print(f"  device said {e.code}: {e.read().decode(errors='replace')}")
            time.sleep(2)
            continue
        except Exception as e:
            print("  error:", e)
            time.sleep(2)
            continue
        fn = os.path.join(d, f"{time.strftime('%Y%m%d-%H%M%S')}_{hdr.get('X-Clip-Id', '0')}.wav")
        with open(fn, "wb") as f:
            f.write(wav)
        n += 1
        left = max(0, end - time.time())
        print(f"  clip {n} saved ({len(wav)} B), {left:.0f} s left", flush=True)
    print(f"session done: {n} clips in {d}")

def cmd_status(a):
    s, _ = api(a.host, a.key, "/api/v1/status")
    print(json.dumps(s, indent=1)[:2000])
    st, _ = api(a.host, a.key, "/api/v1/settings")
    print({k: v for k, v in st.items() if k.startswith("voice")})

def cmd_clear(a):
    r, _ = api(a.host, a.key, "/api/v1/voice/clips/clear", "POST", {})
    print(r)

def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True, help="device IP[:port]")
    p.add_argument("--key", default=os.environ.get("BEDSIDE_API_KEY", ""), help="API key (or env BEDSIDE_API_KEY)")
    p.add_argument("--out", default="wake_clips", help="output folder")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("pull")
    r = sub.add_parser("record")
    r.add_argument("--count", type=int, default=20)
    r.add_argument("--seconds", type=int, default=3)
    r.add_argument("--label", default="rec_positive")
    r.add_argument("--prompt", default='say "Hi ESP" naturally:')
    s = sub.add_parser("session")
    s.add_argument("--minutes", type=float, default=3)
    s.add_argument("--seconds", type=int, default=8)
    s.add_argument("--label", default="session_positive")
    sub.add_parser("status")
    sub.add_parser("clear")
    a = p.parse_args()
    if not a.key:
        sys.exit("need --key or BEDSIDE_API_KEY")
    {"pull": cmd_pull, "record": cmd_record, "session": cmd_session,
     "status": cmd_status, "clear": cmd_clear}[a.cmd](a)

if __name__ == "__main__":
    main()
