#!/bin/sh
# Signs a release directory (step 1.3, ADR-034, SR-24): SHA256SUMS lists every artifact and SBOM,
# SHA256SUMS.sig is its Ed25519 signature, psim-signing.pub.pem is the public key for reference
# (verification trusts only a key obtained separately, see verify.sh).
# Usage: sign.sh <release-dir> <private-key.pem>
set -eu
dir=$1
key=$2
cd "$dir"
rm -f SHA256SUMS SHA256SUMS.sig psim-signing.pub.pem
find . -maxdepth 1 -type f ! -name 'SHA256SUMS*' ! -name 'psim-signing.pub.pem' -printf '%f\n' | LC_ALL=C sort \
  | xargs -r sha256sum > SHA256SUMS
openssl pkeyutl -sign -inkey "$key" -rawin -in SHA256SUMS -out SHA256SUMS.sig
openssl pkey -in "$key" -pubout -out psim-signing.pub.pem
echo "signed: $(wc -l < SHA256SUMS) files in $dir, key $(openssl pkey -in "$key" -pubout -outform DER | sha256sum | cut -c1-16)"
