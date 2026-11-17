#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Host test for launcher/tools/scan_names.py (plan Task E6).

Run: python3 -I launcher/tests/test_scan_names.py
Builds fixture trees in a temp dir; never touches real package inputs.
"""
import hashlib
import os
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCANNER = os.path.join(HERE, "..", "tools", "scan_names.py")
FAILS = []

# Built from pieces so this test file does not itself trip a scan of the repo.
P = "pros" + "pero"
ALLOWED_SYM = "__wine_" + P + "_native_wow64_caps"


def run(root, allow=None, private=None):
    cmd = [sys.executable, "-I", SCANNER, root]
    if allow:
        cmd += ["--allow", allow]
    env = dict(os.environ)
    env.pop("SCAN_NAMES_PRIVATE", None)   # only the list a test passes counts
    if private:
        env["SCAN_NAMES_PRIVATE"] = private
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    return r.returncode, lines, r.stderr


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else "  " + detail))
    if not cond:
        FAILS.append(name)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def test_plan_fixture(tmp):
    """Plan E6 step 1: one hit in a name, one in a binary, one allowed -> exit 1, 2 lines."""
    root = os.path.join(tmp, "plan")
    write(os.path.join(root, "sce_module", "wow" + P + ".prx"), b"\x7fELF clean bytes")
    write(os.path.join(root, "lib", "ntdll.so"), b"\x00\x01ab/data/Pc/Blur/x\x00\xff")
    write(os.path.join(root, "lib", "wow64.so"), b"\x00" + ALLOWED_SYM.encode() + b"\x00")
    allow = os.path.join(tmp, "allow.txt")
    write(allow, (ALLOWED_SYM + "  # internal ABI symbol (decision 24)\n# comment\n\n").encode())
    rc, lines, err = run(root, allow)
    check("plan_fixture_exit1", rc == 1, "rc=%d err=%s" % (rc, err))
    check("plan_fixture_two_lines", len(lines) == 2, repr(lines))
    names = [l for l in lines if l.startswith("sce_module/wow" + P + ".prx:name:")]
    check("plan_fixture_name_hit", len(names) == 1, repr(lines))
    bins = [l for l in lines if l == "lib/ntdll.so:4:/data/Pc/Blur"]
    check("plan_fixture_binary_hit_offset", len(bins) == 1, repr(lines))
    # Without the allow file the symbol is a hit too.
    rc2, lines2, _ = run(root)
    check("no_allow_three_lines", rc2 == 1 and len(lines2) == 3, repr(lines2))


def test_clean_tree_exit0(tmp):
    root = os.path.join(tmp, "clean")
    write(os.path.join(root, "a", "dxvk.conf"), b"d3d9.maxFrameLatency = 1\n")
    write(os.path.join(root, "BLUR", "blur.txt"), b"/data/BLUR/game\n")
    rc, lines, err = run(root)
    check("clean_exit0", rc == 0 and lines == [], "rc=%d %r %s" % (rc, lines, err))


def test_case_insensitive_and_utf16(tmp):
    root = os.path.join(tmp, "case")
    write(os.path.join(root, "upper.bin"), b"xx" + P.upper().encode() + b"yy")
    write(os.path.join(root, "wide.dll"), b"\x00\x00" + ("/data/" + P + "-win").encode("utf-16-le"))
    rc, lines, _ = run(root)
    check("upper_hit", any(l.startswith("upper.bin:2:") for l in lines), repr(lines))
    check("utf16_hit", any(l.startswith("wide.dll:2:") and "utf16" in l for l in lines), repr(lines))


def test_other_patterns(tmp):
    root = os.path.join(tmp, "other")
    write(os.path.join(root, "t.txt"), b"see github.com/plastik" + b"man/x and mail joe.doe@example.org\n")
    rc, lines, _ = run(root)
    check("plastikman_hit", any(l.lower().endswith(":plastik" + "man") for l in lines), repr(lines))
    check("email_hit", any(l.endswith(":joe.doe@example.org") for l in lines), repr(lines))
    # mingw stdcall symbols (name@argbytes.cold / .part.0) are not e-mail addresses
    root2 = os.path.join(tmp, "stdcall")
    write(os.path.join(root2, "d3d9.dll"), b"\x00__ZN4dxvk9Foo4ResetEv@8.cold\x00_RegCreateKeyExA@36.part.0\x00")
    rc2, lines2, _ = run(root2)
    check("stdcall_symbol_not_email", rc2 == 0 and lines2 == [], repr(lines2))


def test_backslash_paths_and_utf16_email(tmp):
    """Review finding: Windows/registry spellings of the old data folder and UTF-16 addresses."""
    root = os.path.join(tmp, "bs")
    write(os.path.join(root, "profile.txt"), b"executable = Z:\\data\\Pc\\Blur\\Blur.exe\n")
    write(os.path.join(root, "user.reg"), b'"Path"="Z:\\\\data\\\\Pc\\\\Blur"\n')
    write(os.path.join(root, "w.dll"), b"\x00\x00" + "Z:\\data\\Pc\\Blur".encode("utf-16-le"))
    write(os.path.join(root, "v.dll"), b"\x00\x00" + "mail me@example.org".encode("utf-16-le"))
    rc, lines, _ = run(root)
    check("backslash_path_hit", any(l.startswith("profile.txt:") for l in lines), repr(lines))
    check("reg_escaped_path_hit", any(l.startswith("user.reg:") for l in lines), repr(lines))
    check("utf16_backslash_path_hit", any(l.startswith("w.dll:") for l in lines), repr(lines))
    check("utf16_email_hit", any(l.startswith("v.dll:") and "me@example.org" in l for l in lines), repr(lines))


def test_build_host_paths(tmp):
    """Audit finding 6: build-host homes, the WSL Windows profile and personal names.
    The personal names are synthetic and come from a private list (SCAN_NAMES_PRIVATE)."""
    U, Z, W = "builderone", "buildertwo", "Testname"
    private = os.path.join(tmp, "private-names.txt")
    write(private, ("# synthetic private names\n%s\n%s\n=%s\n\n" % (U, Z, W.lower())).encode())
    root = os.path.join(tmp, "host")
    write(os.path.join(root, "a.prx"), b"\x00/home/" + U.encode() + b"/orbis/x.c\x00")
    write(os.path.join(root, "b.dll"), b"\x00/home/" + Z.encode() + b"/work/y.c\x00")
    write(os.path.join(root, "c.prx"), b"\x00/home/runner/work/llvm-project/z.c\x00")
    write(os.path.join(root, "system.reg"), b'"Tahoma"="Z:\\\\home\\\\' + U.encode() + b'\\\\fonts\\\\t.ttf"\n')
    write(os.path.join(root, "d.sh"), b"zip=/mnt/c/Users/" + W.lower().encode() + b"/Downloads/a.zip\n")
    write(os.path.join(root, "e.txt"), b"C:\\Users\\Someone\\AppData\n")
    write(os.path.join(root, "f.dll"), b"\x00\x00" + "/home/someone/x".encode("utf-16-le"))
    write(os.path.join(root, "g.txt"), b"built by " + W.encode() + b" today\n")
    write(os.path.join(root, "h.txt"), b"tool " + Z.encode() + b"-2 made it\n")
    rc, lines, _ = run(root, private=private)
    for f in ("a.prx", "b.dll", "c.prx", "system.reg", "d.sh", "e.txt", "f.dll", "g.txt", "h.txt"):
        check("host_path_hit_" + f, any(l.startswith(f + ":") for l in lines), repr(lines))
    # Without the private list a bare name is not known; with it, "=name" is a whole word only.
    rc3, lines3, _ = run(root)
    check("private_names_need_the_list", not any(l.startswith(("g.txt:", "h.txt:")) for l in lines3), repr(lines3))
    root3 = os.path.join(tmp, "word")
    write(os.path.join(root3, "w.txt"), b"xx" + W.lower().encode() + b"yy\n")
    rc4, lines4, _ = run(root3, private=private)
    check("private_word_only_whole", rc4 == 0 and lines4 == [], repr(lines4))
    # The Wine prefix's own users and ordinary words are not host paths.
    root2 = os.path.join(tmp, "prefix")
    write(os.path.join(root2, "user.reg"), b'"Desktop"="C:\\\\users\\\\BLUR\\\\Desktop"\n"P"="C:\\\\users\\\\Public"\n'
          b'"D"="C:\\\\Users\\\\Default"\n')
    write(os.path.join(root2, "t.txt"), b"/data/BLUR/home.txt homepage /usr/home2 drive_c/users/BLUR\n")
    rc2, lines2, _ = run(root2, private=private)
    check("prefix_users_clean", rc2 == 0 and lines2 == [], repr(lines2))


def test_symlink_cycle_terminates(tmp):
    """Review finding: a prefix with dosdevices/z: -> / must not walk forever."""
    root = os.path.join(tmp, "cyc")
    write(os.path.join(root, "a", "ok.txt"), b"fine")
    os.symlink(root, os.path.join(root, "a", "loop"))
    try:
        r = subprocess.run([sys.executable, "-I", SCANNER, root], capture_output=True, text=True, timeout=20)
        check("symlink_cycle_exit0", r.returncode == 0, r.stdout + r.stderr)
    except subprocess.TimeoutExpired:
        check("symlink_cycle_exit0", False, "timeout")


def test_zip_descend(tmp):
    root = os.path.join(tmp, "zip")
    os.makedirs(root)
    z = os.path.join(root, "pack.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("inner/ok.txt", b"fine")
        zf.writestr("inner/bad.txt", b"0123" + P.encode())
        zf.writestr("inner/" + P + ".txt", b"fine")
    rc, lines, _ = run(root)
    check("zip_content_hit", "pack.zip!inner/bad.txt:4:" + P in lines, repr(lines))
    check("zip_name_hit", any(l.startswith("pack.zip!inner/" + P + ".txt:name:") for l in lines), repr(lines))
    check("zip_exit1", rc == 1)


def test_seed_descend(tmp):
    """seed.idx (N245-SEED 1): objects are reported under their logical path."""
    root = os.path.join(tmp, "seed")
    sd = os.path.join(root, "seed245", "blur-native248")
    objs = os.path.join(sd, "objects")
    os.makedirs(objs)
    good = b"REGEDIT4\n"
    bad = b"[Software\\Wine]\n\"x\"=\"wow" + P.encode() + b".dll\"\n"
    hg, hb = hashlib.sha256(good).hexdigest(), hashlib.sha256(bad).hexdigest()
    write(os.path.join(objs, hg), good)
    write(os.path.join(objs, hb), bad)
    prof = b"[application]\nexecutable = Z:\\data\\BLUR\\game\\Blur.exe\nworking_directory = /data/Pc/Blur\n"
    hp = hashlib.sha256(prof).hexdigest()
    write(os.path.join(objs, hp), prof)
    idx = ("N245-SEED 1\nslug blur-native248\nmanifest_end 00\n"
           "D\tdrive_c/users/" + P + "\n"
           "F\t%d\t%s\tuserdef.reg\t-\n" % (len(good), hg) +
           "F\t%d\t%s\tsystem.reg\t-\n" % (len(bad), hb) +
           "F\t5\t%s\tdrive_c/windows/x.dll\twin/wine/lib/x.dll\n" % ("ab" * 32) +
           "P\t%d\t%s\n" % (len(prof), hp) +
           "END 00\n")
    write(os.path.join(sd, "seed.idx"), idx.encode())
    rc, lines, _ = run(root)
    off = bad.index(b"wow")
    check("seed_logical_content_hit",
          "seed245/blur-native248/seed.idx!system.reg:%d:wow%s" % (off, P) in lines, repr(lines))
    check("seed_logical_dir_hit",
          any(l.startswith("seed245/blur-native248/seed.idx!drive_c/users/" + P + ":name:") for l in lines),
          repr(lines))
    check("seed_profile_hit",
          "seed245/blur-native248/seed.idx!<profile>:%d:/data/Pc/Blur" % prof.index(b"/data/Pc") in lines,
          repr(lines))
    check("seed_runtime_file_no_error", rc == 1, "rc=%d" % rc)
    check("seed_object_not_double", not any("/objects/" in l for l in lines), repr(lines))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        for t in (test_plan_fixture, test_clean_tree_exit0, test_case_insensitive_and_utf16,
                  test_other_patterns, test_backslash_paths_and_utf16_email, test_build_host_paths,
                  test_symlink_cycle_terminates,
                  test_zip_descend, test_seed_descend):
            try:
                t(tmp)
            except Exception as e:  # a crash is a failure, not a skip
                check(t.__name__ + "_ran", False, repr(e))
    print("test_scan_names: %s" % ("FAIL %d" % len(FAILS) if FAILS else "OK"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
