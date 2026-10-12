#!/bin/sh
# Ephemeral test certificates live in the ignored cache and are never release credentials.
set -eu
target=.cache/valkey-tls
mkdir -p "$target"
if [ -f "$target/server.pem" ] && openssl x509 -checkend 86400 -noout -in "$target/server.pem" >/dev/null 2>&1; then
  exit 0
fi
openssl req -x509 -newkey rsa:2048 -nodes -days 7 -subj /CN=psim-test-ca \
  -keyout "$target/ca.key" -out "$target/ca.pem" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -subj /CN=valkey-tls \
  -keyout "$target/server.key" -out "$target/server.csr" >/dev/null 2>&1
cat > "$target/server.ext" <<'CONF'
subjectAltName=DNS:valkey-tls
extendedKeyUsage=serverAuth
CONF
openssl x509 -req -days 7 -in "$target/server.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" \
  -CAcreateserial -extfile "$target/server.ext" -out "$target/server.pem" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -subj /CN=psim-test-client \
  -keyout "$target/client.key" -out "$target/client.csr" >/dev/null 2>&1
printf 'extendedKeyUsage=clientAuth\n' > "$target/client.ext"
openssl x509 -req -days 7 -in "$target/client.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" \
  -CAcreateserial -extfile "$target/client.ext" -out "$target/client.pem" >/dev/null 2>&1
# The test server's unprivileged container user must be able to read these throwaway keys.
chmod 644 "$target/server.key" "$target/client.key"
