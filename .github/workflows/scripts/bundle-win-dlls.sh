#!/usr/bin/env bash
#
# Bundle required DLLs for Windows builds (if any are needed)
#
# This script detects missing DLLs for the QEMU binaries and copies them
# from the MinGW64 bin directory. With proper static linking, no DLLs
# should be needed, but this provides a safety net.
#

set -euo pipefail

INSTALL_BIN_DIR="${1:-install/qemu/bin}"
MINGW_BIN_DIR="/mingw64/bin"

echo "Checking for missing DLLs in ${INSTALL_BIN_DIR}..."

# Collect all missing DLLs from all executables
missing_dlls=()

for exe in "${INSTALL_BIN_DIR}"/*.exe; do
    if [[ -f "${exe}" ]]; then
        echo "Analyzing: $(basename "${exe}")"
        # Use ldd to find dependencies marked as "not found"
        # Also filter for DLLs that exist in mingw64/bin (our responsibility)
        while IFS= read -r line; do
            # Extract the DLL name from lines like "libfoo.dll => not found"
            if [[ "${line}" =~ ^[[:space:]]*([^[:space:]]+\.dll)[[:space:]]*'=>'[[:space:]]*'not found' ]]; then
                dll="${BASH_REMATCH[1]}"
                # Check if this DLL exists in our MinGW bin dir
                if [[ -f "${MINGW_BIN_DIR}/${dll}" ]]; then
                    missing_dlls+=("${dll}")
                fi
            fi
        done < <(ldd "${exe}" 2>/dev/null || true)
    fi
done

# Remove duplicates
if [[ ${#missing_dlls[@]} -gt 0 ]]; then
    readarray -t missing_dlls < <(printf '%s\n' "${missing_dlls[@]}" | sort -u)
fi

# Copy missing DLLs
if [[ ${#missing_dlls[@]} -eq 0 ]]; then
    echo ""
    echo "No missing DLLs found - static linking appears to be working correctly."
    echo ""
else
    echo ""
    echo "Found ${#missing_dlls[@]} missing DLLs. Copying from ${MINGW_BIN_DIR}..."

    for dll in "${missing_dlls[@]}"; do
        src="${MINGW_BIN_DIR}/${dll}"
        if [[ -f "${src}" ]]; then
            cp -v "${src}" "${INSTALL_BIN_DIR}/"
        else
            echo "WARNING: ${dll} not found in ${MINGW_BIN_DIR}"
        fi
    done

    echo ""
    echo "DLL bundling complete."

    # Re-check for any still-missing dependencies (recursive deps)
    echo ""
    echo "Re-checking dependencies after bundling..."
    still_missing=0
    for exe in "${INSTALL_BIN_DIR}"/*.exe; do
        if [[ -f "${exe}" ]]; then
            missing=$(ldd "${exe}" 2>/dev/null | grep -i "not found" || true)
            if [[ -n "${missing}" ]]; then
                echo "WARNING: Still missing deps for $(basename "${exe}"):"
                echo "${missing}"
                still_missing=1
            fi
        fi
    done

    if [[ ${still_missing} -eq 0 ]]; then
        echo "All dependencies resolved."
    fi
fi
