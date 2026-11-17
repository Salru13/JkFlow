#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Name and path scanner for BLUR package trees (plan Task E6).

    python3 -I scan_names.py ROOT [--allow FILE] [--private FILE] [--strings]

Scans every file name and file content under ROOT (symlinks followed),
case-insensitive, for the old project name, the old data folders, the old
upstream author handle and e-mail-like strings. ASCII and UTF-16LE forms
are searched (Windows binaries keep wide strings). It descends into .zip
files and into seed archives (a ``seed.idx`` starting ``N245-SEED 1`` with
an ``objects/`` folder): seed objects are reported under their logical
prefix path, never under ``objects/<sha>``.

Output, one hit per line (paths relative to ROOT, ``!`` = inside an archive):
    path:offset:match            content hit (offset in bytes)
    path:offset:match [utf16]    content hit, UTF-16LE form
    path:name:match              hit in the entry's own name
With --strings a tab and the whole printable string around the hit follow.

--allow FILE: one approved exception per line, ``token  # reason``. A hit is
allowed when it lies inside an occurrence of an allowed token
(case-insensitive). Blank lines and lines starting with # are ignored.

--private FILE (default: the environment variable SCAN_NAMES_PRIVATE): extra
private names to report, such as build-host account names, kept outside the
repository. One name per line; "=name" matches only as a whole word. Blank
lines and lines starting with # are ignored. scan-names-private.example next
to this script is an empty example.

Exit status: 0 clean, 1 hits printed, 2 usage or I/O error.
"""
import argparse
import io
import os
import re
import sys
import zipfile

# The names are built from pieces so that this file itself scans clean.
_OLD = "pros" + "pero"
_TERMS = ["wow" + _OLD, _OLD, "plastik" + "man"]
# The old data folders in any separator spelling: /data/x, Z:\data\x, .reg-escaped \\data\\x.
_SEP = rb"[/\\]+"
_DIRS = [_SEP + b"data" + _SEP + re.escape(_OLD.encode()) + b"-win", _SEP + b"data" + _SEP + b"pc" + _SEP + b"blur"]
# Build-host paths (audit finding 6): a POSIX home (/home/<user>, also Z:\home\x and the
# .reg-escaped \\home\\x), the WSL view of a Windows profile (/mnt/c/Users/x) and a Windows
# profile (C:\Users\x, C:/Users/x) other than the Wine prefix's own BLUR/Public/Default users.
_DIRS += [_SEP + b"home" + _SEP + rb"[A-Za-z0-9_.-]",
          _SEP + b"mnt" + _SEP + rb"[a-z]" + _SEP + b"users" + _SEP,
          rb"\b[a-z]:" + _SEP + b"users" + _SEP + rb"(?!(?:blur|public|default)(?![A-Za-z0-9_.-]))[A-Za-z0-9_.-]"]
# Build-host account and personal names are not in this file: they come from a private
# list outside the repository (--private FILE or SCAN_NAMES_PRIVATE=FILE, see load_private).
# A name listed as "=name" matches only as a whole word (a short name inside a longer
# word is not a hit).
# The domain must start with a letter: mingw stdcall symbols (Foo@16.cold, Bar@8.part.0)
# are not addresses.
_EMAIL = rb"[A-Za-z0-9._%+-]{1,64}@[A-Za-z][A-Za-z0-9-]{0,62}(?:\.[A-Za-z0-9-]{1,63})*\.[A-Za-z]{2,24}"


def load_private(path):
    """Private names: one per line, "=name" for a whole word only; # comments."""
    terms, words = [], []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            if s.startswith("="):
                if s[1:]:
                    words.append(s[1:])
            else:
                terms.append(s)
    return terms, words


def configure(private_terms=(), private_words=()):
    """Compile the patterns, with the private names added."""
    global ASCII_RE, NAME_RE
    dirs = list(_DIRS) + [rb"(?<![a-z])" + re.escape(w.encode()) + rb"(?![a-z])" for w in private_words]
    terms = list(_TERMS) + list(private_terms)
    # Longest alternatives first: the folder forms contain the bare old name.
    ASCII_RE = re.compile(b"|".join(dirs + [re.escape(t.encode()) for t in terms] + [_EMAIL]), re.IGNORECASE)
    NAME_RE = re.compile("|".join([d.decode() for d in dirs] + [re.escape(t) for t in terms] + [_EMAIL.decode()]),
                         re.IGNORECASE)


configure()
# UTF-16LE: printable runs are narrowed to ASCII and searched with the same patterns.
UTF16_RUN = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")

SEED_MAGIC = b"N245-SEED 1\n"
PRINTABLE = set(range(0x20, 0x7F))


def load_allow(path):
    tokens = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            s = re.split(r"\s+#", s, maxsplit=1)[0].strip()
            if s:
                tokens.append(s.lower())
    return tokens


class Scanner:
    def __init__(self, allow, strings):
        self.allow = allow
        self.strings = strings
        self.hits = []

    # -- allow test -------------------------------------------------------
    def _allowed(self, low, start, end, wide):
        return any(_covers(low, tok.encode("utf-16-le") if wide else tok.encode(), start, end)
                   for tok in self.allow)

    def _context(self, data, start, end, wide):
        if wide:
            a = start
            while a >= 2 and data[a - 1] == 0 and data[a - 2] in PRINTABLE:
                a -= 2
            b = end
            while b + 1 < len(data) and data[b + 1] == 0 and data[b] in PRINTABLE:
                b += 2
            return data[a:b].decode("utf-16-le", "replace")
        a = start
        while a > 0 and data[a - 1] in PRINTABLE:
            a -= 1
        b = end
        while b < len(data) and data[b] in PRINTABLE:
            b += 1
        return data[a:b].decode("latin-1")

    def _emit(self, line, ctx):
        if self.strings and ctx is not None:
            line += "\t" + ctx
        self.hits.append(line)

    # -- content and names -------------------------------------------------
    def content(self, rel, data):
        low = data.lower() if self.allow else b""
        hits = [(m.start(), m.end(), False) for m in ASCII_RE.finditer(data)]
        for run in UTF16_RUN.finditer(data):
            narrow = run.group(0)[::2]
            for m in ASCII_RE.finditer(narrow):
                hits.append((run.start() + 2 * m.start(), run.start() + 2 * m.end(), True))
        for start, end, wide in hits:
            if self.allow and self._allowed(low, start, end, wide):
                continue
            text = data[start:end].decode("utf-16-le" if wide else "latin-1")
            ctx = self._context(data, start, end, wide) if self.strings else None
            self._emit("%s:%d:%s%s" % (rel, start, text, " [utf16]" if wide else ""), ctx)

    def name(self, rel, base):
        low = base.lower()
        for m in NAME_RE.finditer(base):
            if any(_covers(low, tok, m.start(), m.end()) for tok in self.allow):
                continue
            self._emit("%s:name:%s" % (rel, m.group(0)), base)

    def blob(self, rel, data):
        """Content of one file; descends into zip archives by name."""
        if rel.lower().endswith(".zip"):
            try:
                with zipfile.ZipFile(io.BytesIO(data)) as zf:
                    for info in sorted(zf.infolist(), key=lambda i: i.filename):
                        inner = rel + "!" + info.filename.rstrip("/")
                        self.name(inner, os.path.basename(info.filename.rstrip("/")))
                        if not info.is_dir():
                            self.blob(inner, zf.read(info))
                return
            except zipfile.BadZipFile:
                pass  # not really a zip: scan the raw bytes
        self.content(rel, data)

    def seed(self, rel_idx, idx_path):
        """Seed archive: logical names and object contents."""
        objdir = os.path.join(os.path.dirname(idx_path), "objects")
        with open(idx_path, "rb") as f:
            lines = f.read().decode("utf-8", "replace").splitlines()
        for line in lines:
            parts = line.split("\t")
            kind = parts[0]
            if kind == "D" and len(parts) >= 2:
                logical = parts[1]
                self.name(rel_idx + "!" + logical, logical.rsplit("/", 1)[-1])
            elif kind == "F" and len(parts) >= 4:
                sha, logical = parts[2], parts[3]
                self.name(rel_idx + "!" + logical, logical.rsplit("/", 1)[-1])
                obj = os.path.join(objdir, sha)
                runtime_src = parts[4] if len(parts) >= 5 and parts[4] != "-" else None
                if not os.path.isfile(obj):
                    if runtime_src:
                        continue  # copied from the package's runtime tree, scanned there
                    raise IOError("seed object missing: %s (%s)" % (obj, logical))
                with open(obj, "rb") as o:
                    self.blob(rel_idx + "!" + logical, o.read())
            elif kind == "P" and len(parts) >= 3:
                obj = os.path.join(objdir, parts[2])
                if not os.path.isfile(obj):
                    raise IOError("seed profile object missing: %s" % obj)
                with open(obj, "rb") as o:
                    self.content(rel_idx + "!<profile>", o.read())
            elif kind == "L" and len(parts) >= 2:
                # link entry: name, and its target text (may carry a path)
                self.name(rel_idx + "!" + parts[1], parts[1].rsplit("/", 1)[-1])
                self.content(rel_idx + "!" + parts[1], "\t".join(parts[2:]).encode())


def _covers(low, tok, start, end):
    i = low.find(tok, max(0, end - len(tok)))
    while i != -1 and i <= start:
        if i + len(tok) >= end:
            return True
        i = low.find(tok, i + 1)
    return False


def is_seed_idx(path):
    if os.path.basename(path) != "seed.idx":
        return False
    with open(path, "rb") as f:
        return f.read(len(SEED_MAGIC)) == SEED_MAGIC


def scan(root, allow, strings):
    sc = Scanner(allow, strings)
    root = os.path.abspath(root)
    entries = []  # (rel, abs, is_dir)
    for dirpath, dirnames, filenames in os.walk(root, followlinks=True):
        dirnames.sort()
        for d in dirnames:
            entries.append((os.path.relpath(os.path.join(dirpath, d), root), os.path.join(dirpath, d), True))
        for fn in sorted(filenames):
            entries.append((os.path.relpath(os.path.join(dirpath, fn), root), os.path.join(dirpath, fn), False))
    rel_of = lambda r: r.replace(os.sep, "/")
    # Seed archives first, so their objects are not scanned twice.
    seed_objdirs = set()
    for rel, ab, is_dir in entries:
        if not is_dir and is_seed_idx(ab):
            sc.seed(rel_of(rel), ab)
            seed_objdirs.add(os.path.join(os.path.dirname(ab), "objects"))
    for rel, ab, is_dir in entries:
        if any(ab == o or ab.startswith(o + os.sep) for o in seed_objdirs):
            continue  # reported under logical seed paths
        sc.name(rel_of(rel), os.path.basename(ab))
        if is_dir or is_seed_idx(ab):
            continue
        if not os.path.isfile(ab):
            continue  # dangling link or special file
        with open(ab, "rb") as f:
            sc.blob(rel_of(rel), f.read())
    return sc.hits


def main(argv):
    ap = argparse.ArgumentParser(description="BLUR name and path scanner")
    ap.add_argument("root")
    ap.add_argument("--allow", help="approved exceptions, one token per line")
    ap.add_argument("--strings", action="store_true", help="append the whole string around each hit")
    ap.add_argument("--private", default=os.environ.get("SCAN_NAMES_PRIVATE") or None,
                    help="private names to report, one per line (default: $SCAN_NAMES_PRIVATE)")
    a = ap.parse_args(argv)
    if not os.path.isdir(a.root):
        print("scan_names: not a directory: %s" % a.root, file=sys.stderr)
        return 2
    try:
        if a.private:
            configure(*load_private(a.private))
        allow = load_allow(a.allow) if a.allow else []
        hits = scan(a.root, allow, a.strings)
    except (IOError, OSError) as e:
        print("scan_names: %s" % e, file=sys.stderr)
        return 2
    out = sys.stdout
    for h in hits:
        out.write(h + "\n")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
