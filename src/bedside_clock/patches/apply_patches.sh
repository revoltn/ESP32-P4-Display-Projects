#!/usr/bin/env bash
# Re-apply the local patches to managed_components/ after idf.py has populated it.
#
#   cd src/bedside_clock
#   idf.py reconfigure            # downloads the versions pinned in dependencies.lock
#   bash patches/apply_patches.sh
#   idf.py build
#
# Idempotent: a component whose patched file already carries the "LOCAL PATCH"
# marker is reported as OK and skipped. Every patch is dry-run first; nothing is
# half-applied. Exit status is non-zero when any patch could not be applied.
# See patches/README.md for what the patches do and why.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJ=$(dirname "$HERE")
MC="$PROJ/managed_components"

apply_one() {
    # $1 component directory name, $2 patch file (in this directory),
    # $3 file inside the component that carries the LOCAL PATCH marker once applied
    local comp="$1" patch_file="$2" marker_file="$3"
    local dir="$MC/$comp"
    if [ ! -d "$dir" ]; then
        echo "MISSING $comp: $dir does not exist - run 'idf.py reconfigure' first"
        return 1
    fi
    if grep -q "LOCAL PATCH" "$dir/$marker_file" 2>/dev/null; then
        echo "OK      $comp: already patched ($marker_file carries the LOCAL PATCH marker)"
        return 0
    fi
    if ! patch -p1 -d "$dir" --dry-run --silent < "$HERE/$patch_file"; then
        echo "FAIL    $comp: $patch_file does not apply - component version drift?"
        echo "        (patch was made against the version in dependencies.lock; see README.md)"
        return 1
    fi
    if patch -p1 -d "$dir" --silent < "$HERE/$patch_file"; then
        echo "APPLIED $comp: $patch_file"
    else
        echo "FAIL    $comp: patch reported an error while applying $patch_file"
        return 1
    fi
}

rc=0
apply_one espressif__esp_lvgl_port \
          0001-esp_lvgl_port-2.9.0-bounded-ppa-wait-cpu-fallback.patch \
          src/common/ppa/lcd_ppa.c || rc=1
apply_one espressif__esp_codec_dev \
          0002-esp_codec_dev-1.5.11-fail-open-when-set_fmt-fails.patch \
          esp_codec_dev.c || rc=1

if [ $rc -ne 0 ]; then
    echo "one or more patches were not applied - the firmware will build but the"
    echo "PPA-hang and codec-open fixes will be missing (see patches/README.md)"
fi
exit $rc
