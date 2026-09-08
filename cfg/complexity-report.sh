#!/usr/bin/env bash
# Repository complexity scope and report generation.
set -euo pipefail

[[ $# -eq 1 ]] || { echo "usage: $0 OUTPUT_FILE" >&2; exit 2; }
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=$(realpath -m "$1")
mkdir -p "$(dirname "$output")"

source_dirs=(
    "$root/cfs/cfe/modules/config/fsw" "$root/cfs/cfe/modules/es/fsw"
    "$root/cfs/cfe/modules/evs/fsw" "$root/cfs/cfe/modules/fs/fsw"
    "$root/cfs/cfe/modules/msg/fsw" "$root/cfs/cfe/modules/resourceid/fsw"
    "$root/cfs/cfe/modules/sb/fsw" "$root/cfs/cfe/modules/sbr/fsw"
    "$root/cfs/cfe/modules/tbl/fsw" "$root/cfs/cfe/modules/time/fsw"
    "$root/cfs/apps/cf/fsw/src" "$root/cfs/apps/ci_lab/fsw/src"
    "$root/cfs/apps/ds/fsw/src" "$root/cfs/apps/fm/fsw/src"
    "$root/cfs/apps/io_lib/fsw/src" "$root/cfs/apps/lc/fsw/src"
    "$root/cfs/apps/sc/fsw/src" "$root/cfs/apps/sch/fsw/src"
    "$root/cfs/apps/to_lab/fsw/src"
    "$root/cfs/osal/src/os/shared/src"
    "$root/cfs/osal/src/os/posix/src" "$root/cfs/osal/src/os/portable"
    "$root/cfs/osal/src/os/shire/src"
    "$root/cfs/osal/src/bsp/shared/src" "$root/cfs/osal/src/bsp/generic-linux/src"
    "$root/cfs/psp/fsw/shared/src"
    "$root/cfs/psp/fsw/pc-linux/src"
    "$root/cfs/psp/fsw/shire/src"
    "$root/cfs/psp/fsw/modules/eeprom_mmap_file"
    "$root/cfs/psp/fsw/modules/iodriver/src"
    "$root/cfs/psp/fsw/modules/linux_sysmon"
    "$root/cfs/psp/fsw/modules/port_notimpl"
    "$root/cfs/psp/fsw/modules/ram_notimpl"
    "$root/comp/adcs/src" "$root/comp/adcs/shared" "$root/comp/adcs/sim"
    "$root/comp/demo/src" "$root/comp/demo/shared" "$root/comp/demo/sim"
    "$root/comp/eps/src" "$root/comp/eps/shared" "$root/comp/eps/sim"
    "$root/comp/radio/src" "$root/comp/radio/shared" "$root/comp/radio/sim"
    "$root/simulith/src"
)

mapfile -d '' sources < <(
    find "${source_dirs[@]}" -type f -name '*.c' \
        ! -path '*/test/*' ! -path '*/tests/*' ! -path '*/unit-test/*' \
        ! -path '*/unit_test/*' ! -path '*/ut-coverage/*' \
        ! -path '*/ut-stubs/*' ! -path '*/tables/*' -print0
)

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
pmccabe -X -c "${sources[@]}" | sort -nr > "$tmp"

{
    echo "SHIRE cyclomatic complexity (pmccabe -X -c)"
    echo "Columns: modified traditional statements first_line lines function"
    echo
    cat "$tmp"
    echo
    echo "Functions above modified complexity 10"
    awk '$1 > 10' "$tmp"
} > "$output"

echo "Complexity report: $output"
