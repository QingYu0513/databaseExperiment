"""Normalize the miniob working tree (including submodules) to LF line endings.

Why: Git for Windows here has `core.autocrlf=true`, so the checkout is CRLF
(1037/1038 tracked files). Mounted into a Linux container, that breaks every
shell script -- `build.sh` dies with `$'\r': command not found`.

Note: with autocrlf=true the *index* already stores LF, so converting the working
tree to LF actually makes it match the index (git status stays clean). We then set
core.autocrlf=false locally so git won't re-convert on the next checkout.
"""
import os
import subprocess
import sys

ROOT = r"C:\Users\24235\Desktop\databaseExperiment\miniob"

SKIP_DIRS = {".git", "build", "build_debug", "build_release", "node_modules"}


def is_binary(data: bytes) -> bool:
    return b"\x00" in data[:8192]


def fix_tree(root: str, label: str):
    converted = []
    scanned = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            p = os.path.join(dirpath, fn)
            try:
                if os.path.getsize(p) > 40 * 1024 * 1024:
                    continue
                with open(p, "rb") as f:
                    data = f.read()
            except OSError:
                continue
            scanned += 1
            if b"\r\n" not in data or is_binary(data):
                continue
            with open(p, "wb") as f:
                f.write(data.replace(b"\r\n", b"\n"))
            converted.append(os.path.relpath(p, root))
    print(f"[{label}] scanned={scanned} converted={len(converted)}")
    for c in converted[:8]:
        print(f"    {c}")
    if len(converted) > 8:
        print(f"    ... and {len(converted)-8} more")
    return len(converted)


def git(args, cwd):
    r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    return r.returncode, (r.stdout or "").strip(), (r.stderr or "").strip()


if __name__ == "__main__":
    print("=== before: git config ===")
    print("local core.autocrlf =", git(["config", "--local", "--get", "core.autocrlf"], ROOT)[1] or "(unset)")

    # 1) main repo
    fix_tree(ROOT, "miniob")

    # 2) submodules
    rc, out, err = git(["submodule", "foreach", "--recursive", "--quiet", "echo $displaypath"], ROOT)
    subs = [s for s in out.splitlines() if s.strip()]
    print(f"submodules: {subs}")
    for s in subs:
        sub_path = os.path.join(ROOT, s.replace("/", os.sep))
        if os.path.isdir(sub_path):
            fix_tree(sub_path, s)

    # 3) stop git from re-converting: repo-local + every submodule
    for target, name in [(ROOT, "miniob")] + [
        (os.path.join(ROOT, s.replace("/", os.sep)), s) for s in subs
    ]:
        if os.path.isdir(target):
            git(["config", "core.autocrlf", "false"], target)
            git(["config", "core.eol", "lf"], target)
    print("set core.autocrlf=false + core.eol=lf in miniob and all submodules")
