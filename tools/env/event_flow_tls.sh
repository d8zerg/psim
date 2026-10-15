#!/bin/sh
# Isolated development identities; production enrollment belongs to the installer.
set -eu
umask 077
target=.cache/event-flow
mkdir -p "$target"
chmod 755 "$target"
if [ ! -f "$target/server.pem" ] || ! openssl x509 -checkend 86400 -noout -in "$target/server.pem" >/dev/null 2>&1; then
  chmod u+w "$target"/*.key 2>/dev/null || true
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 7 -subj /CN=psim-event-flow-ca -keyout "$target/ca.key" -out "$target/ca.pem" >/dev/null 2>&1
  for identity in server client; do
    openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj "/CN=psim-event-flow-$identity" -keyout "$target/$identity.key" -out "$target/$identity.csr" >/dev/null 2>&1
    if [ "$identity" = server ]; then
      printf '%s\n' 'subjectAltName=DNS:connector-gateway,DNS:localhost,URI:psim://service/connector-gateway' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    else
      printf '%s\n' 'subjectAltName=URI:psim://connector/22222222-2222-4222-8222-222222222222' 'extendedKeyUsage=clientAuth' > "$target/$identity.ext"
    fi
    openssl x509 -req -days 7 -in "$target/$identity.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" -CAcreateserial -extfile "$target/$identity.ext" -out "$target/$identity.pem" >/dev/null 2>&1
  done
fi
chmod u+w "$target/postgres.secret" 2>/dev/null || true
printf %s psim-dev-psim > "$target/postgres.secret"
chmod 644 "$target"/*.pem
chmod 400 "$target"/*.key "$target/postgres.secret"
