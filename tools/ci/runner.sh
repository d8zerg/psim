#!/bin/sh
# Runs `task ci` for one commit in a dedicated clone (step 1.3, ADR-034), so the working tree stays
# free for editing while the pipeline checks exactly the committed content.
#   runner.sh <commit> [background]
# The clone .cache/ci/repo keeps its own build directories (incremental builds between runs) and
# shares the Conan, ccache and Trivy caches of the main tree. One pipeline runs at a time; a
# background run for a commit that is no longer HEAD is skipped as superseded.
# Records in .cache/ci: <sha>.running (pid), then <sha>.ok or <sha>.failed, and the log <sha>.log.
set -eu
main=$(git rev-parse --show-toplevel)
ci="$main/.cache/ci"
clone="$ci/repo"
sha=$(git -C "$main" rev-parse "$1^{commit}")
mode=${2:-foreground}
mkdir -p "$ci" "$main/.cache/debian12" "$main/.cache/trivy"

echo $$ > "$ci/$sha.running"
trap 'rm -f "$ci/$sha.running"' EXIT
exec 9>"$ci/runner.lock"
flock 9

if [ "$mode" = background ] && [ "$(git -C "$main" rev-parse HEAD)" != "$sha" ]; then
  echo "ci: $sha superseded by a newer commit, skipped" >> "$ci/runner.log"
  exit 0
fi
[ -f "$ci/$sha.ok" ] && exit 0
rm -f "$ci/$sha.failed"

if [ ! -d "$clone/.git" ]; then
  git clone --quiet --local --no-checkout "$main" "$clone"
fi
git -C "$clone" fetch --quiet --force --update-head-ok "$main" \
  '+refs/heads/*:refs/heads/*' '+refs/remotes/origin/*:refs/remotes/origin/*' '+refs/tags/*:refs/tags/*'
git -C "$clone" checkout --quiet --detach --force "$sha"
git -C "$clone" clean -fdq   # untracked files; ignored build and cache directories are kept
mkdir -p "$clone/.cache"
for shared in debian12 trivy; do ln -sfn "$main/.cache/$shared" "$clone/.cache/$shared"; done

# A background run leaves CPUs for interactive work on the stand.
jobs=""
[ "$mode" = background ] && jobs="JOBS=8"
start=$(date +%s)
if (cd "$clone" && task ci $jobs) > "$ci/$sha.log" 2>&1 < /dev/null; then
  echo "passed $(date -u +%Y-%m-%dT%H:%M:%SZ) in $(( $(date +%s) - start ))s" > "$ci/$sha.ok"
  result="passed"
else
  echo "failed $(date -u +%Y-%m-%dT%H:%M:%SZ), log: $ci/$sha.log" > "$ci/$sha.failed"
  result="FAILED (log: .cache/ci/$sha.log)"
fi
echo "ci: $sha $result" >> "$ci/runner.log"
if [ "$mode" = background ] && command -v notify-send >/dev/null 2>&1; then
  notify-send "PSIM ci: $(git -C "$main" log -1 --format=%h "$sha") $result" >/dev/null 2>&1 || true
fi
[ "$result" = passed ]
