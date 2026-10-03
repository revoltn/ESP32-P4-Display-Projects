# Licensing: what this repository may publish, and why

Maintainer notes. The itemised attribution list is [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)
(referred to below as "the notices file"); this document records the rules that produced it,
the honest verdict on the state of the working folder before the 2026-10-02 restructure, the
one unresolved problem (the 10.1 inch touch driver), and the checklist to run before every
push.

## 1. The repository licence and what it covers

The repository is MIT (`LICENSE`, "Copyright (c) 2026 Kent (github.com/revoltn)"). The grant
covers what the author wrote:

- `src/*/main/` (application code, generated font tables, the embedded web page, wake-word
  models trained by the author and their manifests),
- `src/*/tools/` (host-side scripts, the wake-word training recipe),
- `src/bedside_clock/patches/*.patch` (the diffs themselves; the code they patch stays
  Apache-2.0, and a patch applied to an Apache file produces an Apache file),
- `docs/`, `devices/*/docs/`, `devices/*/README.md`, the inventories and board notes,
- `sdkconfig*.defaults`, `partitions.csv`, `CMakeLists.txt`, `idf_component.yml`,
  `dependencies.lock`.

It does not cover `src/*/components/` (Apache-2.0 upstreams, some modified here), anything
the component manager downloads, fonts, the one third-party model in the benchmark, or any of
the reference trees that stay on disk. The MIT text's "included in all copies or substantial
portions" condition is satisfied for downstream users by `LICENSE` at the root.

## 2. Decision rules

**Host versus link.** Material is committed only when (a) a licence permits redistribution,
(b) the licence conditions can be met inside this tree, and (c) it is needed to build or to
understand the firmware. Everything else is linked by URL, with a SHA256 where a byte-exact
identity matters (factory images). Unlicensed GitHub repositories and vendor packages fail
(a); EspControl fails (b) for an MIT repository; ESPHome passes (a) and (b) but fails (c).

**Apache-2.0 section 4.** Keep the `LICENSE` file (and `NOTICE`, when upstream ships one; none
of the vendored components does). Section 4(b): every modified file carries a prominent
statement that it was changed. The modified files are enumerated in section 1 of the notices
file; the test used to find them was a hash comparison against each component's
`CHECKSUMS.json`, not memory. Guition's own modifications to Espressif code carry no such
notice from Guition; the notices file records them instead, which is the most this repository
can do.

**MIT, BSD, CC0.** Keep the copyright and permission notice with the code. CC0 code
(`app_video.c` donor) needs nothing, but the origin is recorded anyway.

**Espressif MIT.** `esp-sr`, `esp_video`, `esp_ipa` and `esp_new_jpeg` use Espressif's MIT
variant: permission "for use on all ESPRESSIF SYSTEMS products". On an ESP32-P4 the condition
is met. The clause is nonetheless an additional restriction in GPL terms, which is why it is
listed separately and why it matters in section 4 below.

**OFL 1.1.** The licence text travels with the font and with every Modified Version. A glyph
table produced by `lv_font_conv` is a Modified Version (a subset, re-encoded), so
`tools/fonts/OFL-*.txt` stay in the tree and `gen_fonts.sh` documents the mapping. OFL forbids
selling the font files by themselves; embedding in firmware is explicitly allowed.

**CC BY 4.0 (Font Awesome icons).** Name Fonticons, Inc. and the licence where the icons are
used (`main/fonts/app_icons.h`, `tools/fonts/LICENSE-FontAwesome.txt`, the notices file).

**PolyForm Noncommercial 1.0.0 (EspControl).** Use, study and private copies are allowed for
any noncommercial purpose. Redistribution is allowed only noncommercially and only with the
licence and the "Required Notice" line. Publishing it here would make this repository
non-OSI and carry the notice obligation onto every fork. Decision: read it, write down what
was learned (`docs/reliability-checklist.md`, `07-espcontrol-findings.md`), never copy code.
A grep of `src/` for EspControl identifiers (`configuration_store`, `espcontrol`) finds
nothing.

**GPL.** No GPL code is copied into `src/`. Reading GPL code to understand an algorithm and
then writing one's own implementation of a publicly documented recipe is what was done for
`wake_mww.cpp` (ESPHome's `micro_wake_word`, GPLv3). A GPL *component pulled at build time*
is a different matter: the resulting firmware image is a combined work, and distributing that
image is governed by the GPL. See section 4.

**RealNetworks RPSL (libhelix-mp3).** The MP3 decoder inside `chmorgan/esp-libhelix-mp3`
is RPSL 1.0 / RCSL 1.0 even though the wrapper component says Apache-2.0. Nothing here
modifies it and the repository does not redistribute it. The RPSL's obligations (notice,
source availability for modifications, the "Externally Deploy" clause) fall on whoever
distributes a firmware binary that contains it. If binaries are ever published, add the RPSL
notice to the release and keep the decoder unmodified.

**Unlicensed repositories.** No licence means all rights reserved. GitHub's terms let other
users view and fork *on GitHub*; they do not grant redistribution elsewhere. Private local
copies for study are tolerated practice and were never published; the repositories are now
linked, not hosted.

**Vendor packages.** Guition publishes its board packages freely on its download site and
grants no licence. Inside them, each Espressif/third-party component keeps its own licence
(and those parts, the four vendor-patched components, are what this repository redistributes,
under Apache-2.0, with provenance recorded). The rest (factory images, datasheets, CAD,
WinHex, drivers, sample videos, MP3s) is linked by URL and SHA256.

**Training data.** The wake-word models are the author's work and are released MIT. The
datasets that fed negatives (Hugging Face `kahrendt/microwakeword`, MIT RIRs, FMA, AudioSet)
are not redistributed; their terms attach to the data. No position is taken here on whether
dataset terms reach trained weights; the facts (what was used, that none of it is in the
tree) are in section 5 of the notices file.

## 3. Did we contravene any licence?

Context: until 2026-10-02 the working folder, which was never pushed anywhere, contained full
copies of EspControl, ESPHome, both Guition packages, two unlicensed community repositories
and a mirror of the 10.1 inch package. Two local commits were made (a pre-restructure
snapshot and the move-only restructure); neither left the machine.

| Item | Verdict | Reasoning |
|---|---|---|
| Private copies of EspControl, ESPHome, vendor packages, unlicensed repos, the DevinWatson mirror | **Not a breach.** | Copying for private study and build experiments is permitted (MIT, GPLv3, PolyForm NC all allow use and private modification; GitHub's terms cover viewing and cloning; the vendor packages are offered for download by the vendor). Nothing was distributed. **Publishing them would have been**: EspControl only noncommercially and with the Required Notice (and a repository-wide PolyForm taint); unlicensed repos not at all; factory `.bin` images, Espressif and OmniVision datasheets, WinHex, the sample videos and the demo MP3s not at all. They are now gitignored and linked. |
| Modified Apache-2.0 files without a 4(b) notice (`bsp_extra`, `esp_hosted` x2, `usb_host_uac` x2, `esp-audio-player` x3 in both projects) | **No breach yet; fixed.** | Section 4 of Apache-2.0 conditions *redistribution*. No copy left the machine, so the obligation had not been triggered. The notices were added on 2026-10-02 before the first push, and the notices file enumerates every changed file. |
| Montserrat glyph tables without the OFL text | **No breach yet; fixed.** | Same logic: OFL conditions distribution. `tools/fonts/OFL-Montserrat.txt` was added; Dangrek, Anton, Archivo Black and Font Awesome already had theirs. |
| `wake_mww.cpp` versus ESPHome's GPLv3 `micro_wake_word` | **No breach; defensible, documented.** | No ESPHome file was copied (grep for `esphome` in `src/` hits only the dependency name, manifest fields and comments). What was reused is the publicly documented microWakeWord recipe (feature quantisation formula, window mean, cool-off count), which is algorithmic and is also published under Apache-2.0 by the microWakeWord project. The code was written against `esp-tflite-micro` and validated against the reference in `src/sample_wakeword_bench`. The source header says so. |
| Redistributing Guition's patched Espressif components (BSP, ST7701, `esp_cam_sensor`) | **Compliant on our side.** | Apache-2.0 permits it; the `LICENSE` files travel; provenance and the vendor's changes are recorded. The missing 4(b) notices on Guition's own edits are Guition's omission; recording them is the practical remedy. |
| Vendoring `esp_hosted` 2.12.12 in full (20 MB incl. `slave/`, `examples/`, `.gitlab/`) | **Compliant, wasteful.** | Nothing in Apache-2.0 requires trimming; it is a size problem, not a licence one. |
| `okay_nabu.tflite` in the benchmark | **Compliant once attributed.** | Apache-2.0 asks for the licence notice and attribution; both are now in the notices file and `src/sample_wakeword_bench/NOTICE.md`. The file is unmodified. |
| Font Awesome glyphs in `app_icons_28.c` | **Compliant.** | CC BY 4.0 attribution present in `app_icons.h`, `gen_fonts.sh`, `LICENSE-FontAwesome.txt` and the notices file. |
| Schematic sheets as `.webp` in the 4.3 inch docs | **Not published; now excluded.** | They were Guition copyright with no licence. The images are gitignored; the pin tables derived from them are facts and stay. p1ngb4ck has Guition's permission to post schematics; this repository does not. |
| Espressif MIT components (`esp-sr`, `esp_video`, `esp_ipa`, `esp_new_jpeg`) | **Compliant.** | Field-of-use satisfied (ESP32-P4). Not redistributed here. |
| `esp-libhelix-mp3` (RPSL) linked into every image | **Compliant; a note for binary releases.** | Unmodified, fetched at build time, not redistributed in source. A published firmware binary would need the RPSL notice alongside it. No binaries have been published. |
| Baked-in credentials and a LAN hostname in `sample_hello_world` and `bedside_clock` defaults | **Not a licence matter; a secrets matter.** | Scrubbed in the same pass (Kconfig symbols / empty defaults). Listed here because the publishing checklist below exists for it. |
| Hugging Face / FMA / AudioSet data used to train the wake-word models | **No breach identified.** | Data not redistributed; models are the author's own. See the training-data rule above. |

Net: nothing was distributed in breach of a licence. Several obligations that only arise on
distribution (Apache 4(b), OFL text, attribution of the benchmark model) were unmet in the
private tree and were satisfied before the first push.

## 4. The 10.1 inch touch driver (Guition JC8012P4A1C, Silead GSL3680)

The 10.1 inch board uses a Silead GSL3680 touch controller instead of the 4.3 inch board's
Goodix GT911. The GSL3680 has no flash: the host uploads a firmware image (about 4600 words)
over I2C at every boot, and the raw coordinates are post-processed by Silead's point-ID
algorithm. Every available driver is built from the same two Silead pieces:

- `gsl_point_id.c`: Silead's algorithm, **GPL-2.0-or-later** by its own header
  ("2010 - 2016 silead inc.", from the MediaTek Android kernel tree). Six identical copies in
  the Guition package; the same file in `mangoo1/esp_lcd_touch_gsl3680` on the component
  registry (listed there as GPL-2.0-or-later) and in ESPHome's `gsl3670` component.
- `GSLX680_FW[]`: the controller firmware blob, embedded as a C array, with **no licence
  statement** from Silead or Guition anywhere in the chain.

None of this can be vendored into an MIT tree. Since 2026-10-03 the 10.1 inch build consumes
`mangoo1/esp_lcd_touch_gsl3680` as a registry dependency declared in
`src/bedside_clock/components/espressif__esp32_p4_function_ev_board/idf_component.yml`
(fetched by `idf.py` into the gitignored `managed_components/`, exactly as `esp-sr` or `lvgl`
are consumed, never copied). The BSP only calls it under `CONFIG_BSP_LCD_TYPE_1280_800`; in a
4.3 inch build the component is still downloaded and compiled, but no object from it is
referenced, so the linker drops it and the 4.3 inch image contains no GPL code. Moving the
declaration into a 10.1 inch-only component directory (so a 4.3 inch build never fetches it)
is part of the board-profile plumbing, step 2 of
`architecture/board-profiles-plan.md`.

What that does to distribution, stated plainly:

- **Source**: this repository stays MIT. MIT is GPL-compatible, so a downstream builder may
  combine it with the GPL component. Nothing changes for source publication.
- **Binaries of the 10.1 inch profile**: a firmware image that links `gsl_point_id.c` is a
  combined work governed by the GPL-2.0-or-later. Whoever distributes that image (the
  maintainer included) must offer the complete corresponding source under GPL terms. For
  the application code that is possible (MIT). It is **not** possible for the parts the image
  also contains that carry GPL-incompatible terms: the Espressif MIT components with their
  field-of-use clause, and in particular `esp-sr`'s closed binary libraries and models, which
  `bedside_clock` needs for voice control. The Silead firmware blob has no licence at all and
  cannot be offered as "corresponding source" in any meaningful sense. In practice this means
  **10.1 inch binaries of `bedside_clock` must not be distributed**; users build them
  themselves from source, and the GPL component is downloaded onto their machine by the
  component manager. The 4.3 inch build is unaffected (GT911 driver is Apache-2.0).
- The same logic already applies to anyone shipping the Guition factory demos, which link
  the same GPL file next to Apache, Espressif MIT and proprietary code.

Alternative, if a distributable 10.1 inch binary is ever wanted: ESPHome's approach. ESPHome
keeps the GSL firmware out of its own tree and fetches it at build time from a separately
hosted release (`esphome-libs/gsl3670-firmware`, as recorded in the ESPHome `gsl3670`
component; not inspected for this note), and treats the touch stack as the one GPL piece in
an otherwise GPL-3 C++ codebase. For this repository the equivalent would be a touch stack
that does not contain Silead's algorithm at all (raw coordinate read-out without
`gsl_alg_id_main`), which has not been attempted and may not give usable multi-touch.

The JD9365 **V2 panel init table** (`priv_include/jd9365_init_jc8012p4a1_v2.h` in the BSP
fork) is a register sequence copied, with attribution, from ESPHome's
`esphome/components/mipi_dsi/models/guition.py`. ESPHome's licence file places its Python
sources under MIT (only the C++/runtime files are GPL-3), so this copy is MIT-on-MIT and
carries ESPHome's copyright line in the header. The JD9365 panel driver itself
(`espressif/esp_lcd_jd9365`) is Apache-2.0 from the registry.

Status: the only GPL reference in any `idf_component.yml` is the GSL3680 touch driver named
above.

## 5. Publishing checklist

Run before every push. The first three are hard stops.

1. **Secrets.** `git grep -n -i -E 'password|passwd|api[_-]?key|x-api-key|bearer |ssid' -- src docs devices`
   and read every hit; only symbol names, Kconfig prompts and `<key>` placeholders may remain.
   Also `git grep -n -E '192\.168\.|your-private-domain|BEDSIDE_API_KEY=|[0-9A-F]{2}(:[0-9A-F]{2}){5}'`
   (LAN addresses, the old hostname, device MACs). Compiled copies: no `__pycache__/` or
   `*.pyc` tracked.
2. **Nothing ignored leaked.** `git ls-files | grep -E 'vendor/package|vendor/.*\.zip|community/|upstream/|managed_components/|/build|sdkconfig$|sdkconfig\.(old|mww|ref)$|\.bin$|\.webp$|schematics/|\.bak$'`
   must print nothing. `git check-ignore -v devices/guition-jc4880p443c/vendor/package reference/esphome/upstream`
   must name a rule for each.
3. **Licence files present.** `LICENSE`, `THIRD_PARTY_NOTICES.md`, `docs/licensing.md`,
   `src/*/NOTICE.md`; a `LICENSE`/`license.txt` in every `src/*/components/*`;
   `src/bedside_clock/tools/fonts/{OFL-Dangrek,OFL-Montserrat,OFL-Anton,OFL-ArchivoBlack,LICENSE-FontAwesome}.txt`;
   `src/bedside_clock/patches/README.md` next to the `.patch` files.
4. **Apache 4(b) notices** in every file the notices file lists as modified:
   `grep -L -i -E 'modified|local patch|local override' <the nine files>` must print nothing.
5. **Model manifests** (`main/models/*.json`, `assets/*.json`): `author` carries no personal
   data beyond what the author intends to publish.
6. **Size.** `git ls-files -z | xargs -0 du -b | sort -n | tail -5`: nothing above 50 MB
   (GitHub warns) and nothing above 100 MB (GitHub refuses). The largest tracked files should
   be the generated font tables (about 2 MB) and `esp_hosted`.
7. **Lock files.** `dependencies.lock` is committed; it contains absolute local paths for the
   seven local components (harmless: the component manager rewrites them). The
   `*.pre-5.5.5.bak` copies are not tracked.
8. **Line endings.** `.gitattributes` normalises to LF; scripts (`*.sh`, `*.py`) must be LF for
   WSL and Git Bash. `git ls-files --eol | grep -E 'w/crlf' ` should list only files that are
   deliberately CRLF (none are).
9. **Docs still true.** Every path in `README.md`, `src/*/README.md` and the notices file
   resolves (`git ls-files | grep` the ones you touched); the project names in new text are
   `bedside_clock`, `sample_hello_world`, `sample_wakeword_bench`.

## 6. Where the facts live

- Attribution, versions, commits, modification lists: `THIRD_PARTY_NOTICES.md` (sections 1 to 7).
- How to record a new third-party item: `THIRD_PARTY_NOTICES.md` section 8.
- Per-project summaries for people who only clone one project: `src/*/NOTICE.md`.
- The audit that produced this state (secrets, sizes, provenance of every reference tree) was
  a one-off read-only pass on 2026-10-02; its conclusions are folded into the two files above
  and into the device inventories (`devices/guition-jc8012p4a1c/vendor/INVENTORY.md`).
