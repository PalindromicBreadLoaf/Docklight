#!/usr/bin/env python3
"""Stamp a `portVersion` record into an .o2r archive."""

import os
import struct
import sys
import zipfile

ENTRY_NAME = "portVersion"


def main(argv):
    if len(argv) != 5:
        print(f"usage: {argv[0]} <archive.o2r> <major> <minor> <patch>", file=sys.stderr)
        return 2

    archive, parts = argv[1], tuple(int(n) for n in argv[2:5])
    payload = struct.pack(">3H", *parts)
    version = ".".join(str(n) for n in parts)

    try:
        with zipfile.ZipFile(archive) as zf:
            existing = zf.read(ENTRY_NAME) if ENTRY_NAME in zf.namelist() else None
    except (OSError, zipfile.BadZipFile) as err:
        print(f"error: cannot read {archive}: {err}", file=sys.stderr)
        return 1

    if existing == payload:
        print(f"portVersion {version} already stamped in {archive}")
        return 0

    if existing is not None:
        rewrite_without_entry(archive)

    with zipfile.ZipFile(archive, "a", compression=zipfile.ZIP_STORED) as zf:
        zf.writestr(ENTRY_NAME, payload)

    print(f"stamped portVersion {version} into {archive}")
    return 0


def rewrite_without_entry(archive):
    """Copy `archive` over itself, dropping ENTRY_NAME. Entries are copied compressed-as-is."""
    tmp = archive + ".stamp.tmp"
    with zipfile.ZipFile(archive) as src, zipfile.ZipFile(tmp, "w") as dst:
        for info in src.infolist():
            if info.filename == ENTRY_NAME:
                continue
            dst.writestr(info, src.read(info.filename), compress_type=info.compress_type)
    os.replace(tmp, archive)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
