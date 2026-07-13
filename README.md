# rrs

Tick-driven room-based realtime multiplayer server.

Current scope:

- C++20 project structure
- Basic domain id types
- Basic application config
- Basic logger facade backed by `spdlog`
- Worker thread dynamically owning rooms
- 30Hz room tick loop based on `std::chrono::steady_clock`
- Limited catch-up for room ticks
- Mailbox-based IO-to-worker and worker-to-IO messages
- Room-side pending input consumption during tick
- Minimal player entity state
- Full room snapshot generation
- IO thread owning fd/session binding cache
- Session generation validation for stale input rejection
- Minimal TCP accept path and binary protocol
- Worker to IO snapshot path with bounded per-client socket queue
- Continuous server process with signal-based shutdown
- Gameplay pressure profile: 16 players per room, 1024 food entities per room, 30Hz snapshots
- Snapshot food sync uses fixed food ids: JOIN/RECONNECT sends all foods, normal tick snapshots send changed foods only

Source layout:

- `app`: application entry and config
- `base`: shared strong id types
- `game`: room gameplay state and room snapshots
- `net`: TCP acceptor, IO thread, fd routing, binary protocol
- `runtime`: sessions, worker runtime, mailboxes, cross-thread messages
- `log`: logger facade

Build with CMake:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Run the server:

```bash
./build/rrs
```

Run pressure tests with most per-client info logs disabled while keeping metrics:

```bash
./build/rrs --io 3 --worker 1 --log warn
```

Stop the server with `Ctrl+C` or `SIGTERM`.

Supported binary client messages over TCP:

- Frame format: 4-byte big-endian length, 1-byte message type, N-byte payload.
- Client message types: `JOIN = 1`, `RECONNECT = 2`, `INPUT = 3`, `LEAVE = 4`.
- Server message types: `JOIN_OK = 101`, `RECONNECT_OK = 102`, `SNAPSHOT = 103`, `ERROR = 104`.

The real load-test client is intentionally separated into `../RealtimeRoomLoadClient`.

Build and run load tests from that project, not from this server repository.

```bash
cd ../RealtimeRoomLoadClient
cmake -S . -B build -G Ninja
cmake --build build
```
