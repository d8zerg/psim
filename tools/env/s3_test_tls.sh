#!/bin/sh
# Local fixture credentials and certificates; never use them for delivery.
set -eu
umask 077
target=.cache/s3-tls
mkdir -p "$target"
chmod 755 "$target"
if [ -f "$target/server.pem" ] && openssl x509 -checkend 86400 -noout -in "$target/server.pem" >/dev/null 2>&1; then
  exit 0
fi
chmod u+w "$target"/* 2>/dev/null || true
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 7 -subj /CN=psim-s3-fixture-ca \
  -keyout "$target/ca.key" -out "$target/ca.pem" >/dev/null 2>&1
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj /CN=psim-s3-fixture \
  -keyout "$target/server.key" -out "$target/server.csr" >/dev/null 2>&1
cat > "$target/server.ext" <<'CONF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:s3,DNS:toxiproxy,DNS:localhost,URI:psim://service/filestore
CONF
openssl x509 -req -days 7 -in "$target/server.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" \
  -CAcreateserial -extfile "$target/server.ext" -out "$target/server.pem" >/dev/null 2>&1
for role in admin filestore reader; do
  printf %s "psim-dev-$role" > "$target/$role.access"
  printf %s "psim-dev-$role-secret" > "$target/$role.secret"
done
chmod 644 "$target/ca.pem" "$target/server.pem"
chmod 400 "$target/ca.key" "$target/server.key" "$target"/*.access "$target"/*.secret
