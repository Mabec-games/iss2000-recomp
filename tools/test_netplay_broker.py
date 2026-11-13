#!/usr/bin/env python3
"""
Test script for ISS 2000 P2P Rendezvous & Relay Server.
Simulates Host and Guest connecting using 3-word room code.
"""

import sys
if sys.path and sys.path[0].endswith("tools"):
    sys.path.pop(0)

import socket
import time
import subprocess

def test_broker():
    # 1. Start rendezvous server on localhost:19099
    port = 19099
    server_proc = subprocess.Popen([sys.executable, "tools/rendezvous_server.py", "--host", "127.0.0.1", "--port", str(port)])
    time.sleep(0.5)

    try:
        # Create Host socket
        host_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        host_sock.settimeout(2.0)
        room_code = "falcon-orbit-tempo"

        # Host registers room
        host_sock.sendto(f"REGISTER {room_code}".encode("utf-8"), ("127.0.0.1", port))
        resp, _ = host_sock.recvfrom(1024)
        print("[Host] Received from broker:", resp.decode())
        assert resp.decode() == f"WAITING {room_code}"

        # Create Guest socket
        guest_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        guest_sock.settimeout(2.0)

        # Guest joins room
        guest_sock.sendto(f"JOIN {room_code}".encode("utf-8"), ("127.0.0.1", port))
        guest_resp, _ = guest_sock.recvfrom(1024)
        print("[Guest] Received peer info:", guest_resp.decode())
        assert guest_resp.decode().startswith("PEER ")

        # Host should receive peer info
        host_resp, _ = host_sock.recvfrom(1024)
        print("[Host] Received peer info:", host_resp.decode())
        assert host_resp.decode().startswith("PEER ")

        # Test relay mode: Host sends RELAY packet to Guest
        relay_payload = b"RELAY falcon-orbit-tempo\x00HELLO_NETPLAY_GUEST"
        host_sock.sendto(relay_payload, ("127.0.0.1", port))

        relayed_msg, _ = guest_sock.recvfrom(1024)
        print("[Guest] Received relayed packet:", relayed_msg)
        assert relayed_msg == b"GAME HELLO_NETPLAY_GUEST"

        # Guest sends RELAY packet to Host
        relay_payload2 = b"RELAY falcon-orbit-tempo\x00HELLO_NETPLAY_HOST"
        guest_sock.sendto(relay_payload2, ("127.0.0.1", port))

        relayed_msg2, _ = host_sock.recvfrom(1024)
        print("[Host] Received relayed packet:", relayed_msg2)
        assert relayed_msg2 == b"GAME HELLO_NETPLAY_HOST"

        # The game sends normalize_words() output, which is UPPERCASE: relay must still match the room.
        host_sock.sendto(b"RELAY FALCON-ORBIT-TEMPO\x00UPPER_CASE_CODE", ("127.0.0.1", port))
        relayed_msg3, _ = guest_sock.recvfrom(1024)
        print("[Guest] Received relayed packet (uppercase code):", relayed_msg3)
        assert relayed_msg3 == b"GAME UPPER_CASE_CODE"

        # A third party must not be able to steal, re-register or close the paired room.
        intruder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        intruder.settimeout(2.0)
        for cmd, expected in ((b"JOIN falcon-orbit-tempo", b"ERROR room_full"),
                              (b"REGISTER falcon-orbit-tempo", b"ERROR room_taken"),
                              (b"CLOSE falcon-orbit-tempo", b"ERROR not_room_host")):
            intruder.sendto(cmd, ("127.0.0.1", port))
            resp, _ = intruder.recvfrom(1024)
            print(f"[Intruder] {cmd.decode()} ->", resp.decode())
            assert resp == expected

        # The room still relays between the original pair.
        guest_sock.sendto(b"RELAY falcon-orbit-tempo\x00STILL_PAIRED", ("127.0.0.1", port))
        relayed_msg4, _ = host_sock.recvfrom(1024)
        assert relayed_msg4 == b"GAME STILL_PAIRED"

        # Only the host can close it.
        host_sock.sendto(b"CLOSE falcon-orbit-tempo", ("127.0.0.1", port))
        resp, _ = host_sock.recvfrom(1024)
        assert resp == b"OK"

        print("\n>>> ALL RENDEZVOUS & RELAY TESTS PASSED! <<<")

    finally:
        server_proc.terminate()
        server_proc.wait(timeout=5)

if __name__ == "__main__":
    test_broker()
