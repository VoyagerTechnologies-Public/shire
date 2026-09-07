#!/usr/bin/env bash
# Repository coverage capture and filtering configuration.
set -euo pipefail

usage() {
    echo "usage: $0 initial|report SCOPE BUILD_DIR OUTPUT_DIR [SUPPLEMENT_DIR]" >&2
    echo "scopes: fsw, component-sim:NAME, simulith" >&2
    exit 2
}

[[ $# -ge 4 ]] || usage
phase=$1
scope=$2
build_dir=$(realpath "$3")
output_dir=$(realpath -m "$4")
supplement_dir=${5:-}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

mkdir -p "$output_dir"

scope_patterns=()
supplement_patterns=()
case "$scope" in
    fsw)
        scope_patterns=(
            "$root/cfs/cfe/modules/*/fsw/*"
            "$root/cfs/apps/cf/fsw/src/*"
            "$root/cfs/apps/ci_lab/fsw/src/*"
            "$root/cfs/apps/ds/fsw/src/*"
            "$root/cfs/apps/fm/fsw/src/*"
            "$root/cfs/apps/io_lib/fsw/src/*"
            "$root/cfs/apps/lc/fsw/src/*"
            "$root/cfs/apps/sc/fsw/src/*"
            "$root/cfs/apps/sch/fsw/src/*"
            "$root/cfs/apps/to_lab/fsw/src/*"
            "$root/cfs/osal/src/os/shared/src/*"
            "$root/cfs/osal/src/os/posix/src/*"
            "$root/cfs/osal/src/os/portable/*"
            "$root/cfs/osal/src/os/shire/src/*"
            "$root/cfs/osal/src/bsp/shared/src/*"
            "$root/cfs/osal/src/bsp/generic-linux/src/*"
            "$root/cfs/psp/fsw/shared/src/*"
            "$root/cfs/psp/fsw/pc-linux/src/*"
            "$root/cfs/psp/fsw/shire/src/*"
            "$root/cfs/psp/fsw/modules/eeprom_mmap_file/*"
            "$root/cfs/psp/fsw/modules/iodriver/src/*"
            "$root/cfs/psp/fsw/modules/linux_sysmon/*"
            "$root/cfs/psp/fsw/modules/port_notimpl/*"
            "$root/cfs/psp/fsw/modules/ram_notimpl/*"
            "$root/comp/adcs/src/*" "$root/comp/adcs/shared/*"
            "$root/comp/demo/src/*" "$root/comp/demo/shared/*"
            "$root/comp/eps/src/*" "$root/comp/eps/shared/*"
            "$root/comp/radio/src/*" "$root/comp/radio/shared/*"
        )
        supplement_patterns=(
            "$root/cfs/osal/src/os/posix/src/*"
            "$root/cfs/osal/src/os/portable/*"
            "$root/cfs/osal/src/os/shire/src/*"
            "$root/cfs/osal/src/bsp/shared/src/*"
            "$root/cfs/osal/src/bsp/generic-linux/src/*"
            "$root/cfs/psp/fsw/shared/src/*"
            "$root/cfs/psp/fsw/pc-linux/src/*"
            "$root/cfs/psp/fsw/shire/src/*"
            "$root/cfs/psp/fsw/modules/eeprom_mmap_file/*"
            "$root/cfs/psp/fsw/modules/iodriver/src/*"
            "$root/cfs/psp/fsw/modules/linux_sysmon/*"
            "$root/cfs/psp/fsw/modules/port_notimpl/*"
            "$root/cfs/psp/fsw/modules/ram_notimpl/*"
        )
        ;;
    component-sim:*)
        component=${scope#component-sim:}
        [[ "$component" =~ ^[a-zA-Z0-9_-]+$ ]] || usage
        scope_patterns=("$root/comp/$component/sim/*")
        ;;
    simulith)
        scope_patterns=("$root/simulith/src/*")
        ;;
    *) usage ;;
esac

# GCC may place an unexecuted basic block on a source line that another block
# executes (notably the FD_ZERO macro). LCOV reports that compiler-generated
# shape as an "inconsistent" branch/line count. Ignore only that diagnostic;
# profile corruption, negative counters, and every other error remain fatal.
coverage_common=(--branch-coverage --mcdc-coverage
                 --ignore-errors inconsistent,inconsistent)
lcov_common=("${coverage_common[@]}" --rc geninfo_unexecuted_blocks=1)

if [[ "$phase" == initial ]]; then
    lcov --capture --initial --directory "$build_dir" "${lcov_common[@]}" \
        --output-file "$output_dir/initial-primary.info"

    if [[ -n "$supplement_dir" ]]; then
        supplement_dir=$(realpath "$supplement_dir")
        lcov --capture --initial --directory "$supplement_dir" "${lcov_common[@]}" \
            --output-file "$output_dir/initial-supplement-raw.info"
        lcov --extract "$output_dir/initial-supplement-raw.info" \
            "${supplement_patterns[@]}" "${lcov_common[@]}" \
            --output-file "$output_dir/initial-supplement.info"
        lcov --add-tracefile "$output_dir/initial-primary.info" \
            --add-tracefile "$output_dir/initial-supplement.info" \
            "${lcov_common[@]}" --output-file "$output_dir/initial.info"
    else
        cp "$output_dir/initial-primary.info" "$output_dir/initial.info"
    fi
    exit 0
fi

[[ "$phase" == report ]] || usage
[[ -f "$output_dir/initial.info" ]] || {
    echo "missing initial trace: $output_dir/initial.info" >&2
    exit 1
}

lcov --capture --directory "$build_dir" "${lcov_common[@]}" \
    --output-file "$output_dir/executed.info"
lcov --add-tracefile "$output_dir/initial.info" \
    --add-tracefile "$output_dir/executed.info" "${lcov_common[@]}" \
    --output-file "$output_dir/combined.info"
lcov --extract "$output_dir/combined.info" "${scope_patterns[@]}" \
    "${lcov_common[@]}" --output-file "$output_dir/coverage_mcdc.info"

# Codecov consumes the standard LCOV records. Keep the LCOV MC/DC extension in
# the downloadable artifact and remove only those records from its input.
awk '!/^(MCDC|MCF|MCH):/' "$output_dir/coverage_mcdc.info" \
    > "$output_dir/coverage_filtered.info"

genhtml "$output_dir/coverage_mcdc.info" "${coverage_common[@]}" \
    --output-directory "$output_dir/report"

echo
lcov --summary "$output_dir/coverage_mcdc.info" "${lcov_common[@]}"
echo "MC/DC HTML: $output_dir/report/index.html"
echo "View in Firefox: firefox \"file://$output_dir/report/index.html\""
echo "Codecov trace: $output_dir/coverage_filtered.info"
