#!/bin/sh
# Runs `task ci` for one commit in a dedicated clone (ADR-034, ADR-036): checks exactly the committed
# content - files that exist only in the working tree cannot hide a problem - while the working
# tree stays free for editing. Optional; invoked by `task ci:run [SHA=<commit>]`.
# The clone .cache/ci/repo keeps its own build directories (incremental builds between runs) and
# shares the Conan, ccache and Trivy caches of the main tree. One run at a time (lock).
# Records in .cache/ci: <sha>.running (pid), then <sha>.ok or <sha>.failed, and the log <sha>.log.
set -eu
main=$(git rev-parse --show-toplevel)
ci="$main/.cache/ci"
clone="$ci/repo"
sha=$(git -C "$main" rev-parse "$1^{commit}")
mkdir -p "$ci" "$main/.cache/debian12" "$main/.cache/trivy"

echo $$ > "$ci/$sha.running"
trap 'rm -f "$ci/$sha.running"' EXIT
exec 9>"$ci/runner.lock"
flock 9
rm -f "$ci/$sha.ok" "$ci/$sha.failed"

if [ ! -d "$clone/.git" ]; then
  git clone --quiet --local --no-checkout "$main" "$clone"
fi
git -C "$clone" fetch --quiet --force --update-head-ok "$main" \
  '+refs/heads/*:refs/heads/*' '+refs/remotes/origin/*:refs/remotes/origin/*' '+refs/tags/*:refs/tags/*'
git -C "$clone" checkout --quiet --detach --force "$sha"
git -C "$clone" clean -fdq   # untracked files; ignored build and cache directories are kept
mkdir -p "$clone/.cache"
for shared in debian12 trivy; do ln -sfn "$main/.cache/$shared" "$clone/.cache/$shared"; done

echo "ci: running for $(git -C "$main" log -1 --format='%h %s' "$sha") in .cache/ci/repo (log .cache/ci/$sha.log)"
start=$(date +%s)
if (cd "$clone" && task ci) > "$ci/$sha.log" 2>&1 < /dev/null; then
  echo "passed $(date -u +%Y-%m-%dT%H:%M:%SZ) in $(( $(date +%s) - start ))s" > "$ci/$sha.ok"
  echo "ci: passed in $(( $(date +%s) - start ))s"
else
  echo "failed $(date -u +%Y-%m-%dT%H:%M:%SZ), log: $ci/$sha.log" > "$ci/$sha.failed"
  tail -n 20 "$ci/$sha.log"
  echo "ci: FAILED for $sha (log .cache/ci/$sha.log)" >&2
  exit 1
fi
