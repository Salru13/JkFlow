# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Write manifest.txt for Patch (spec 8.5, BLUR 1.00 plan Task L3) at package build time.

    python3 -I make_manifest.py BLUR_FILES_DIR NAME [NAME ...] [--seed NAME ...]
        [--overlay DIR PREFIX ...] [--online-overlay DIR PREFIX ...] [--amax DIR]
        [--blur12 DIR] [--park GLOB ...] [--known OLD_MANIFEST ...]
        [--tested SHA256 ...] [--cache NAME ...] [--edit-plugins REL]

BLUR_FILES_DIR holds the files the launcher installs into the player's Blur
folder. Every NAME (a path relative to BLUR_FILES_DIR, "/" subfolders allowed)
is hashed and listed as "file <name> <sha256> <size>".

--overlay DIR PREFIX lists every file under BLUR_FILES_DIR/DIR as
"file PREFIX/<rel> <sha256> <size> src=DIR/<rel>" (e.g. amax-overlay -> amax:
our plugins.lua and account plugin).

--online-overlay DIR PREFIX is the same for our files that only make sense
with AMAX (installed only by "Patch for online play"): "online" lines.

--amax DIR lists every file under DIR (the extracted, pinned AMAX client zip;
dot files skipped) as "amax <rel> <sha256> <size>". The launcher takes those
files from the player's zip at Patch; they are never shipped. An AMAX file
that an overlay replaces is not listed as amax; its hash becomes "known" so
the patcher replaces it without parking it. An AMAX file with the name of one
of our offline files (the root d3d9.dll) is listed as both: online, the amax
line takes over.

--edit-plugins REL (owner finding 25: JkFlow ships no AMAX file) marks the
AMAX file REL (amax/plugins/plugins.lua) as edited at Patch time: its amax
line gets "edit=<sha256>", the hash of the list after the launcher comments
out "solo" and "fps" and adds our account plugin (plugins_edit below, the
same rule as bl_patch_plugins_edit in bl_patcher.c). Its own hash is not made
"known", so the patcher parks an existing copy and Restore puts it back.

--blur12 DIR lists every file under DIR (the extracted, pinned blur_12.zip)
as "blur12 <rel> <sha256> <size>"; Blur.exe is allowed at the top level.

--park GLOB adds "park <glob>": top-level names in the game folder (any
letter case, * and ?) that the patcher parks unless they belong to the set.

Each --known manifest (from an earlier package) adds its hashes as
"known <sha256>". Each --seed NAME is listed as "seed <name> <sha256> <size>"
(put in place only when missing or header-only). Each --tested SHA256 is
listed as "tested <sha256>". Each --cache NAME is listed as
"cache <name> <sha256> <size>": the launcher puts it into its cache folder
(the engine's DXVK_STATE_CACHE_PATH), never into the game folder, and only
when missing or header-only; the patcher ignores these lines (they do not
count towards the file limit). The file is written to BLUR_FILES_DIR/manifest.txt.
"""
import hashlib
import os
import sys

MAX_FILES = 64


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


ACCOUNT = b'require("account.account")'


def plugins_edit(data):
    """bl_patcher.c bl_patch_plugins_edit: comment out solo and fps, add our
    account plugin before the last line starting with '}'. None: no '}'."""
    eol = b'\r\n' if b'\r\n' in data else b'\n'
    lines = [x + b'\n' for x in data.split(b'\n')]     # lines end at \n only, as in C
    lines[-1] = lines[-1][:-1]
    if not lines[-1]:
        lines.pop()
    indent, close, have = None, None, False
    for i, line in enumerate(lines):
        body = line.lstrip(b' \t')
        ind = line[:len(line) - len(body)]
        if indent is None and body.startswith(b'require(') and len(ind) < 32:
            indent = ind
        if body.startswith(ACCOUNT):
            have = True
        if body.startswith(b'}'):
            close = i
    if close is None:
        return None
    out = []
    for i, line in enumerate(lines):
        body = line.lstrip(b' \t')
        ind = line[:len(line) - len(body)]
        if not have and i == close:
            out.append((b'\t' if indent is None else indent) + ACCOUNT + b',' + eol)
        if body.startswith(b'require("solo.solo")') or body.startswith(b'require("fps.fps")'):
            out.append(ind + b'-- ' + body)
        else:
            out.append(line)
    return b''.join(out)


def safe_blur12(name):
    """bl_patcher.c safe_rel_blur12: safe, or Blur.exe at the top level."""
    return safe(name) or name.lower() == 'blur.exe'


def safe(name):
    """A relative path the launcher accepts (bl_patcher.c safe_rel)."""
    if not name or len(name) >= 128 or name.startswith('/') or name.endswith('/'):
        return False
    if not all(0x20 < ord(c) < 0x7f and c != '\\' for c in name):
        return False
    for seg in name.split('/'):
        if not seg or seg.startswith('.') or seg.lower() == 'blur.exe':
            return False
    return True


def safe_glob(g):
    return (g and len(g) < 64 and not g.startswith('.') and '/' not in g and '\\' not in g
            and '..' not in g and all(0x20 < ord(c) < 0x7f for c in g))


def walk(top):
    """Relative paths of the regular files under top, sorted, dot entries skipped."""
    out = []
    for d, dirs, files in os.walk(top):
        dirs[:] = sorted(x for x in dirs if not x.startswith('.'))
        for f in sorted(files):
            if f.startswith('.'):
                continue
            out.append(os.path.relpath(os.path.join(d, f), top).replace(os.sep, '/'))
    return out


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    root, rest = argv[1], argv[2:]
    names, seeds, known_files, tested, overlays, parks, caches = [], set(), [], [], [], [], []
    amax_dir = blur12_dir = edit_rel = None
    while rest:
        a = rest.pop(0)
        if a == '--known':
            known_files.append(rest.pop(0))
        elif a == '--tested':
            t = rest.pop(0).lower()
            if len(t) != 64 or any(c not in '0123456789abcdef' for c in t):
                raise SystemExit('make_manifest: --tested needs a SHA-256: %r' % t)
            tested.append(t)
        elif a == '--seed':
            n = rest.pop(0)
            seeds.add(n)
            names.append(n)
        elif a == '--cache':
            caches.append(rest.pop(0))
        elif a == '--overlay':
            overlays.append((rest.pop(0), rest.pop(0), 'file'))
        elif a == '--online-overlay':
            overlays.append((rest.pop(0), rest.pop(0), 'online'))
        elif a == '--amax':
            amax_dir = rest.pop(0)
        elif a == '--edit-plugins':
            edit_rel = rest.pop(0)
            if not safe(edit_rel):
                raise SystemExit('make_manifest: unsafe --edit-plugins name: %r' % edit_rel)
        elif a == '--blur12':
            blur12_dir = rest.pop(0)
        elif a == '--park':
            g = rest.pop(0)
            if not safe_glob(g):
                raise SystemExit('make_manifest: unsafe park glob: %r' % g)
            parks.append(g)
        elif a.startswith("--"):
            raise SystemExit("make_manifest: unknown option: %r" % a)
        else:
            names.append(a)
    lines = ['# blur launcher manifest 2']
    current, listed = set(), set()
    entries = []
    for n in names:
        if not safe(n):
            raise SystemExit('make_manifest: unsafe file name: %r' % n)
        p = os.path.join(root, n)
        entries.append(('seed' if n in seeds else 'file', n, p, None))
    for src_dir, prefix, kind in overlays:
        if not safe(src_dir) or not safe(prefix):
            raise SystemExit('make_manifest: unsafe overlay: %r %r' % (src_dir, prefix))
        for rel in walk(os.path.join(root, src_dir)):
            entries.append((kind, prefix + '/' + rel, os.path.join(root, src_dir, rel), src_dir + '/' + rel))
    known = []
    if amax_dir:
        overlaid = {e[1] for e in entries if e[3] is not None}
        for rel in walk(amax_dir):
            p = os.path.join(amax_dir, rel)
            if rel in overlaid:
                known.append(sha256(p))      # replaced by our overlay: ours to overwrite
                continue
            entries.append(('amax', rel, p, None))
    edited = {}
    if edit_rel is not None:
        if not amax_dir or edit_rel not in walk(amax_dir):
            raise SystemExit('make_manifest: --edit-plugins %r is not in the AMAX set' % edit_rel)
        with open(os.path.join(amax_dir, edit_rel), 'rb') as f:
            text = plugins_edit(f.read())
        if text is None:
            raise SystemExit('make_manifest: %r has no closing "}"' % edit_rel)
        edited[edit_rel] = hashlib.sha256(text).hexdigest()
    if blur12_dir:
        for rel in walk(blur12_dir):
            if not safe_blur12(rel):
                raise SystemExit('make_manifest: unsafe blur_12 name: %r' % rel)
            entries.append(('blur12', rel, os.path.join(blur12_dir, rel), None))
    if len(entries) > MAX_FILES:
        raise SystemExit('make_manifest: at most %d files' % MAX_FILES)
    for n in caches:
        if not safe(n):
            raise SystemExit('make_manifest: unsafe file name: %r' % n)
        entries.append(('cache', n, os.path.join(root, n), None))
    for kind, name, path, src in entries:
        if not (safe_blur12(name) if kind == 'blur12' else safe(name)) or (src is not None and not safe(src)):
            raise SystemExit('make_manifest: unsafe file name: %r' % name)
        # Two name spaces: ours (file, seed, online, cache) and the zips' (amax, blur12).
        key = ('zip' if kind in ('amax', 'blur12') else 'ours', name)
        if key in listed:
            raise SystemExit('make_manifest: listed twice: %r' % name)
        listed.add(key)
        digest = sha256(path)
        current.add(digest)
        line = '%s %s %s %d' % (kind, name, digest, os.path.getsize(path))
        if src is not None:
            line += ' src=' + src
        if kind == 'amax' and name in edited:
            line += ' edit=' + edited[name]
        lines.append(line)
    for k in known_files:
        for line in open(k):
            parts = line.split()
            if len(parts) >= 3 and parts[0] in ('file', 'seed'):
                digest = parts[2]
            elif len(parts) >= 2 and parts[0] == 'known':
                digest = parts[1]
            else:
                continue
            known.append(digest)
    seen = set()
    known = [d for d in known if d not in current and not (d in seen or seen.add(d))]
    lines += ['known %s' % d for d in known[:64]]
    lines += ['park %s' % g for g in parks[:32]]
    lines += ['tested %s' % t for t in tested[:8]]
    with open(os.path.join(root, 'manifest.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main(sys.argv)
