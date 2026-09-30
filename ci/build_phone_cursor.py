"""Compile the phone-side display-only helper; cache hash-pinned build tools."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import ssl
import time
import urllib.error
import urllib.request
import zipfile

TOOLS = {
    "ecj.jar": ("https://repo.maven.apache.org/maven2/org/eclipse/jdt/ecj/3.37.0/ecj-3.37.0.jar",
                "cde026ff966b48b5e5f148b6f041ceff3cf4f85cf75155f4ec0f40e4ee14b545"),
    "r8.jar": ("https://dl.google.com/dl/android/maven2/com/android/tools/r8/8.3.37/r8-8.3.37.jar",
               "59753e70a74f918389cc87f1b7d66b5c0862932559167425708ded159e3de439"),
}


def download_tool(url: str, digest: str, path: Path) -> None:
    """Retry transient HTTPS transport failures, never bypass TLS or hash checks."""
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                data = response.read()
        except (urllib.error.URLError, ConnectionError, TimeoutError) as error:
            reason = getattr(error, "reason", error)
            # Certificate failures and permanent HTTP errors need investigation,
            # not relaxed security or repeated requests to another origin.
            if isinstance(reason, ssl.SSLCertVerificationError):
                raise
            if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == 3:
                raise
            print(f"Retrying {path.name} download after transport error ({attempt + 1}/3)", flush=True)
            time.sleep(2 ** attempt)
            continue
        # A successful but different response is NOT a transient network error.
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f"Hash mismatch: {path.name}")
        path.write_bytes(data)
        return


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--java", required=True)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in TOOLS.items():
        path = a.output / name
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            download_tool(url, digest, path)
    classes, dex = a.output / "classes", a.output / "dex"
    classes.mkdir(exist_ok=True)
    dex.mkdir(exist_ok=True)
    subprocess.run([a.java, "-jar", str(a.output / "ecj.jar"), "-source", "1.8", "-target", "1.8",
                    "-d", str(classes), str(a.source)], check=True)
    subprocess.run([a.java, "-cp", str(a.output / "r8.jar"), "com.android.tools.r8.D8", "--release",
                    "--min-api", "29", "--output", str(dex), str(classes / "com/qtscrcpy/cursor/PhoneCursor.class")], check=True)
    with zipfile.ZipFile(a.output / "qtscrcpy-cursor.jar", "w", zipfile.ZIP_DEFLATED) as z:
        entry = zipfile.ZipInfo("classes.dex", (2026, 1, 1, 0, 0, 0))
        entry.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(entry, (dex / "classes.dex").read_bytes())

if __name__ == "__main__":
    main()
