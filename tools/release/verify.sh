#!/bin/sh
# Verifies a signed release directory (step 1.3, ADR-034, FF-14, SR-24); the installer (step 9.2)
# runs the same checks before installation and upgrade. Needs only openssl and sha256sum.
#   1. SHA256SUMS is signed by the trusted public key (never the key shipped in the directory);
#   2. every file matches its checksum and no unlisted file is present;
#   3. every artifact (.deb, image .tar) has an SBOM <artifact>.cdx.json describing exactly its hash.
# Usage: verify.sh <release-dir> <trusted-public-key.pem>
set -eu
dir=$1
trusted=$2
fail() { echo "FAIL: $*" >&2; exit 1; }

[ -f "$dir/SHA256SUMS" ] && [ -f "$dir/SHA256SUMS.sig" ] || fail "$dir: SHA256SUMS or SHA256SUMS.sig missing"
openssl pkeyutl -verify -pubin -inkey "$trusted" -rawin -in "$dir/SHA256SUMS" -sigfile "$dir/SHA256SUMS.sig" >/dev/null \
  || fail "signature of SHA256SUMS does not match the trusted key $trusted"
(cd "$dir" && sha256sum --quiet --strict -c SHA256SUMS) || fail "checksum mismatch"

artifacts=0
for f in "$dir"/*; do
  name=$(basename "$f")
  case "$name" in SHA256SUMS|SHA256SUMS.sig|psim-signing.pub.pem) continue ;; esac
  grep -q "  $name\$" "$dir/SHA256SUMS" || fail "$name is not listed in SHA256SUMS"
  case "$name" in
    *.deb|*.tar)
      artifacts=$((artifacts + 1))
      sbom="$f.cdx.json"
      [ -f "$sbom" ] || fail "$name has no SBOM ($name.cdx.json)"
      hash=$(sha256sum "$f" | cut -d' ' -f1)
      grep -q "\"content\": \"$hash\"" "$sbom" || fail "SBOM of $name does not describe its SHA-256"
      grep -q '"bomFormat": "CycloneDX"' "$sbom" || fail "SBOM of $name is not CycloneDX"
      ;;
  esac
done
[ "$artifacts" -gt 0 ] || fail "$dir has no artifacts"
echo "verified: $artifacts artifacts with SBOMs, signature by $(openssl pkey -pubin -in "$trusted" -outform DER | sha256sum | cut -c1-16)"
