#!/bin/sh
# Throwaway native TLS fixtures, never used for platform delivery.
set -eu
umask 077
target=.cache/clickhouse-tls
mkdir -p "$target"
chmod 755 "$target"
if [ -f "$target/server.pem" ] && openssl x509 -checkend 86400 -noout -in "$target/server.pem" >/dev/null 2>&1; then
  exit 0
fi
chmod u+w "$target"/* 2>/dev/null || true
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 7 -subj /CN=psim-clickhouse-test-ca \
  -keyout "$target/ca.key" -out "$target/ca.pem" >/dev/null 2>&1
for identity in server client; do
  openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$target/$identity.key" >/dev/null 2>&1
  openssl req -new -key "$target/$identity.key" -subj "/CN=psim-clickhouse-$identity" -out "$target/$identity.csr" >/dev/null 2>&1
  cat > "$target/$identity.ext" <<'CONF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth,clientAuth
CONF
  if [ "$identity" = server ]; then
    printf 'subjectAltName=DNS:clickhouse-1,DNS:clickhouse-2,DNS:toxiproxy,URI:psim://service/clickhouse\n' >> "$target/$identity.ext"
  else
    printf 'subjectAltName=URI:psim://service/ch-loader\n' >> "$target/$identity.ext"
  fi
  openssl x509 -req -days 7 -in "$target/$identity.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" \
    -CAcreateserial -extfile "$target/$identity.ext" -out "$target/$identity.pem" >/dev/null 2>&1
done
cat "$target/server.key" "$target/server.pem" > "$target/server-combined.pem"
printf %s psim-dev-clickhouse > "$target/admin.password"
printf %s psim-dev-ch-loader > "$target/loader.password"
printf %s psim-dev-ch-reader > "$target/reader.password"
chmod 644 "$target/ca.pem" "$target/server.pem" "$target/client.pem" "$target/server-combined.pem"
chmod 400 "$target/ca.key" "$target/server.key" "$target/client.key" "$target"/*.password
