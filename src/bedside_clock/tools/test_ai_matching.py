#!/usr/bin/env python3
"""Offline (PC-side) tests for the bedside AI song matching.

Replicates the device's Song flow byte-for-byte (music_player.c do_song +
localai_client.c ai_pick_best) against the REAL Navidrome library and the
REAL LocalAI server, so prompt/flow changes can be validated here before
touching the firmware. Run it before and after modifying anything related
to LocalAI fetching, and keep its prompt/flow in sync with the C code.

Configuration (nothing is hard-coded): NAV_URL, NAV_USER, NAV_PASS, AI_URL and
AI_KEY come from the environment, or from the matching --nav-url/--nav-user/
--nav-pass/--ai-url/--ai-key flags, which win. AI_MODEL is optional. The script
refuses to start when one of the required values is missing.

Usage:
    python test_ai_matching.py            # run the standard test set
    python test_ai_matching.py --runs 3   # consistency runs per query
    python test_ai_matching.py "my song"  # ad-hoc single query
"""
import argparse
import hashlib
import json
import os
import random
import string
import sys
import time
import urllib.parse
import urllib.request

# --- server config (device settings equivalents) ---
# Filled in by main() from the environment / command line; see the docstring.
NAV_URL = None
NAV_USER = None
NAV_PASS = None
AI_URL = None
AI_KEY = ""
AI_MODEL = "qwen_qwen3-4b-instruct-2507"

# --- device constants (music_player.c / localai_client.c) ---
SEARCH_HITS = 40      # nav_search_songs(text, 40, ...)
LIBRARY_CAP = 400     # nav_search_songs("", 400, ...) fallback
SHORTLIST = 25        # max options sent to the model (small context!)
MAX_TOKENS = 8
TIMEOUT_S = 120

# The standard test set: query -> substring the winning title must contain
# (lowercase). These are fuzzy/partial on purpose.
TESTS = {
    "toccata":    "toccata",
    "tocata":     "toccata",   # typo - must survive the fuzzy shortlist
    "riders sky": "riders in the sky",
    "Red Gum":    None,   # expect a Redgum track - verified by artist below
    "Barbers":    "adagio",
    "time bottle": "time in a bottle",
    "Tenacious":  None,   # expect a Tenacious D track - verified by artist
    # sounds-like / phonetic misspellings
    "tokata":     "toccata",
    "gost riders": "riders in the sky",
    "tenashus":   None,   # Tenacious D - verified by artist
    "time in a botle": "time in a bottle",
    "tome":       None,   # vague - any consistent real song is a pass
    "Ram stein":  None,   # a Rammstein track - verified by artist
}
EXPECT_ARTIST = {
    "Red Gum":   "redgum",
    "Tenacious": "tenacious",
    "tenashus":  "tenacious",
    "Ram stein": "rammstein",
}


def subsonic(endpoint, **params):
    salt = "".join(random.choices(string.hexdigits.lower(), k=16))
    token = hashlib.md5((NAV_PASS + salt).encode()).hexdigest()
    qs = {
        "u": NAV_USER, "t": token, "s": salt,
        "v": "1.16.1", "c": "bedside-test", "f": "json", **params,
    }
    url = f"{NAV_URL}/rest/{endpoint}?{urllib.parse.urlencode(qs)}"
    with urllib.request.urlopen(url, timeout=30) as r:
        data = json.loads(r.read().decode())
    resp = data["subsonic-response"]
    if resp["status"] != "ok":
        raise RuntimeError(f"subsonic error: {resp.get('error')}")
    return resp


def search_songs(query, count):
    resp = subsonic("search3", query=query, songCount=count,
                    artistCount=0, albumCount=0)
    return resp.get("searchResult3", {}).get("song", [])


def ai_pick_best(query, options):
    """Mirror of localai_client.c ai_pick_best (keep in sync!)."""
    if len(options) == 1:
        return 0, 0.0
    listing = "".join(f"{i + 1}. {o}\n" for i, o in enumerate(options))
    prompt = (
        "Each list item is a song, written \"title - artist\". "
        "The request names a song or an artist, and may be misspelled "
        "or written how it sounds. The list is ordered with the most "
        "likely match first. Pick the item that best matches; when "
        "torn between similar items prefer the earlier one. "
        "Reply with ONLY its number, nothing else. "
        "Reply 0 only if no item shares any words or sounds with the "
        "request.\n\n"
        f"Request: {query}\n\nList:\n" + listing
    )
    body = {
        "model": AI_MODEL,
        "temperature": 0,
        "max_tokens": MAX_TOKENS,
        "messages": [{"role": "user", "content": prompt}],
    }
    req = urllib.request.Request(
        f"{AI_URL}/v1/chat/completions",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json",
                 **({"Authorization": f"Bearer {AI_KEY}"} if AI_KEY else {})},
        method="POST",
    )
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as r:
        data = json.loads(r.read().decode())
    took = time.time() - t0
    text = data["choices"][0]["message"]["content"]
    digits = text.lstrip()
    while digits and not digits[0].isdigit():
        digits = digits[1:]
    n = int("".join(c for c in digits if c.isdigit()) or "-1") \
        if digits else -1
    if 1 <= n <= len(options):
        return n - 1, took
    if n == 0:
        return None, took   # model says "no match"
    raise RuntimeError(f"unparseable model reply: {text!r}")


def _norm(s):
    """lowercase, strip punctuation (keep spaces) - mirror of C helper."""
    return "".join(c if c.isalnum() or c == " " else "" for c in s.lower())


def _squash(s):
    return s.replace(" ", "")


def _phonetic(s):
    """Cheap sounds-like fold (mirror of music_player.c phonetic_fold):
    silent-ish h dropped, c/q->k, z->s, all vowels+y -> a (vowel-blind),
    then collapse repeats. "toccata"/"tocata"/"tokata" -> "takata";
    "ghost"/"gost" -> "gast"; "tome"/"time" -> "tama"."""
    out = []
    for i, c in enumerate(s):
        if c == "h":
            continue
        if c == "c":
            nxt = s[i + 1] if i + 1 < len(s) else ""
            c = "s" if nxt in "eiy" else "k"   # soft c: "tenacious"
        c = {"q": "k", "z": "s",
             "e": "a", "i": "a", "o": "a", "u": "a", "y": "a"}.get(c, c)
        if out and out[-1] == c:
            continue
        out.append(c)
    return "".join(out)


def _lev(a, b):
    """Plain Levenshtein, early-out on hopeless length gap."""
    if abs(len(a) - len(b)) > 3:
        return 99
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[-1] + 1,
                           prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def fuzzy_score(query, title, artist):
    """Cheap token/substring scorer - mirror of music_player.c fuzzy_score.
    Punctuation-blind and word-join-blind ("red gum" hits "Redgum",
    "barbers" hits "Barber's"), with a character-bigram fallback so typos
    ("tocata") still surface candidates for the AI to judge."""
    q = _norm(query)
    haysq = _squash(_norm(title + " " + artist))
    hayph = _phonetic(haysq)
    words = [_phonetic(w) for w in _norm(title + " " + artist).split()
             if len(w) >= 3]
    score = 0
    for tok in q.split():
        if len(tok) < 2:
            continue
        if tok in haysq:
            score += 2 * len(tok)       # exact substring: strong
            continue
        ph = _phonetic(tok)
        # edit distance between PHONETIC folds: "tokata"->"takata" is 0
        # edits from "toccata"->"takata" but 2 from "takatukka"->
        # "takataka" - separates sound-alikes the fold alone ties
        if len(ph) >= 3:
            best = 0.0
            for w in words:
                d = _lev(ph, w)
                m = max(len(ph), len(w))
                sim = 1.0 - d / m
                if sim > best:
                    best = sim
            if best >= 0.8:
                score += round(len(tok) * best)
        if len(ph) >= 3 and ph in hayph:
            score += len(tok) + len(tok) // 2   # sounds-like: medium
            continue
        if len(tok) >= 4:
            grams = [tok[i:i + 2] for i in range(len(tok) - 1)]
            hits = sum(1 for g in grams if g in haysq)
            if hits * 4 >= len(grams) * 3:   # >= 75% of bigrams present
                score += (len(tok) * hits) // len(grams)
                continue
            if len(ph) >= 4:
                pg = [ph[i:i + 2] for i in range(len(ph) - 1)]
                phits = sum(1 for g in pg if g in hayph)
                if phits * 4 >= len(pg) * 3:
                    score += (len(tok) * phits) // (2 * len(pg))
    qsq = _squash(q)
    if len(qsq) >= 3 and qsq in haysq:
        score += 2 * len(qsq)
    else:
        qph = _phonetic(qsq)
        if len(qph) >= 4 and qph in hayph:
            score += len(qsq)           # joined sounds-like: "ram stein"
    return score


def shortlist(query, songs, cap):
    """Best `cap` candidates by fuzzy score (with scores); empty when
    nothing scores."""
    scored = []
    for s in songs:
        sc = fuzzy_score(query, s.get("title") or "", s.get("artist") or "")
        if sc > 0:
            scored.append((sc, s))
    scored.sort(key=lambda t: -t[0])
    return scored[:cap]


DOMINANT_MIN = 8   # absolute score a runaway winner must reach


def _fold_field(s):
    return _phonetic(_squash(_norm(s or "")))


def _is_dup_of(top, other):
    """Same artist and one folded title contains the other: remaster /
    remix duplicates of the same song."""
    if _fold_field(top.get("artist")) != _fold_field(other.get("artist")):
        return False
    a = _fold_field(top.get("title"))
    b = _fold_field(other.get("title"))
    return a in b or b in a


def dominant(scored):
    """The top candidate when it outscores the best NON-duplicate rival
    2:1 - a clear local win needs no AI referee (mirror of
    music_player.c logic). Duplicate variants of the top song don't
    count as rivals."""
    if not scored or scored[0][0] < DOMINANT_MIN:
        return None
    top_sc, top = scored[0]
    for sc, s in scored[1:]:
        if _is_dup_of(top, s):
            continue
        return top if top_sc >= 2 * sc else None
    return top   # nothing but duplicates of the winner


_library_cache = None


def get_library():
    global _library_cache
    if _library_cache is None:
        _library_cache = search_songs("", LIBRARY_CAP)
        print(f"  [library fallback list: {len(_library_cache)} songs]")
    return _library_cache


def resolve_song(query):
    """Device do_song(): search first; on empty, fuzzy-shortlist the
    library so the model never sees more than SHORTLIST options."""
    songs = search_songs(query, SEARCH_HITS)
    source = f"search ({len(songs)} hits)"
    if len(songs) > SHORTLIST:
        scored = shortlist(query, songs, SHORTLIST)
        if scored:
            win = dominant(scored)
            if win is not None:
                return win, source + " -> dominant", 0.0
            songs = [s for _, s in scored]
            source += f" -> shortlist {len(songs)}"
        else:
            songs = songs[:SHORTLIST]
            source += f" -> first {len(songs)}"
    if not songs:
        lib = get_library()
        scored = shortlist(query, lib, SHORTLIST)
        source = f"library shortlist ({len(scored)} of {len(lib)})"
        win = dominant(scored)
        if win is not None:
            return win, source + " -> dominant", 0.0
        songs = [s for _, s in scored]
    if not songs:
        return None, source, 0.0
    options = [f"{s.get('title', '?')} - {s.get('artist', '')}"
               for s in songs]
    idx, took = ai_pick_best(query, options)
    if idx is None:
        return None, source, took
    return songs[idx], source, took


def full_list_experiment(runs):
    """Send the ENTIRE library to the model (no shortlist) - measures
    whether the small model can take the full list: latency + accuracy."""
    lib = get_library()
    options = [f"{s.get('title', '?')} - {s.get('artist', '')}"
               for s in lib]
    print(f"\nFULL-LIST EXPERIMENT: {len(options)} options per prompt "
          f"(~{sum(len(o) for o in options) // 4} tokens of list)")
    for query in ("tocata", "Red Gum", "time bottle"):
        print(f"\n=== full-list {query!r} ===")
        for run in range(runs):
            try:
                idx, took = ai_pick_best(query, options)
            except Exception as e:
                print(f"  run {run + 1}: ERROR after long wait: {e}")
                continue
            if idx is None:
                print(f"  run {run + 1}: model said no match ({took:.0f}s)")
            else:
                print(f"  run {run + 1}: {options[idx]}  [{took:.0f}s]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("query", nargs="?", help="ad-hoc single query")
    ap.add_argument("--runs", type=int, default=3,
                    help="consistency runs per query (default 3)")
    ap.add_argument("--full-list", action="store_true",
                    help="experiment: whole library in one prompt")
    cfg = ap.add_argument_group("servers (each flag overrides the env var of the same name)")
    cfg.add_argument("--nav-url", default=os.environ.get("NAV_URL"),
                     help="Navidrome base URL, e.g. https://music.example.org (env NAV_URL)")
    cfg.add_argument("--nav-user", default=os.environ.get("NAV_USER"), help="Navidrome user (env NAV_USER)")
    cfg.add_argument("--nav-pass", default=os.environ.get("NAV_PASS"), help="Navidrome password (env NAV_PASS)")
    cfg.add_argument("--ai-url", default=os.environ.get("AI_URL"),
                     help="OpenAI-compatible server base URL, e.g. http://host:8080 (env AI_URL)")
    cfg.add_argument("--ai-key", default=os.environ.get("AI_KEY", ""),
                     help="Bearer key; empty sends no Authorization header (env AI_KEY)")
    cfg.add_argument("--ai-model", default=os.environ.get("AI_MODEL", AI_MODEL),
                     help=f"chat model name (env AI_MODEL, default {AI_MODEL})")
    args = ap.parse_args()

    global NAV_URL, NAV_USER, NAV_PASS, AI_URL, AI_KEY, AI_MODEL
    NAV_URL, NAV_USER, NAV_PASS = args.nav_url, args.nav_user, args.nav_pass
    AI_URL, AI_KEY, AI_MODEL = args.ai_url, args.ai_key, args.ai_model
    missing = [name for name, val in (("NAV_URL", NAV_URL), ("NAV_USER", NAV_USER),
                                      ("NAV_PASS", NAV_PASS), ("AI_URL", AI_URL)) if not val]
    if missing:
        sys.exit("missing configuration: set " + ", ".join(missing)
                 + " in the environment or pass --"
                 + ", --".join(m.lower().replace("_", "-") for m in missing))
    NAV_URL = NAV_URL.rstrip("/")
    AI_URL = AI_URL.rstrip("/")

    if args.full_list:
        full_list_experiment(args.runs)
        return 0

    queries = {args.query: None} if args.query else TESTS
    failures = 0

    for query, want_title in queries.items():
        print(f"\n=== {query!r} ===")
        picks = []
        song = None
        for run in range(args.runs):
            result, source, took = resolve_song(query)
            if result is None:
                print(f"  run {run + 1}: NO MATCH ({source}, {took:.1f}s)")
                picks.append(None)
                continue
            song = result
            label = f"{song.get('title')} - {song.get('artist')}"
            print(f"  run {run + 1}: {label}  [{source}, {took:.1f}s]")
            picks.append(song.get("id"))

        ok = True
        if len(set(picks)) != 1:
            print("  FAIL: inconsistent picks across runs")
            ok = False
        elif picks[0] is None or song is None:
            print("  FAIL: no song returned")
            ok = False
        else:
            title = (song.get("title") or "").lower()
            artist = (song.get("artist") or "").lower()
            if want_title and want_title not in title:
                print(f"  FAIL: expected title containing {want_title!r}")
                ok = False
            want_artist = EXPECT_ARTIST.get(query)
            if want_artist and want_artist not in artist:
                print(f"  FAIL: expected artist containing {want_artist!r}")
                ok = False
        if ok:
            print("  PASS")
        else:
            failures += 1

    print(f"\n{'ALL PASS' if failures == 0 else f'{failures} FAILURE(S)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
