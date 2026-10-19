#!/usr/bin/env python3
"""Send one event with a stable retry key over verified HTTPS."""
import argparse
import http.client
from pathlib import Path
import random
import ssl
import stat
import time
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("--ca", required=True)
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--event", required=True)
    args = parser.parse_args()
    url = urlsplit(args.url)
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.fragment:
        parser.error("a verified HTTPS URL without credentials is required")
    secret = Path(args.token_file)
    metadata = secret.stat()
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_mode & 0o077:
        parser.error("token must be a private regular file")
    token = secret.read_text()
    payload = Path(args.event).read_bytes()
    context = ssl.create_default_context(cafile=args.ca)
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    for attempt in range(8):
        connection = http.client.HTTPSConnection(url.hostname, url.port or 443, context=context, timeout=10)
        try:
            connection.request("POST", url.path or "/v1/events", payload,
                               {"Authorization": "Bearer " + token, "Content-Type": "application/json", "Idempotency-Key": args.key})
            response = connection.getresponse()
            body = response.read().decode()
            if response.status == 202:
                print(body)
                return
            if response.status != 503:
                raise SystemExit(f"request rejected: HTTP {response.status}: {body}")
        except ssl.SSLCertVerificationError:
            raise
        except (OSError, http.client.HTTPException):
            pass
        finally:
            connection.close()
        time.sleep(min(2 ** attempt, 15) + random.random())
    raise SystemExit("retry budget exhausted; retry later with the same key and event")


if __name__ == "__main__":
    main()
