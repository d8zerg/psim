#!/bin/sh
# Synthetic identities for isolated rule-management checks.
set -eu
umask 077
target=.cache/rules
mkdir -p "$target" .cache/processing
chmod 755 "$target"
for identity in server replay; do
  if [ "$identity" = server ]; then
    authority=.cache/event-flow
    subject=psim-rule-management
  else
    authority=.cache/security-tls
    subject=psim-rule-replay
  fi
  if [ ! -f "$target/$identity.pem" ] || ! openssl verify -CAfile "$authority/ca.pem" "$target/$identity.pem" >/dev/null 2>&1 || ! openssl x509 -checkend 86400 -noout -in "$target/$identity.pem" >/dev/null 2>&1; then
    chmod u+w "$target/$identity.key" 2>/dev/null || true
    openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj "/CN=$subject" -keyout "$target/$identity.key" -out "$target/$identity.csr" >/dev/null 2>&1
    if [ "$identity" = server ]; then
      printf '%s\n' 'subjectAltName=DNS:localhost,DNS:rule-management,DNS:rule-manager-2,URI:psim://service/correlation-engine' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    else
      printf '%s\n' 'subjectAltName=URI:psim://service/correlation-engine' 'extendedKeyUsage=clientAuth' > "$target/$identity.ext"
    fi
    serial=$(openssl rand -hex 16)
    openssl x509 -req -days 7 -set_serial "0x$serial" -in "$target/$identity.csr" -CA "$authority/ca.pem" -CAkey "$authority/ca.key" -extfile "$target/$identity.ext" -out "$target/$identity.pem" >/dev/null 2>&1
  fi
  chmod 400 "$target/$identity.key"
  chmod 644 "$target/$identity.pem"
done
for fixture in pg clickhouse; do
  chmod u+w "$target/$fixture.secret" 2>/dev/null || true
  printf 'psim-dev-rule-%s' "$fixture" > "$target/$fixture.secret"
  chmod 400 "$target/$fixture.secret"
done
chmod u+w .cache/processing/clickhouse.password 2>/dev/null || true
printf %s psim-dev-clickhouse > .cache/processing/clickhouse.password
chmod 400 .cache/processing/clickhouse.password
