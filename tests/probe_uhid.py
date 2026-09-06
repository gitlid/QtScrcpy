"""Exercise the official scrcpy server's UHID path over an isolated ADB tunnel."""
import argparse
import json
import pathlib
import re
import secrets
import socket
import struct
import subprocess
import time

DESCRIPTOR = bytes.fromhex(
    "05010906a101050719e029e715002501750195088102950175088101"
    "9505750105081901290591029501750391019506750815002565"
    "0507190029658100c0"
)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--adb', required=True)
    p.add_argument('--serial', required=True)
    p.add_argument('--server', required=True)
    p.add_argument('--version', default='4.1')
    p.add_argument('--output', required=True)
    a = p.parse_args()
    out = pathlib.Path(a.output)
    out.mkdir(parents=True, exist_ok=True)
    flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    prefix = [a.adb, '-s', a.serial]

    def adb(*args, timeout=20):
        return subprocess.check_output(prefix + list(args), timeout=timeout,
                                       creationflags=flags).decode('utf-8', errors='replace')

    scid = secrets.randbelow(0x7fffffff)
    remote = f'/data/local/tmp/qtscrcpy-uhid-probe-{scid:08x}.jar'
    name = f'QtScrcpy UHID Probe {scid:08x}'
    result = {'serial': a.serial, 'keyboard_name': name, 'server_version': a.version}
    server = event_reader = control = None
    port = None
    log = (out / 'server.log').open('w', encoding='utf-8')
    try:
        result['android_version'] = adb('shell', 'getprop ro.build.version.release').strip()
        adb('push', a.server, remote)
        port = adb('forward', 'tcp:0', f'localabstract:scrcpy_{scid:08x}').strip()
        command = (f'CLASSPATH={remote} app_process / com.genymobile.scrcpy.Server {a.version} '
                   f'scid={scid:08x} video=false audio=false control=true tunnel_forward=true '
                   'send_device_meta=false send_dummy_byte=false clipboard_autosync=false '
                   'cleanup=false log_level=debug')
        server = subprocess.Popen(prefix + ['shell', command], stdout=log,
                                  stderr=subprocess.STDOUT, creationflags=flags)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if server.poll() is not None:
                raise RuntimeError('Official server exited before connection; see server.log')
            try:
                control = socket.create_connection(('127.0.0.1', int(port)), timeout=1)
                # ADB may accept the local connection before Android starts listening.
                control.settimeout(0.15)
                try:
                    if not control.recv(1):
                        control.close()
                        control = None
                        time.sleep(0.15)
                        continue
                except socket.timeout:
                    pass
                break
            except OSError:
                time.sleep(0.15)
        if control is None:
            raise RuntimeError('Timed out waiting for official server')
        encoded = name.encode()
        control.sendall(struct.pack('>BHHHB', 12, 1, 0, 0, len(encoded)) + encoded
                        + struct.pack('>H', len(DESCRIPTOR)) + DESCRIPTOR)
        block = ''
        for _ in range(30):
            devices = adb('shell', 'getevent -pl')
            block = next((b for b in devices.split('add device ') if name in b), '')
            if block:
                break
            time.sleep(0.1)
        if not block:
            raise RuntimeError('UHID keyboard did not appear in kernel input devices')
        (out / 'kernel-device.txt').write_text(block, encoding='utf-8')
        event = re.search(r'\bevent\d+\b', block)
        if not event:
            raise RuntimeError('Keyboard has no evdev handler')
        input_dump = adb('shell', 'dumpsys input')
        (out / 'input-during.txt').write_text(input_dump, encoding='utf-8')
        result['kernel_registered'] = True
        result['android_registered'] = name in input_dump
        result['event_device'] = '/dev/input/' + event.group()
        event_reader = subprocess.Popen(prefix + ['shell', 'timeout', '3', 'getevent', '-lt',
                                                  result['event_device']], stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, creationflags=flags)
        time.sleep(0.3)
        def report(data):
            control.sendall(struct.pack('>BHH', 13, 1, 8) + data)
        report(bytes([2, 0, 0, 0, 0, 0, 0, 0]))  # left Shift down; no text or shortcuts
        time.sleep(0.12)
        report(bytes(8))
        trace = event_reader.communicate(timeout=8)[0].decode('utf-8', errors='replace')
        (out / 'key-events.txt').write_text(trace, encoding='utf-8')
        result['key_down'] = bool(re.search(r'KEY_LEFTSHIFT\s+DOWN', trace))
        result['key_up'] = bool(re.search(r'KEY_LEFTSHIFT\s+UP', trace))
        control.sendall(struct.pack('>BH', 14, 1))
        for _ in range(30):
            if name not in adb('shell', 'dumpsys input'):
                result['destroyed'] = True
                break
            time.sleep(0.1)
        result['supported'] = all(result.get(k) for k in
                                  ['kernel_registered', 'android_registered', 'key_down', 'key_up', 'destroyed'])
    except Exception as e:
        result['supported'] = False
        result['error'] = str(e)
    finally:
        if control:
            control.close()
        if event_reader and event_reader.poll() is None:
            event_reader.terminate()
        if server:
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.terminate()
                server.wait(timeout=5)
        if port:
            adb('forward', '--remove', 'tcp:' + port)
        adb('shell', 'rm', '-f', remote)
        log.close()
        (out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get('supported') else 1


if __name__ == '__main__':
    raise SystemExit(main())
