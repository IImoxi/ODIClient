#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
stage=$(mktemp -d /tmp/odiclient-install-test.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
bash install.sh "$stage" > "$stage/install.log"
package=1.0.0/x86_64
target=$stage/ODIClient/$package
cmp -- build/libblank-client-menu.so "$target/libblank-client-menu.so"
cmp -- build/build-info.txt "$target/build-info.txt"
binary_sha=$(sha256sum "$target/libblank-client-menu.so")
expected_sha=$(sed -n 's/^binary_sha256=//p' "$target/build-info.txt")
[[ "${binary_sha%% *}" == "$expected_sha" ]]
strings "$target/libblank-client-menu.so" > "$stage/strings.txt"
if rg -q -F "IImoxi  /  build " "$stage/strings.txt"; then
    echo 'Build identity still appears in the menu footer.' >&2
    exit 1
fi
if rg -q 'Atmosphere lookup|Half resolution atmosphere|Reduced atmosphere samples' "$stage/strings.txt"; then
    echo 'Removed sky quality controls still appear in the installed binary.' >&2
    exit 1
fi
# The same-folder install path also performs identity verification.
bash install.sh "$(cd -- .. && pwd -P)" > "$stage/self-install.log"
# A corrupted identity must be rejected before it can replace an installed binary.
mkdir -p "$stage/source/$package"
cp -- install.sh "$stage/source/"
cp -- "$package/libblank-client-menu.so" "$stage/source/$package/"
printf 'source_id=bad\nbinary_sha256=bad\n' > "$stage/source/$package/build-info.txt"
if bash "$stage/source/install.sh" "$stage" > "$stage/reject.log" 2>&1; then
    echo 'Installer accepted a mismatched package.' >&2
    exit 1
fi
rg -q 'does not match build-info.txt' "$stage/reject.log"
cmp -- build/libblank-client-menu.so "$target/libblank-client-menu.so"
echo 'Build identity, both install paths and mismatched-package rejection passed.'
