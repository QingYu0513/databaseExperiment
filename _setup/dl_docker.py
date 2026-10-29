"""Resumable downloader for the installers we need."""
import os
import ssl
import sys
import time
import urllib.request

ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

URL = "https://desktop.docker.com/win/main/amd64/Docker%20Desktop%20Installer.exe"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "downloads")
os.makedirs(OUT, exist_ok=True)
dest = os.path.join(OUT, "DockerDesktopInstaller.exe")

total = 636811696
for attempt in range(1, 40):
    have = os.path.getsize(dest) if os.path.exists(dest) else 0
    if have >= total:
        print(f"DONE {have} bytes")
        break
    req = urllib.request.Request(URL, headers={
        "User-Agent": "Mozilla/5.0",
        "Range": f"bytes={have}-",
    })
    try:
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=90, context=ctx) as r, open(dest, "ab") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
                have += len(chunk)
                if have % (32 << 20) < (1 << 20):
                    pct = have / total * 100
                    spd = have / 1024 / 1024 / max(time.time() - t0, 1)
                    print(f"{pct:5.1f}%  {have/1024/1024:7.1f}/{total/1024/1024:.0f} MB  ~{spd:.2f} MB/s", flush=True)
    except Exception as e:
        print(f"attempt {attempt}: {type(e).__name__}: {str(e)[:100]} (have {have/1024/1024:.1f} MB)", flush=True)
        time.sleep(3)

size = os.path.getsize(dest) if os.path.exists(dest) else 0
print(f"FINAL {size} bytes ({size/1024/1024:.1f} MB) complete={size >= total}")
