#!/usr/bin/env python3
"""
End-to-end check: real Host + Guest game processes pairing through tools/rendezvous_server.py.

NOTE: as of v1.4 the game parses --broker but has no broker client yet (src/iss_netplay.cpp never
uses broker_addr, and the host replaces its room code with the traversal-server one). Until that
client exists this test is expected to FAIL; tools/test_netplay_broker.py covers the broker itself.
"""
import sys
if sys.path and sys.path[0].endswith("tools"):
    sys.path.pop(0)

import os
import signal
import subprocess
import tempfile
import time

GAME = "./build/ISS2000Recompiled"
ROM = "baserom.z64"
PORT = 19099
ROOM_CODE = "FALCON-ORBIT-TEMPO"


def stop(proc, name):
    """Stop one process; each step is independent so one failure can't skip the others."""
    if proc.poll() is not None:
        return
    try:
        if os.name == "nt":
            proc.terminate()
        else:
            proc.send_signal(signal.SIGINT)
        proc.wait(timeout=2)
        return
    except Exception:
        pass
    try:
        proc.terminate()
        proc.wait(timeout=2)
        return
    except Exception:
        pass
    print(f"[Test] {name} did not exit, killing it")
    proc.kill()
    proc.wait()


def test_full_connection():
    print("==================================================")
    print(" Testing Full Host + Guest P2P Word Handshake")
    print("==================================================")

    procs = []
    # Logs go to temp files, not unread pipes, so a chatty game process can never block on stderr.
    with tempfile.TemporaryFile("w+") as host_log, tempfile.TemporaryFile("w+") as guest_log:
        try:
            broker = subprocess.Popen([sys.executable, "tools/rendezvous_server.py",
                                       "--host", "127.0.0.1", "--port", str(PORT)])
            procs.append((broker, "broker"))
            time.sleep(0.5)

            print("[Test] Launching Host process...")
            host_proc = subprocess.Popen(
                [GAME, ROM, "--no-launcher", "--host", ROOM_CODE, "--broker", f"127.0.0.1:{PORT}"],
                stdout=subprocess.DEVNULL, stderr=host_log)
            procs.append((host_proc, "host"))
            time.sleep(1.0)

            print("[Test] Launching Guest process...")
            guest_proc = subprocess.Popen(
                [GAME, ROM, "--no-launcher", "--join", ROOM_CODE, "--broker", f"127.0.0.1:{PORT}"],
                stdout=subprocess.DEVNULL, stderr=guest_log)
            procs.append((guest_proc, "guest"))

            # Give the handshake up to 10 seconds.
            start_time = time.time()
            while time.time() - start_time < 10.0:
                if host_proc.poll() is not None or guest_proc.poll() is not None:
                    break
                time.sleep(0.5)
        finally:
            print("[Test] Shutting down processes...")
            for proc, name in reversed(procs):
                stop(proc, name)

        host_log.seek(0)
        guest_log.seek(0)
        host_err = host_log.read()
        guest_err = guest_log.read()

    print("\n--- HOST STDERR ---")
    print(host_err)
    print("--- GUEST STDERR ---")
    print(guest_err)

    def paired(log):
        return "Handshake" in log or "Broker paired" in log

    if paired(host_err) and paired(guest_err):
        print("\n>>> FULL P2P HANDSHAKE TEST SUCCEEDED! <<<")
        return True
    print("\n[!] Handshake check failed (expected until the game has a broker client, see docstring)")
    return False


if __name__ == "__main__":
    sys.exit(0 if test_full_connection() else 1)
