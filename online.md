# ISS 2000 Recompiled — Online 1vs1 P2P Architecture, Work Notes & Guide

This document contains the architecture, technical work notes, engineering changelog, and user guide for the **1vs1 Online P2P multiplayer** implementation in **Mabecs International Superstar Soccer 2000**.

---

## 1. System Architecture: UDP Hole Punching with LAN Discovery

Inspired by the multi-machine networking architecture of [F-ZERO-GLINUX](https://github.com/Mabec-games/F-ZERO-GLINUX), ISS 2000 utilizes Dolphin's public NAT traversal (STUN) network combined with zero-latency local LAN broadcast:

```
                      +------------------------------------------+
                      |   Public STUN Traversal Coordinator      |
                      |        stun.dolphin-emu.org:6262         |
                      +------------------------------------------+
                                    ^              ^
               1. HelloFromClient   |              | 2. ConnectPlease
               Assigns 8-hex HostID |              |    (decodes 4 soccer words)
               "009de90a"           |              |
                                    v              v
                        +---------------+      +---------------+
                        |  Host (P1)    |      |  Guest (P2)   |
                        +---------------+      +---------------+
                                |                      |
                                +======================+
                                  3. Direct UDP Punch
                                     & Auto-LAN Upgrade
                                     (LAN path when available)
```

### Key Pillars:
1. **Internet Matchmaking**: Uses `stun.dolphin-emu.org:6262` for UDP hole punching. Success depends on both networks allowing compatible UDP traversal; this implementation has no relay fallback.
2. **Region-Tagged Pass Phrases**: Hosts choose `EU`, `NA`, `SA`, `ASIA`, `OC`, or `AF` and share a prefix, for example `EU-STRIKER-VOLLEY-HEADER-GOAL`. The four soccer words encode the 8-hex traversal host code; the region is a human-readable label and does not select a different traversal server.
3. **Local LAN Discovery**: Hosts announce and guests query on UDP broadcast port 9099. Direct LAN play can avoid a WAN hairpin when broadcast is available on the local network.
4. **Buffered Input Synchronization**: Packets carry up to 12 recent input frames, with 100ms peer status heartbeats reporting frame progress. If either side falls behind, gameplay waits while status and recovery probes continue; after 5 seconds without recovery, the match exits back to the launcher.
5. **VI Clock Pacing**: Peers exchange retrace counters and allow a small lead of `max(3, frame_delay + 2)` VI ticks before waiting for the other side. Each online tick still uses the ROM's 50/60 Hz interval, so packet delay does not trigger catch-up bursts or a network round trip on every single frame.
6. **Direct IP Fallback**: Players can also type a direct IP or hostname in the Join field if preferred.

---

## 2. Work Notes & Problem Resolutions

### Issue 1: "Nothing connects between two different machines"
* **Diagnosis**: The initial prototype was configured to connect to `127.0.0.1:9099`. When running on two separate physical computers, each machine attempted to contact its own local loopback interface, meaning the guest could not locate the host without a manually configured external IP.
* **Resolution**:
  * Adopted the Dolphin Netplay Traversal protocol communicating with `stun.dolphin-emu.org:6262`.
  * The host registers with the STUN coordinator upon clicking "Host", obtaining an official 8-hex `HostId`.
  * The guest sends `ConnectPlease` with that `HostId`. The STUN coordinator instructs both peers to exchange UDP punch packets, penetrating NAT firewalls on both sides.
  * Added UDP broadcast discovery on port 9099 (`255.255.255.255`) as a parallel local discovery mechanism.

---

### Issue 2: Button Layout Covering Text (`Screenshot_20261027_175605.png`)
* **Diagnosis**: The bottom panel was only 260px tall, squeezing the title, input box, live status text, and four buttons into a cramped space. The red error message ("Could not connect to room...") collided directly with the top edge of the buttons (only 10px margin).
* **Resolution**:
  * Increased the bottom panel backdrop height from 260px to 330px, shifting content upward into the empty dark space.
  * Widened the input/display box to 720px.
  * Positioned status and error messages at `vh - 138`, providing **53px clearance above the buttons** (`vh - 85`) and 23px below the input box.
  * Verified zero overlapping across all states (idle, connecting, error, connected).

---

### Issue 3: Generic Words vs. Soccer-Themed Pass Phrases
* **Diagnosis**: The user requested soccer-themed unique pass phrases instead of generic BIP-39 words.
* **Resolution**:
  * Created a curated 256-word dictionary in [src/iss_words.h](src/iss_words.h) covering:
    * **Positions**: `STRIKER`, `KEEPER`, `WINGER`, `DEFENDER`, `MIDFIELD`, `CAPTAIN`, `SWEEPER`, `PLAYMAKER`
    * **Skills**: `DRIBBLE`, `VOLLEY`, `HEADER`, `TACKLE`, `BACKHEEL`, `NUTMEG`, `BICYCLE`, `CHIP`
    * **Rules/Events**: `GOAL`, `OFFSIDE`, `PENALTY`, `CORNER`, `FREEKICK`, `HATTRICK`, `CLEANSHEET`
    * **Field**: `STADIUM`, `ARENA`, `PITCH`, `CROSSBAR`, `POST`, `NET`, `GOALLINE`
    * **Konami ISS Legends**: `KONAMI`, `MALCOLM`, `CARBONI`, `SIEKE`, `MURILLO`, `KASTLE`, `BAGGIO`, `ZIDANE`, `RONALDO`, `NEDVED`
  * Exactly 256 words means each byte ($0x00 .. 0xFF$) of the 4-byte STUN host ID maps 1:1 to a soccer word. New codes add the selected region before those four words; older unprefixed codes still decode.
  * $256^4 = 4,294,967,296$ unique combinations.
  * Decoder accepts case-insensitive input, spaces, hyphens, and underscores, with or without a supported region prefix.

---

### Issue 4: Yellow Bounding Frame Too Small (`Screenshot_20261027_182958.png`)
* **Diagnosis**: In the Host screen, the yellow outline box was too narrow for long 4-word combinations such as `COMEBACK-UNDERLAP-WINNER-FULLBACK`, causing the outer letters to spill outside the border.
* **Resolution**:
  * Removed the yellow bounding box (`outline_rect` and `fill_rect`) in the Host screen.
  * The pass phrase is now drawn centered in gold text with adaptive font sizing: uses `big` font (38px) when width fits within 90% of screen width, and gracefully falls back to `normal` font (22px) for unusually long combinations.
  * The Join screen input field was similarly updated to auto-scale text size to prevent overflow.

---

### Issue 5: Sluggish Connection and Lagging Audio on Local Machines
* **Diagnosis**: When testing two machines in the same room, the game suffered from severe frame stutter and distorted/crackling audio. Root causes identified:
  1. **NAT Hairpinning**: STUN told both machines to connect via their public WAN IP. The router had to loop WAN packets back to the local LAN, causing 30–80ms latency spikes and high jitter.
  2. **Audio Buffer Underrun**: `reset_audio()` in [src/main.cpp](src/main.cpp) configured `desired.samples = 0x100` (256 samples $\approx$ 5.3ms). Any tiny network hiccup emptied the audio buffer completely, producing clicks, pops, and audio lag.
  3. **80ms CPU Lockstep Stalling**: In [src/iss_netplay.cpp](src/iss_netplay.cpp), `poll_and_sync` blocked the main CPU thread for up to 80ms whenever a packet was late. Because the N64 game thread generates audio, stalling the CPU thread starved SDL's audio queue.
* **Resolution**:
  1. **LAN discovery**: Added broadcast announcements and queries plus private-address exchange. A direct LAN path is attempted where routing allows it; latency depends on the local network.
  2. **Audio Buffer Expansion**: Increased `desired.samples` in `reset_audio()` from 256 to **1024 samples (about 21ms at 48 kHz)**. The audio queue accepts new data while less than 100ms is queued.
  3. **12-Frame Input Redundancy**: Expanded `inputs[12]` in `NetPacket`. Every packet carries the current frame plus the last 11 frames of inputs. Dropped packets are recovered instantly on the next frame.
  4. **Missing-frame wait**: The game waits for the required delayed input frame while continuing to poll the socket and sending recovery probes. If it does not arrive within 5 seconds, the session returns to the launcher. Long pauses can drain queued audio.
  5. **VI Clock Pacing**: Each peer reports its VI count and may lead by `max(3, frame_delay + 2)` ticks before waiting. Online VI ticks use a relative 50/60 Hz interval, avoiding absolute-clock catch-up bursts after network waits.

### Issue 6: Match Connects, but Frame Rate Drops After VI Synchronization
* **Observed (2026-10-27)**: The AppImage connected successfully and the log showed a LAN route upgrade from the public address to `192.168.1.10:9099`. Audio was reported clear, while match frame rate lagged. An earlier build also produced repeated bogus audio-buffer-length warnings and did not respond to controller input.
* **Diagnosis**: Waiting for the peer on every VI made network latency part of every frame interval. The first relative-pacing fix removed catch-up bursts, but still imposed a strict peer barrier on every VI.
* **Changes**:
  * VI counters are exchanged each tick, but a peer can lead by `max(3, frame_delay + 2)` VIs before it waits. The existing delayed-input synchronization remains responsible for controller input ordering.
  * Online VI pacing waits one ROM-rate interval (50 Hz PAL / 60 Hz NTSC) after the previous synchronized tick, avoiding catch-up bursts after a wait.
  * Handshake retries are limited to one every 200ms while connecting. Duplicate handshake packets no longer print repeated “Handshake completed” messages.
  * Rebuilt `dist/Mabecs_ISS2000_Widescreen-x86_64.AppImage` with these changes.
* **Still to verify**: Run a live two-peer match and check frame rate, controller response, long-session sync, and disconnect handling. The latest AppImage was compiled and packaged, but not tested in a live match.

### Issue 7: Matches Drift Out of Sync
* **Diagnosis (2026-10-27)**: The input lockstep was sound, but the two machines did not run the game identically:
  1. **Clock-seeded RNG**: the game seeds its RNGs from `osGetCount` (US `func_80008F20` -> LFSR at `0x800A7878`, `func_80016448` -> `0x800A7AE8`), re-seeding from menus and the match overlay. Each machine read its own host clock, so AI and ball decisions diverged.
  2. **Controller Pak**: each machine loaded its own local pak save (options, edited data) into the game.
  3. **Machine-local settings written into game memory**: the widescreen cull extents (from window shape, updated by the graphics thread) and the player LOD option.
  4. Direct IP mode never worked: `--host-direct` bound a random port and never sent the handshake that carries the session token.
* **Changes**:
  * Game calls to `osGetCount` are renamed to `iss_game_osGetCount_recomp` (objcopy `--redefine-sym` in CMakeLists.txt). Online, it returns `session token + input frame * 1562500 + per-thread call index * 64`, identical on both peers. Offline it returns the real clock.
  * Online sessions run without Controller Paks, with a fixed 21:9 cull width and full player detail.
  * **Desync detection**: every input packet carries a hash of the RNG states taken when that frame's input was polled. A mismatch logs `[Netplay] DESYNC detected at input frame N`. `ISS_NET_DEBUG=1` also logs the hash every 300 frames.
  * Direct host binds the requested port and sends its handshake.
* **Follow-up (2026-10-27, live LAN test: network sync fine, audio stutter, no pak)**:
  * Controller Pak online: a separate `controllerpak_online_p1*.mpk` in port 1 only (in the runtime data folder, e.g. `~/.local/share/iss2000recomp/`). Both machines start from the same formatted pak and write the same saves, so the files stay identical if they are only used between the same two machines. The log prints `[pak] online pak checksum XXXXXXXX`, which must match on both. To start over, delete the file on both machines, or copy one machine's file to the other.
  * Audio: online play keeps a 6-VI audio cushion (`ISS_AUDIO_CUSHION` still overrides) and raises the drop limit to match. The queue goes from 21-43 ms to 88-110 ms, so short waits for the peer no longer empty it, at the cost of roughly 70 ms more audio latency.
* **Verified**: a loopback direct-IP session (two headless instances) connected and stayed in lockstep for 2100+ frames with matching hashes. That run stayed on the title screen; a real two-machine match is still to be tested.

---

## 3. Protocol & Packet Specifications

### Traversal Protocol (Dolphin STUN, port 6262)
Total size: 37 bytes (`#pragma pack(push, 1)`).
* `type = 2 (HelloFromClient)`: Host registers with `stun.dolphin-emu.org:6262`.
* `type = 3 (HelloFromServer)`: Server returns 8-character `yourHostId` and external IP:port. Host replies with `type = 0 (Ack)`.
* `type = 1 (Ping)`: Host sends keep-alive every 3 seconds to keep router NAT port open.
* `type = 4 (ConnectPlease)`: Guest sends target `hostId` to `stun.dolphin-emu.org:6262`.
* `type = 5 (PleaseSendPacket)`: Server instructs Host to punch UDP to Guest's public IP:port.
* `type = 6 (ConnectReady)`: Server gives Guest Host's public IP:port.
* `type = 7 (ConnectFailed)`: Host not found or timed out.

### Game Netplay Packet (`NetPacket`, 84 bytes)
```cpp
#pragma pack(push, 1)
struct NetPacket {
    uint32_t magic;           // 0x4E535349 ('ISSN')
    uint8_t type;             // Includes input, status, handshake and punch packets
    uint32_t base_frame;      // Input sequence or status frame
    uint8_t input_count;      // Number of redundant inputs (up to 12)
    NetInputState inputs[12]; // 12 past frames of controller inputs (48 bytes)
    char rom_version[8];      // ROM version hash check (e.g. "US", "E_ENDE")
    uint32_t seed;            // Per-session packet token after handshake
    uint32_t sender_lan_ip;   // Local LAN IP in network byte order
    uint16_t sender_lan_port; // Local UDP port in network byte order
    uint32_t check_frame;     // PKT_INPUT: frame of check_hash
    uint32_t check_hash;      // PKT_INPUT: RNG-state hash for desync detection
};
#pragma pack(pop)
```

### LAN Discovery Broadcast (Port 9099)
* **Host Announcement**: Sends UDP broadcast to `255.255.255.255:9099` every 1.2s:
  `ISS_LAN_ANNOUNCE <region-prefixed-soccer-phrase> <port>`
* **Guest Query**: Broadcasts UDP query to `255.255.255.255:9099`:
  `ISS_LAN_QUERY <region-prefixed-soccer-phrase>`
* When a matching phrase is received on LAN, both sides immediately initiate direct P2P handshake.

---

## 4. User Guide: How to Play Online

### Using the AppImage (Recommended)
Launch the portable AppImage on both machines:
```bash
./dist/Mabecs_ISS2000_Widescreen-x86_64.AppImage
```

#### Machine 1 — Host:
1. Click the green **"Online"** button on the main menu.
2. Click **"Host 1vs1 Match (Get Soccer Words)"**.
3. Choose a region in the Host screen; the launcher contacts the traversal server and displays a region-prefixed code (e.g. `EU-STRIKER-VOLLEY-HEADER-GOAL`).
4. Click **"Copy Pass Phrase"** to copy it to clipboard.
5. Share the phrase with your friend via chat/Discord.
6. Once your friend connects, the launcher automatically starts the match in 1vs1 P2P mode!

#### Machine 2 — Join:
1. Click the green **"Online"** button on the main menu.
2. Click **"Join 1vs1 Match (Enter Soccer Words)"**.
3. Paste the Host code (click **"Paste Clipboard"** or Ctrl+V), or enter a direct IP/hostname.
4. Click **"Connect & Play"** (or press Enter).
5. The launcher negotiates through UDP traversal and LAN discovery, then boots into the match. Region prefixes are accepted when joining; older four-word codes remain valid.

---

## 5. Command-Line Options

For automated testing or direct headless execution:
* **Host Online Match**:
  ```bash
  ./build/ISS2000Recompiled baserom.z64 --no-launcher --host
  ```
* **Join via Soccer Words**:
  ```bash
  ./build/ISS2000Recompiled baserom.z64 --no-launcher --join EU-STRIKER-VOLLEY-HEADER-GOAL
  ```
* **Direct IP Host & Join**:
  ```bash
  # Machine 1:
  ./build/ISS2000Recompiled baserom.z64 --no-launcher --host-direct 9099
  # Machine 2:
  ./build/ISS2000Recompiled baserom.z64 --no-launcher --join-direct 192.168.1.50:9099
  ```
* **Local 2-Player (Single Machine, 2 Controllers)**:
  ```bash
  ./build/ISS2000Recompiled baserom.z64 --no-launcher --local-p2
  ```

---

## 6. Build & Packaging Commands

* **Host Native Build**:
  ```bash
  make -C build -j$(nproc)
  ```
* **Portable Debian 12 Container Build**:
  ```bash
  tools/debian12.sh ninja -C build-deb12
  ```
* **Package Portable AppImage**:
  ```bash
  tools/make_appimage.sh
  ```
  Output AppImage: `dist/Mabecs_ISS2000_Widescreen-x86_64.AppImage`
