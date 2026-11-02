#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p secrets data mosquitto/data logs
# Public configuration must remain readable by non-root container users.
chmod 755 config mosquitto/config
chmod 644 config/*.yaml mosquitto/config/acl mosquitto/config/mosquitto.conf
[ -e .env ] || cp .env.example .env
for name in mqtt-password observer-password admin-token db-admin-password db-writer-password db-reader-password; do
  [ -s "secrets/$name" ] || openssl rand -hex 32 > "secrets/$name"
done
# Local certificates for encrypted OPC UA; URI matches the server identity.
if [ ! -s secrets/server.der ]; then
 openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj '/CN=CNC Synthetic Simulator' -addext 'subjectAltName=URI:urn:digitheta:cnc-simulator:server,DNS:localhost,DNS:cnc-simulator,IP:127.0.0.1' -keyout secrets/server-key.pem -out secrets/server.pem 2>/dev/null
 openssl x509 -in secrets/server.pem -outform der -out secrets/server.der
 openssl rsa -in secrets/server-key.pem -outform der -out secrets/server-key.der 2>/dev/null
fi
if [ ! -s secrets/client.der ]; then
 openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj '/CN=CNC Test Client' -addext 'subjectAltName=URI:urn:cnc:test-client,DNS:localhost' -keyout secrets/client-key.pem -out secrets/client.pem 2>/dev/null
 openssl x509 -in secrets/client.pem -outform der -out secrets/client.der
fi
# Bind-mounted secrets are readable by the two unprivileged container UIDs.
chmod 700 secrets
chmod 644 secrets/*
docker run --rm --user 0 --entrypoint sh -v "$PWD/secrets:/secrets:ro" -v "$PWD/mosquitto/config:/config" -v "$PWD/data:/data" -v "$PWD/mosquitto/data:/broker-data" eclipse-mosquitto:2.1.2@sha256:6f8d8a947c506f8a2290ec65cd4bd2bc7cb4d43fb5f6271f861cb013e2ef9797 -c 'rm -f /config/passwords.new && mosquitto_passwd -b -c /config/passwords.new simulator "$(cat /secrets/mqtt-password)" && mosquitto_passwd -b /config/passwords.new observer "$(cat /secrets/observer-password)" && mv /config/passwords.new /config/passwords && chown 1883:1883 /config/passwords && chmod 600 /config/passwords && chown 10001:10001 /data && chown 1883:1883 /broker-data'
# Only individual files are mounted; the host secret directory stays private.
chmod 700 secrets
printf 'Bootstrap complete. Start with: docker compose up -d --build\n'
