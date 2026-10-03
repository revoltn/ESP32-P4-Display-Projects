# Publish checklist

Run before every push to a public remote, and in full before a release. The repository
history contains a pre-publication snapshot with local secrets; nothing from before the
restructure may ever be pushed as-is.

## 1. Secrets

Search the tracked files, not the working tree (ignored vendor trees are huge and irrelevant):

```powershell
git grep -n -i -E '(password|passwd|api[_-]?key|secret|token|bearer)\s*[:=]' -- ':!*.lock' ':!*.tflite'
git grep -n -i -E 'ssid\s*[:=]\s*"[^"]+"'
git grep -n -E '\b(192\.168|10\.[0-9]+|172\.(1[6-9]|2[0-9]|3[01]))\.[0-9]+\.[0-9]+\b'
git grep -n -E '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}'           # MAC addresses
git grep -n -i -E 'https?://[a-z0-9.-]+\.(local|lan|home|internal)\b'   # private hostnames; add your own domain
```

```bash
git grep -n -i -E '(password|passwd|api[_-]?key|secret|token|bearer)\s*[:=]' -- ':!*.lock' ':!*.tflite'
```

Then grep for the specific strings you know are yours: your SSID, your API key, your server
hostnames, your MAC. Expected hits that are fine: Kconfig defaults that are empty strings,
documentation placeholders in angle brackets, `api_key` as a JSON field name, test fixtures
that are obviously fake.

Things that have needed scrubbing in this tree before: WiFi credentials compiled into the
sample, a Navidrome password constant, the author's AI server URL and key as settings
defaults in `settings.c`, IP addresses and the API key in tool docstrings and READMEs, a
personal spec note. Every one of these must be empty, a placeholder, or moved into an
untracked `*.local.*` file.

Finally check the history you are about to push:

```powershell
git log --oneline origin/main..HEAD
git log -p origin/main..HEAD | Select-String -Pattern 'password|api_key|ssid' -CaseSensitive:$false | Select-Object -First 20
```

If the pre-publication snapshot commit is an ancestor of what you push, stop: the public
history must start from a clean root.

## 2. Licences

| Check | Command or place |
|---|---|
| Every vendored or copied third-party file has an entry in `THIRD_PARTY_NOTICES.md` | `git diff --stat origin/main..HEAD -- src/*/components reference devices/*/community` and compare |
| Modified Apache-2.0 files carry a notice that they were changed (Apache 2.0 section 4(b)): the local patches in `esp_lvgl_port`, `esp_codec_dev`, `esp_hosted`, the vendored BSP and `esp_lcd_st7701` | the `LOCAL PATCH` markers plus a line in `THIRD_PARTY_NOTICES.md` |
| No PolyForm Noncommercial code (EspControl) copied into `src/` | `git grep -n -i espcontrol -- src` should find comments only |
| No GPL code linked into the MIT firmware (the Silead `gsl_point_id.c`) | `git grep -n -i gsl_point_id -- src` must be empty until a licence decision is recorded in `docs/licensing.md` |
| No binaries without a redistribution grant: vendor firmware images, schematics, datasheets, the WinHex archive from the 10.1" package | `.gitignore` rules below plus a spot check |
| `LICENSE` year and holder, `docs/licensing.md` and `THIRD_PARTY_NOTICES.md` agree | read them |

## 3. gitignore sanity

```powershell
git status --ignored --short | Select-String -Pattern '^!!' | Select-Object -First 40
```

Expected to be ignored: `src/*/build*/`, `src/*/managed_components/`, `src/*/sdkconfig`,
`src/*/sdkconfig.mww`, `devices/*/vendor/package/`, `devices/*/community/*/`,
`reference/*/upstream/`, `devices/*/docs/schematics/*.webp`, `devices/*/firmware/*.bin`.

Spot checks that must come back `IGNORED` (anything else is a leak waiting to happen):

```powershell
foreach ($p in 'src/bedside_clock/build','src/bedside_clock/managed_components',
               'src/bedside_clock/sdkconfig','devices/guition-jc4880p443c/vendor/package',
               'reference/esphome/upstream','devices/guition-jc4880p443c/docs/schematics/01.webp') {
  if (git check-ignore -q $p) { "IGNORED  $p" } else { "TRACKED? $p" }
}
```

And the inverse, files that must be tracked: each project's `dependencies.lock`,
`sdkconfig.defaults`, `sdkconfig.mww.defaults`, `partitions.csv`, the `main/models/*.tflite`
and `.json` pairs, every `README.md` under `devices/` and `reference/`.

```powershell
git ls-files --error-unmatch src/bedside_clock/dependencies.lock src/bedside_clock/main/models/hey_mr_clock.tflite
```

## 4. Size

A clone should be small: sources, documentation, two wake-word models and a few logs. Check
the tracked payload and the pack:

```powershell
(git ls-files | ForEach-Object { Get-Item $_ -ErrorAction SilentlyContinue } | Measure-Object -Sum Length).Sum / 1MB
git count-objects -vH
git ls-files | ForEach-Object { Get-Item $_ -ErrorAction SilentlyContinue } | Sort-Object Length -Descending | Select-Object -First 15 Length, FullName
```

```bash
git ls-files -z | xargs -0 du -ch | tail -1
git ls-files -z | xargs -0 ls -l | sort -k5 -n -r | head -15
```

Anything above a few MB in the top-15 list needs a reason (a model, a log) or a `.gitignore`
rule.

## 5. Line endings and encoding

`.gitattributes` normalises text to LF and keeps `*.sh`, `*.py`, `*.yml` LF on checkout. Verify
nothing slipped through with CRLF:

```powershell
git grep -I -l -P '\r$' -- ':!*.bin' | Select-Object -First 20
```

```bash
git grep -I -l -P '\r$' -- ':!*.bin' | head
```

## 6. Build from a clean clone

The last check is the one that catches everything else: clone into a fresh directory and
build both apps as [getting-started.md](../getting-started.md) describes. If it needs a file
you have locally and the clone does not, the clone is wrong.

```powershell
git clone <repository-url> $env:TEMP\clone-check
cd $env:TEMP\clone-check\src\bedside_clock
& $env:USERPROFILE\esp\esp-idf\export.ps1
idf.py set-target esp32p4; idf.py build
```

## 7. Before tagging a release

- `CHANGELOG.md` has an entry with the date, the hardware it was verified on, and the
  toolchain (`ESP-IDF v5.5.5`, component versions from `dependencies.lock`).
- The firmware's version string is not a placeholder (reliability checklist section 8).
- The `docs/README.md` index lists every new document.
