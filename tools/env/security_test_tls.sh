#!/bin/sh
# Throwaway transport fixtures; the platform does not issue production certificates.
set -eu
umask 077
target=.cache/security-tls
mkdir -p "$target"
chmod 755 "$target"
if [ -f "$target/server.pem" ] && openssl x509 -checkend 86400 -noout -in "$target/server.pem" >/dev/null 2>&1; then
  chmod 400 "$target/server.key" "$target/client.key" "$target/ca.key"
  chmod 644 "$target/server-combined.pem"
  exit 0
fi
chmod u+w "$target"/*.key "$target/server-combined.pem" 2>/dev/null || true
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 7 -subj /CN=psim-security-test-ca \
  -keyout "$target/ca.key" -out "$target/ca.pem" >/dev/null 2>&1
for identity in server client; do
  openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$target/$identity.key" >/dev/null 2>&1
  openssl req -new -key "$target/$identity.key" -subj "/CN=psim-test-$identity" -out "$target/$identity.csr" >/dev/null 2>&1
  cat > "$target/$identity.ext" <<'CONF'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth,clientAuth
CONF
  if [ "$identity" = server ]; then
    printf 'subjectAltName=DNS:localhost,DNS:kafka-tls,DNS:postgres-tls,URI:psim://service/test-infrastructure\n' >> "$target/$identity.ext"
  else
    printf 'subjectAltName=URI:psim://service/psim-services\n' >> "$target/$identity.ext"
  fi
  openssl x509 -req -days 7 -in "$target/$identity.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" \
    -CAcreateserial -extfile "$target/$identity.ext" -out "$target/$identity.pem" >/dev/null 2>&1
done
cat "$target/server.key" "$target/server.pem" > "$target/server-combined.pem"
cat > "$target/kafka-client.properties" <<'CONF'
security.protocol=SSL
ssl.truststore.type=PEM
ssl.truststore.location=/tls/ca.pem
ssl.keystore.type=PEM
ssl.keystore.location=/tls/server-combined.pem
ssl.endpoint.identification.algorithm=https
CONF
chmod 644 "$target/ca.pem" "$target/server.pem" "$target/client.pem" "$target/kafka-client.properties"
chmod 400 "$target/server.key" "$target/client.key" "$target/ca.key"
# The throwaway Java keystore is read by the image's fixed UID; client keys remain owner-only.
chmod 644 "$target/server-combined.pem"
