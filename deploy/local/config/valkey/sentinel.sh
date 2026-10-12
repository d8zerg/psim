#!/bin/sh
# Each fixture starts from a clean topology; Sentinel rewrites its configuration in tmpfs.
set -eu
cat > /data/sentinel.conf <<'CONF'
port 26379
bind 0.0.0.0
protected-mode no
requirepass psim-dev-sentinel
sentinel resolve-hostnames yes
sentinel announce-hostnames yes
sentinel monitor psim valkey-primary 6379 2
sentinel auth-pass psim psim-dev-valkey
sentinel down-after-milliseconds psim 1000
sentinel failover-timeout psim 10000
sentinel parallel-syncs psim 1
CONF
exec valkey-server /data/sentinel.conf --sentinel
