#!/bin/sh
# Runs built artifacts on a target platform image without the toolchain (ADR-032):
# every test executable (*_test) and `psimctl version`. Invoked by `task cpp:runtime-test`.
# Usage: runtime_test.sh <platform-name> <build-dir>
set -eu
platform=$1
build=$2
echo "== runtime: $platform ($(. /etc/os-release && echo "$PRETTY_NAME"), $(ldd --version | head -1))"
tests=$(find "$build" -path '*/CMakeFiles' -prune -o -type f -name '*_test' -perm -u+x -print | sort)
[ -n "$tests" ] || { echo "no test executables in $build" >&2; exit 1; }
for t in $tests; do
  "$t" --gtest_brief=1 >/tmp/psim-test.log 2>&1 || { cat /tmp/psim-test.log; echo "FAIL: $t on $platform" >&2; exit 1; }
  echo "ok: $t"
done
psimctl=$(find "$build" -path '*/CMakeFiles' -prune -o -type f -name psimctl -print | head -1)
"$psimctl" version
if ldd "$psimctl" | grep -q 'libc++'; then
  echo "FAIL: psimctl depends on a shared libc++" >&2
  exit 1
fi
echo "ok: psimctl has no shared libc++ dependency on $platform"
