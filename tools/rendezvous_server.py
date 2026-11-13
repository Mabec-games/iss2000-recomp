#!/usr/bin/env python3
"""
ISS 2000 Recompiled — P2P Rendezvous & Relay Server
Facilitates 1vs1 matchmaking via 3-word room codes (e.g., 'falcon-orbit-tempo').

Supports:
- UDP hole-punching facilitation (NAT traversal)
- Transparent packet relaying fallback for strict/symmetric NATs
"""

import sys
# Prevent shadowing of standard library modules (like 'dis') by local files in tools/
if sys.path and sys.path[0].endswith("tools"):
    sys.path.pop(0)

import time
import socket
import argparse
from typing import Dict, Tuple, Optional

def norm_room(name: str) -> str:
    """Room codes are case-insensitive: the game sends normalize_words() output (UPPERCASE)."""
    return name.strip().lower()

class Room:
    def __init__(self, name: str, host_addr: Tuple[str, int]):
        self.name = name
        self.host_addr = host_addr
        self.guest_addr: Optional[Tuple[str, int]] = None
        self.created_at = time.time()
        self.last_active = time.time()
        self.relayed_packets = 0

def run_server(host: str, port: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((host, port))
    except Exception as e:
        print(f"[Error] Failed to bind to {host}:{port}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"============================================================")
    print(f" ISS 2000 P2P Rendezvous & Matchmaking Broker")
    print(f" Listening on UDP {host}:{port}")
    print(f"============================================================")

    rooms: Dict[str, Room] = {}
    CLEANUP_INTERVAL = 30.0
    ROOM_TIMEOUT = 600.0  # 10 minutes
    last_cleanup = time.time()

    while True:
        try:
            data, client_addr = sock.recvfrom(2048)
            now = time.time()

            # Clean up old rooms periodically
            if now - last_cleanup > CLEANUP_INTERVAL:
                expired = [k for k, r in rooms.items() if now - r.last_active > ROOM_TIMEOUT]
                for k in expired:
                    print(f"[*] Room '{k}' expired due to inactivity")
                    del rooms[k]
                last_cleanup = now

            if not data:
                continue

            # Check if this is a binary RELAY packet:
            # RELAY packet magic: b"RELAY" + b" " + room_name (fixed/padded or delimited) + b"\x00" + payload
            if data.startswith(b"RELAY "):
                null_idx = data.find(b"\x00", 6)
                if null_idx != -1:
                    room_name = norm_room(data[6:null_idx].decode("ascii", errors="ignore"))
                    payload = data[null_idx + 1:]
                    room = rooms.get(room_name)
                    if room and room.guest_addr:
                        room.last_active = now
                        room.relayed_packets += 1
                        if client_addr == room.host_addr:
                            sock.sendto(b"GAME " + payload, room.guest_addr)
                        elif client_addr == room.guest_addr:
                            sock.sendto(b"GAME " + payload, room.host_addr)
                    continue

            text = data.decode("utf-8", errors="ignore").strip()
            parts = text.split()
            if not parts:
                continue

            cmd = parts[0].upper()

            if cmd == "REGISTER" and len(parts) >= 2:
                room_name = norm_room(parts[1])
                existing = rooms.get(room_name)
                if existing and existing.host_addr != client_addr:
                    # Only the original host may re-register (e.g. a retransmit); nobody can take over a room.
                    print(f"[-] Register refused: room '{room_name}' already owned by {existing.host_addr}")
                    sock.sendto(b"ERROR room_taken", client_addr)
                    continue
                if existing:
                    existing.last_active = now
                else:
                    rooms[room_name] = Room(room_name, client_addr)
                    print(f"[+] Room created: '{room_name}' by Host {client_addr[0]}:{client_addr[1]}")
                sock.sendto(f"WAITING {room_name}".encode("utf-8"), client_addr)

            elif cmd == "JOIN" and len(parts) >= 2:
                room_name = norm_room(parts[1])
                room = rooms.get(room_name)
                if not room:
                    print(f"[-] Join failed: room '{room_name}' not found for {client_addr[0]}:{client_addr[1]}")
                    sock.sendto(b"ERROR room_not_found", client_addr)
                elif client_addr == room.host_addr:
                    sock.sendto(b"ERROR cannot_join_own_room", client_addr)
                elif room.guest_addr is not None and room.guest_addr != client_addr:
                    # Room is already paired: refuse instead of letting a third party steal the guest slot.
                    print(f"[-] Join refused: room '{room_name}' is full ({client_addr[0]}:{client_addr[1]})")
                    sock.sendto(b"ERROR room_full", client_addr)
                else:
                    room.guest_addr = client_addr
                    room.last_active = now
                    print(f"[+] Guest {client_addr[0]}:{client_addr[1]} joined room '{room_name}'")
                    
                    # Inform host about guest
                    msg_for_host = f"PEER {client_addr[0]} {client_addr[1]} {room_name}".encode("utf-8")
                    sock.sendto(msg_for_host, room.host_addr)

                    # Inform guest about host
                    msg_for_guest = f"PEER {room.host_addr[0]} {room.host_addr[1]} {room_name}".encode("utf-8")
                    sock.sendto(msg_for_guest, client_addr)
                    print(f"[*] Sent peer endpoints for hole-punching: Host={room.host_addr} <-> Guest={client_addr}")

            elif cmd == "PING":
                sock.sendto(b"PONG", client_addr)

            elif cmd == "CLOSE" and len(parts) >= 2:
                room_name = norm_room(parts[1])
                room = rooms.get(room_name)
                if room and room.host_addr == client_addr:
                    del rooms[room_name]
                    print(f"[*] Room closed: '{room_name}'")
                    sock.sendto(b"OK", client_addr)
                elif room:
                    sock.sendto(b"ERROR not_room_host", client_addr)

        except KeyboardInterrupt:
            print("\nShutting down rendezvous server.")
            break
        except Exception as e:
            print(f"[!] Server error: {e}", file=sys.stderr)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ISS 2000 P2P Matchmaking & Relay Broker")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=9100,
                        help="UDP port (default: 9100; 9099 is the game's own host/LAN port)")
    args = parser.parse_args()

    run_server(args.host, args.port)
