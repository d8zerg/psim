#!/bin/sh
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sh "$tools/release/verify.sh" "$1" "$2"
python3 -I "$tools/deploy/delivery.py" --release "$1" --trusted-key "$2" >/dev/null
echo "verified: lifecycle manifest, complete signed inventory and compatibility metadata"
