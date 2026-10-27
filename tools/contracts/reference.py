#!/usr/bin/env python3
"""Normalize generated Markdown and add navigation to its translated counterpart."""

from pathlib import Path
import re
import sys

IMPORTED_TYPES = '''## Imported Message Types

Specifications: [Protobuf types](https://protobuf.dev/reference/protobuf/google.protobuf/), [HTTP body](https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto).

<a name="google-protobuf-Timestamp"></a>
### google.protobuf.Timestamp

| Field | Type | Label | Description |
| --- | --- | --- | --- |
| seconds | [int64](#int64) | | UTC seconds relative to the Unix epoch. |
| nanos | [int32](#int32) | | Nanosecond fraction, from 0 through 999999999. |

<a name="google-protobuf-Duration"></a>
### google.protobuf.Duration

| Field | Type | Label | Description |
| --- | --- | --- | --- |
| seconds | [int64](#int64) | | Signed whole seconds. |
| nanos | [int32](#int32) | | Nanosecond fraction; its sign matches nonzero seconds. |

<a name="google-protobuf-Any"></a>
### google.protobuf.Any

| Field | Type | Label | Description |
| --- | --- | --- | --- |
| type_url | [string](#string) | | URL identifying the fully qualified message type. |
| value | [bytes](#bytes) | | Serialized bytes of the identified message. |

<a name="google-api-HttpBody"></a>
### google.api.HttpBody

| Field | Type | Label | Description |
| --- | --- | --- | --- |
| content_type | [string](#string) | | MIME type for the HTTP body. |
| data | [bytes](#bytes) | | Raw request or response content. |
| extensions | [google.protobuf.Any](#google-protobuf-Any) | repeated | Additional response metadata; streaming APIs put it in the first response. |
'''


def normalize(text):
    text = text.split("\n<!-- imported-types -->", 1)[0]
    text = re.sub(r"\n+<!-- language-links -->.*?<!-- /language-links -->\n", "\n", text, flags=re.S)
    text = re.sub(r"^(# [^\n]+)\n+", r"\1\n", text)
    lines = [line.rstrip().replace("\u2013", "-").replace("\u2014", "-") for line in text.splitlines()]
    navigation = ["", "<!-- language-links -->",
                  "[English](proto-reference.md) | [Русский](../../docs/ru/repository/contracts/docs/proto-reference.md)",
                  "<!-- /language-links -->"]
    return "\n".join(lines[:1] + navigation + lines[1:]).rstrip() + "\n\n<!-- imported-types -->\n" + IMPORTED_TYPES


if __name__ == "__main__":
    target = Path(sys.argv[1])
    target.write_text(normalize(target.read_text(encoding="utf-8")), encoding="utf-8")
