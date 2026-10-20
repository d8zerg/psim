#!/bin/sh
# Run inside the isolated broker with its synthetic administrative identity.
set -eu
bin=/opt/kafka/bin
for topic in psim.catalog.v1 psim.config.v1 psim.audit.v1 psim.events.normalized.v1 psim.signals.v1 psim.incidents.events.v1 psim.commands.v1 psim.response.events.v1; do
  partitions=2
  if [ "$topic" = psim.config.v1 ]; then partitions=1; fi
  "$bin/kafka-topics.sh" --bootstrap-server kafka-tls:19092 --command-config /tls/kafka-client.properties --create --if-not-exists --topic "$topic" --partitions "$partitions" --replication-factor 1
done
"$bin/kafka-acls.sh" --bootstrap-server kafka-tls:19092 --command-config /tls/kafka-client.properties --add --allow-principal User:CN=psim-rule-replay --operation Read --topic psim.events.normalized.v1 --topic psim.catalog.v1 --topic psim.config.v1 --topic psim.audit.v1
"$bin/kafka-acls.sh" --bootstrap-server kafka-tls:19092 --command-config /tls/kafka-client.properties --add --allow-principal User:ANONYMOUS --operation Write --topic psim.config.v1 --topic psim.audit.v1
"$bin/kafka-acls.sh" --bootstrap-server kafka-tls:19092 --command-config /tls/kafka-client.properties --add --allow-principal User:ANONYMOUS --operation IdempotentWrite --cluster

"$bin/kafka-acls.sh" --bootstrap-server kafka-tls:19092 --command-config /tls/kafka-client.properties --add --allow-principal User:CN=psim-rule-replay --operation Describe --group rule-replay- --resource-pattern-type prefixed
