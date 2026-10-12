#!/bin/sh
# Runs built artifacts on a target platform image without the toolchain (ADR-032):
# Test executables (*_test), psimctl and the migration runner use the distribution's shared libraries.
# Invoked by `task cpp:runtime-test`.
# Usage: runtime_test.sh <platform-name> <build-dir>
set -eu
platform=$1
build=$2
echo "== runtime: $platform ($(. /etc/os-release && echo "$PRETTY_NAME"), $(ldd --version | head -1))"
# Component tests (*_component_test) need the containers of the local environment: task test:component.
tests=$(find "$build" -path '*/CMakeFiles' -prune -o -type f -name '*_test' ! -name '*_component_test' -perm -u+x -print | sort)
[ -n "$tests" ] || { echo "no test executables in $build" >&2; exit 1; }
for t in $tests; do
  "$t" --gtest_brief=1 >/tmp/psim-test.log 2>&1 || { cat /tmp/psim-test.log; echo "FAIL: $t on $platform" >&2; exit 1; }
  echo "ok: $t"
done
psimctl=$(find "$build" -path '*/CMakeFiles' -prune -o -type f -name psimctl -print | head -1)
"$psimctl" version
if ldd "$psimctl" | grep -q 'not found'; then
  echo "FAIL: psimctl has unresolved shared-library dependencies" >&2
  exit 1
fi
echo "ok: psimctl shared-library dependencies resolve on $platform"
migrator=$(find "$build" -path '*/CMakeFiles' -prune -o -type f -name psim-pg-migrate -print | head -1)
[ -n "$migrator" ] || { echo "migration runner missing in $build" >&2; exit 1; }
status=0
"$migrator" >/tmp/psim-migrate.log 2>&1 || status=$?
[ "$status" = 2 ] || { cat /tmp/psim-migrate.log; echo "FAIL: migration runner on $platform" >&2; exit 1; }
if ldd "$migrator" | grep -q 'not found'; then
  echo "FAIL: migration runner has unresolved shared-library dependencies" >&2
  exit 1
fi
echo "ok: psim-pg-migrate runs and shared-library dependencies resolve on $platform"
