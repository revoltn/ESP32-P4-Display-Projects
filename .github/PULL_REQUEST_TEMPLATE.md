<!-- Thanks. Keep what applies, delete what does not. CONTRIBUTING.md has the details. -->

## What and why

<!-- One paragraph: the problem, the change, the reason this approach. Link the issue: Fixes #N -->

## Hardware verification statement

<!-- Required for firmware changes. "Not tested on hardware" is acceptable only for docs-only changes. -->

```
Tested on: <board>, ESP32-P4 rev <x.y>, rear label <text>, C6 slave <version>
Build:     idf.py <exact command> (ESP-IDF v5.5.5)
Checked:   <boot clean, WiFi connect, alarm ring, 30 min idle, ... >
Not run:   <camera, USB audio, ...>
```

## Checklist

- [ ] Builds with the documented command (`idf.py build`; also `-B build_mww ...` if `src/bedside_clock` is touched)
- [ ] No secrets, no generated files (`build*/`, `managed_components/`, `sdkconfig`), no vendor binaries
- [ ] LVGL callbacks only post jobs; blocking calls run on the worker or a dedicated task
- [ ] No new board literals in `main/` (or they are flagged below for the board-profile work)
- [ ] Settings schema: version bumped and migration added, if the settings struct changed
- [ ] Documents updated (device folder, app docs, `docs/howto/http-api.md` for API changes); new documents added to `docs/README.md`
- [ ] `CHANGELOG.md` entry with date and hardware
- [ ] Third-party code: licence compatible, `THIRD_PARTY_NOTICES.md` entry, `LOCAL PATCH` markers on modified managed components
- [ ] `dependencies.lock` changes explained below, if any

## Notes for the reviewer

<!-- Risky areas, things you could not test, follow-ups, screenshots or a short video for UI changes. -->
