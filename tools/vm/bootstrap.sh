#!/bin/sh
set -eu
. /etc/os-release
test "$ID" = debian && test "$VERSION_ID" = 13
test "$(dpkg --print-architecture)" = amd64
cd "$1"
sha256sum --check packages.sha256
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install --yes --no-install-recommends ./*.deb
systemctl enable --now docker
docker version --format '{{.Server.Version}}'
docker compose version --short
sync
