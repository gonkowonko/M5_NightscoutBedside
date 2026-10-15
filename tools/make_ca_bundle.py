#!/usr/bin/env python3
"""Build certs/x509_crt_bundle.bin: trusted root CAs for verifying HTTPS.

The firmware uses it (WiFiClientSecure::setCACertBundle) to properly verify
GitHub's certificates when checking for and downloading updates. Roots come
from Mozilla's list as packaged by certifi.

The format is ESP-IDF's certificate bundle (as written by its
gen_crt_bundle.py): a big-endian uint16 count, then for each certificate,
sorted by subject: uint16 subject length, uint16 public-key length, DER
subject name, DER SubjectPublicKeyInfo.

    tools/.venv/bin/pip install certifi cryptography
    tools/.venv/bin/python tools/make_ca_bundle.py     # then rebuild
"""

import os
import struct

import certifi
from cryptography import x509
from cryptography.hazmat.primitives import serialization

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "certs", "x509_crt_bundle.bin")
MAX_CERTS = 200  # CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS in arduino-esp32 2.x

# Hosts the updater talks to, and the root that currently signs each one.
# Checked here so a bundle that can't reach GitHub is never shipped.
REQUIRED = ["USERTrust ECC Certification Authority", "ISRG Root X1"]


def main():
    pem = open(certifi.where(), "rb").read()
    certs = x509.load_pem_x509_certificates(pem)
    entries = {}
    for c in certs:
        subject = c.subject.public_bytes()
        key = c.public_key().public_bytes(serialization.Encoding.DER,
                                          serialization.PublicFormat.SubjectPublicKeyInfo)
        entries[subject] = key  # de-duplicate identical subjects
    if len(entries) > MAX_CERTS:
        raise SystemExit(f"{len(entries)} certificates exceeds the firmware limit of {MAX_CERTS}")

    names = {c.subject.rfc4514_string() for c in certs}
    for need in REQUIRED:
        if not any(f"CN={need}" in n for n in names):
            raise SystemExit(f"bundle is missing required root: {need}")

    out = struct.pack(">H", len(entries))
    for subject in sorted(entries):  # sorted: the firmware binary-searches by subject
        key = entries[subject]
        out += struct.pack(">HH", len(subject), len(key)) + subject + key

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "wb").write(out)
    print(f"{OUT}: {len(entries)} root CAs, {len(out)} bytes (certifi {certifi.__version__})")


if __name__ == "__main__":
    main()
