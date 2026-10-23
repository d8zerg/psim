#!/bin/sh
# Isolated development identities; production enrollment belongs to the installer.
set -eu
umask 077
target=${1:-.cache/event-flow}
mkdir -p "$target"
chmod 755 "$target"
if [ ! -f "$target/server.pem" ] || ! openssl x509 -in "$target/ca.pem" -noout -ext keyUsage 2>/dev/null | grep -q 'Certificate Sign' || ! openssl x509 -checkhost toxiproxy -noout -in "$target/server.pem" >/dev/null 2>&1 || ! openssl x509 -checkend 86400 -noout -in "$target/server.pem" >/dev/null 2>&1; then
  chmod u+w "$target"/*.key 2>/dev/null || true
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -days 7 -subj /CN=psim-event-flow-ca -addext 'basicConstraints=critical,CA:TRUE' -addext 'keyUsage=critical,keyCertSign,cRLSign' -keyout "$target/ca.key" -out "$target/ca.pem" >/dev/null 2>&1
  for identity in server client admin; do
    openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj "/CN=psim-event-flow-$identity" -keyout "$target/$identity.key" -out "$target/$identity.csr" >/dev/null 2>&1
    if [ "$identity" = server ]; then
      printf '%s\n' 'subjectAltName=DNS:connector-gateway,DNS:gateway-2,DNS:toxiproxy,DNS:localhost,URI:psim://service/connector-gateway' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = admin ]; then
      printf '%s\n' 'subjectAltName=URI:psim://service/gateway-admin' 'extendedKeyUsage=clientAuth' > "$target/$identity.ext"
    else
      printf '%s\n' 'subjectAltName=URI:psim://connector/22222222-2222-4222-8222-222222222222' 'extendedKeyUsage=clientAuth' > "$target/$identity.ext"
    fi
    openssl x509 -req -days 7 -in "$target/$identity.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" -CAcreateserial -extfile "$target/$identity.ext" -out "$target/$identity.pem" >/dev/null 2>&1
  done
fi
if [ ! -f "$target/admin.pem" ]; then
  openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj /CN=psim-gateway-admin -keyout "$target/admin.key" -out "$target/admin.csr" >/dev/null 2>&1
  printf '%s\n' 'subjectAltName=URI:psim://service/gateway-admin' 'extendedKeyUsage=clientAuth' > "$target/admin.ext"
  openssl x509 -req -days 7 -in "$target/admin.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" -CAcreateserial -extfile "$target/admin.ext" -out "$target/admin.pem" >/dev/null 2>&1
fi
for identity in api-server catalog-server catalog-client incident-server history-server command-server response-server notification-server audit-server reports-server rule-server; do
  if [ ! -f "$target/$identity.pem" ] || ! openssl verify -CAfile "$target/ca.pem" "$target/$identity.pem" >/dev/null 2>&1 || ! openssl x509 -checkend 86400 -noout -in "$target/$identity.pem" >/dev/null 2>&1; then
    chmod u+w "$target/$identity.key" 2>/dev/null || true
    openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc -subj /CN=psim-resource-catalog -keyout "$target/$identity.key" -out "$target/$identity.csr" >/dev/null 2>&1
    if [ "$identity" = api-server ]; then
      printf '%s\n' 'subjectAltName=DNS:api-gateway,DNS:api-2,DNS:localhost,URI:psim://service/api-gateway' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = history-server ]; then
      printf '%s\n' 'subjectAltName=DNS:event-history,DNS:history-2,DNS:localhost,URI:psim://service/event-history' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = rule-server ]; then
      printf '%s\n' 'subjectAltName=DNS:rule-management,DNS:rule-manager-2,DNS:localhost,URI:psim://service/correlation-engine' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = reports-server ]; then
      printf '%s\n' 'subjectAltName=DNS:projection-service,DNS:reports-2,DNS:localhost,URI:psim://service/projection-service' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = audit-server ]; then
      printf '%s\n' 'subjectAltName=DNS:audit-service,DNS:audit-2,DNS:localhost,URI:psim://service/audit-service' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = notification-server ]; then
      printf '%s\n' 'subjectAltName=DNS:notification-service,DNS:notification-2,DNS:notification-fixture,DNS:localhost,URI:psim://service/notification-service' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = response-server ]; then
      printf '%s\n' 'subjectAltName=DNS:response-engine,DNS:response-2,DNS:response-handler,DNS:localhost,URI:psim://service/response-engine' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = command-server ]; then
      printf '%s\n' 'subjectAltName=DNS:command-service,DNS:command-2,DNS:localhost,URI:psim://service/command-service' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = incident-server ]; then
      printf '%s\n' 'subjectAltName=DNS:incident-service,DNS:incident-2,DNS:localhost,URI:psim://service/incident-service' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    elif [ "$identity" = catalog-server ]; then
      printf '%s\n' 'subjectAltName=DNS:resource-catalog,DNS:catalog-2,DNS:localhost,URI:psim://service/resource-catalog' 'extendedKeyUsage=serverAuth' > "$target/$identity.ext"
    else
      printf '%s\n' 'subjectAltName=URI:psim://service/resource-catalog' 'extendedKeyUsage=clientAuth' > "$target/$identity.ext"
    fi
    openssl x509 -req -days 7 -in "$target/$identity.csr" -CA "$target/ca.pem" -CAkey "$target/ca.key" -CAcreateserial -extfile "$target/$identity.ext" -out "$target/$identity.pem" >/dev/null 2>&1
  fi
done
if ! printf %s psim-dev-psim | cmp -s - "$target/postgres.secret"; then
  chmod u+w "$target/postgres.secret" 2>/dev/null || true
  printf %s psim-dev-psim > "$target/postgres.secret"
fi
if ! printf %s psim-dev-valkey | cmp -s - "$target/redis.secret"; then
  chmod u+w "$target/redis.secret" 2>/dev/null || true
  printf %s psim-dev-valkey > "$target/redis.secret"
fi
for file in "$target"/*.pem; do
  [ "$(stat -c %a "$file")" = 644 ] || chmod 644 "$file"
done
if ! printf %s psim-dev-introspection | cmp -s - "$target/introspection.secret"; then
  chmod u+w "$target/introspection.secret" 2>/dev/null || true
  printf %s psim-dev-introspection > "$target/introspection.secret"
fi
for file in "$target"/*.key "$target/postgres.secret" "$target/redis.secret" "$target/introspection.secret"; do
  [ "$(stat -c %a "$file")" = 400 ] || chmod 400 "$file"
done
