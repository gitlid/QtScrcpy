"""Exercise the compiled Controller's UHID recording and playback on an ADB device."""
import argparse
import json
import os
from pathlib import Path
import queue
import re
import secrets
import subprocess
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("adb", "serial", "server", "client", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--version", default="4.1")
    parser.add_argument("--runtime", action="append", default=[])
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", args.version):
        parser.error("--version must be a numeric dotted version")
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    prefix = [str(Path(args.adb).resolve()), "-s", args.serial]
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

    def adb(*command):
        return subprocess.check_output(prefix + list(command), timeout=15,
                                       creationflags=flags).decode("utf-8", errors="replace")

    scid = secrets.randbelow(0x7fffffff)
    remote = f"/data/local/tmp/qtscrcpy-compiled-test-{scid:08x}.jar"
    server = client = events = None
    port = None
    result = {"keyboard": "QtScrcpy keyboard", "passed": False}
    server_log = (out / "server.log").open("w", encoding="utf-8")
    client_log = (out / "client.log").open("w", encoding="utf-8")
    try:
        if "QtScrcpy keyboard" in adb("shell", "getevent -pl"):
            raise RuntimeError("Close other UHID projection connections before running this test")
        result["model"] = adb("shell", "getprop ro.product.model").strip()
        result["android"] = adb("shell", "getprop ro.build.version.release").strip()
        adb("push", str(Path(args.server).resolve()), remote)
        port = adb("forward", "tcp:0", f"localabstract:scrcpy_{scid:08x}").strip()
        command = (f"CLASSPATH={remote} app_process / com.genymobile.scrcpy.Server {args.version} "
                   f"scid={scid:08x} video=false audio=false control=true tunnel_forward=true "
                   "send_device_meta=false send_dummy_byte=false clipboard_autosync=false cleanup=false")
        server = subprocess.Popen(prefix + ["shell", command], stdout=server_log,
                                  stderr=subprocess.STDOUT, creationflags=flags)
        time.sleep(1)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        env["PATH"] = os.pathsep.join(str(Path(p).resolve()) for p in args.runtime) + os.pathsep + env["PATH"]
        client = subprocess.Popen([str(Path(args.client).resolve()), "--live", port, str(out)],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=client_log,
                                  text=True, env=env, creationflags=flags)
        lines = queue.Queue()

        def read_lines():
            for line in client.stdout:
                lines.put(line.strip())

        threading.Thread(target=read_lines, daemon=True).start()

        def wait_for(expected):
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                try:
                    if lines.get(timeout=1) == expected:
                        return
                except queue.Empty:
                    if client.poll() is not None:
                        raise RuntimeError(f"Compiled client exited with {client.returncode}")
            raise RuntimeError(f"Timed out waiting for {expected}")

        wait_for("READY")
        dump = adb("shell", "dumpsys input")
        (out / "input-registered.txt").write_text(dump, encoding="utf-8")
        block = next((b for b in re.split(r"\n  Device ", dump)
                      if re.match(r"\d+:", b) and "QtScrcpy keyboard" in b), "")
        result["alphabetic_keyboard"] = "KeyboardType: 2" in block
        block = next((b for b in adb("shell", "getevent -pl").split("add device ")
                      if "QtScrcpy keyboard" in b), "")
        event = re.search(r"/dev/input/event\d+", block)
        if not event or not result["alphabetic_keyboard"]:
            raise RuntimeError("Compiled controller did not register an alphabetic keyboard")
        result["event_device"] = event.group()
        adb("shell", "am", "start", "-a", "android.settings.HARD_KEYBOARD_SETTINGS")
        time.sleep(0.6)
        with (out / "physical-keyboard-settings.png").open("wb") as png:
            subprocess.run(prefix + ["exec-out", "screencap", "-p"], stdout=png,
                           timeout=15, check=True, creationflags=flags)
        events = subprocess.Popen(prefix + ["shell", "timeout", "4", "getevent", "-lt", event.group()],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=flags)
        time.sleep(0.3)
        client.stdin.write("run\n")
        client.stdin.flush()
        wait_for("FINISHED")
        trace = events.communicate(timeout=8)[0].decode("utf-8", errors="replace")
        (out / "input-events.txt").write_text(trace, encoding="utf-8")
        for key, name in (("A", "a"), ("LEFTSHIFT", "shift")):
            for action in ("DOWN", "UP"):
                result[f"{name}_{action.lower()}_count"] = len(re.findall(rf"KEY_{key}\s+{action}", trace))
        result["record_and_two_replays"] = result["a_down_count"] == result["a_up_count"] == 3
        result["focus_release"] = result["shift_down_count"] == result["shift_up_count"] == 1
        client.stdin.write("close\n")
        client.stdin.flush()
        result["client_exit"] = client.wait(timeout=10)
        time.sleep(0.3)
        result["destroyed"] = "QtScrcpy keyboard" not in adb("shell", "getevent -pl")
        result["passed"] = all(result.get(k) for k in (
            "alphabetic_keyboard", "record_and_two_replays", "focus_release", "destroyed"
        )) and result["client_exit"] == 0
    except Exception as error:
        result["error"] = str(error)
    finally:
        for process in (events, client, server):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        try:
            if port:
                adb("forward", "--remove", "tcp:" + port)
            adb("shell", "rm", "-f", remote)
        except Exception as error:
            result["cleanup_error"] = str(error)
            result["passed"] = False
        server_log.close()
        client_log.close()
        (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
