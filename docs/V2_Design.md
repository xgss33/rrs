# V2 Design Plan

## Goal

V2 turns the V1 single-IO/single-worker server skeleton into a real multi-threaded server architecture.

The target topology is:

```text
1 Acceptor
2 IOThread
N WorkerThread, default 4, configured by workerThreadCount
```

V2 focuses on architecture correctness first:

- clear fd ownership
- clear room ownership
- clear session ownership
- explicit cross-thread queues
- epoll-based IO
- eventfd-based IO wakeup

Intermediate V2 iterations only need to compile. Full client verification is required at the end of V2.

## Non-Goals

- No lock-free queue in V2.
- No fake reconnect or server-side test-only stale input injection.
- No embedded test logic in production server code.
- No large-scale benchmark requirement.
- No complete binary protocol or Protobuf migration yet.
- No production database persistence.

If a scenario needs testing, it should be produced by an external test client or bot project, not by fake behavior inside server logic.

## Runtime Topology

```text
                  +-------------+
                  |  Acceptor   |
                  +-------------+
                         |
                         | accepted fd + eventfd wake
                         v
          +-------------------------------+
          |                               |
          v                               v
    +-----------+                   +-----------+
    | IOThread0 |                   | IOThread1 |
    +-----------+                   +-----------+
          |                               |
          | inbound by RoomRouter         | inbound by RoomRouter
          v                               v
  +---------------+               +---------------+
  | Worker inbound| ...           | Worker inbound|
  +---------------+               +---------------+
          |                               |
          v                               v
   +-------------+   +-------------+   +-------------+
   | Worker 0    |   | Worker 1    |   | Worker N    |
   +-------------+   +-------------+   +-------------+
          |                 |                 |
          | workerToIo      | workerToIo      | workerToIo
          v                 v                 v
    +-----------+                   +-----------+
    | IOThread0 |                   | IOThread1 |
    +-----------+                   +-----------+
```

## Thread Ownership

### Acceptor

Owns:

- listen socket
- accept loop
- IOThread assignment policy

Does not own:

- client fd lifetime after handoff
- epoll client registration
- session state
- room state

Acceptor handoff rule:

```text
accept fd -> choose IoThreadId -> push fd to IOThread accepted-fd queue -> write eventfd
```

### IOThread

Owns:

- assigned client fds
- epoll fd
- eventfd
- local fd-to-session cache
- protocol parsing
- per-client outbound queue
- local send/drop accounting

Does not own:

- room state
- player entity state
- worker tick
- global session truth

IOThread writes inbound messages to worker queues and drains outbound messages addressed to itself.

### WorkerThread

Owns:

- assigned rooms
- room tick
- room input consumption
- player entities
- snapshot generation
- generation validation against inbound messages

Does not own:

- socket fd
- epoll
- client send buffer
- local IO session cache

WorkerThread consumes its own inbound queue and writes outbound messages to the target IO queue.

## Queue Topology

V2 uses the C plan for worker-to-IO output routing.

### Worker Inbound

Each worker has one inbound queue:

```text
workerInbound[workerId]
```

IOThread routes client commands by room ownership:

```text
RoomId -> WorkerId -> workerInbound[workerId]
```

### Worker To IO Outbound

Each worker has one outbound queue per IOThread:

```text
workerToIo[workerId][ioThreadId]
```

Worker output flow:

```text
Worker generates OutboundMessage
Worker resolves SessionId -> IoThreadId
Worker pushes to workerToIo[workerId][ioThreadId]
IOThread drains queues addressed to itself
```

For the V2 scale:

```text
4 workers * 2 IOThreads = 8 outbound queues
```

This is acceptable and avoids multiple IOThreads competing for the same outbound queue.

### Queue Implementation

V2 keeps the existing mutex-based `ThreadSafeQueue`.

Reason:

- correctness first
- easier debugging
- current throughput target is architectural validation, not final performance

Future queue options:

- bounded queue
- input coalescing
- lock-free queue
- spin lock for short critical sections

These are not part of V2 unless a later profiling result requires them.

## Session Architecture

V1 placed `SessionManager` inside `IOThread`. V2 changes this because multiple IOThreads make local-only session ownership insufficient.

Example problem:

```text
first connection assigned to IOThread0
Session exists only inside IOThread0
client disconnects
new connection assigned to IOThread1
IOThread1 cannot resume old Session
```

### Global SessionRegistry

V2 introduces a global `SessionRegistry`.

It owns the authoritative session state:

```text
SessionId -> Session
PlayerId -> SessionId
SessionId -> IoThreadId
```

Locking:

```text
std::shared_mutex
read: shared_lock
write: unique_lock
```

Expected operations:

- create or find session
- resume session
- bind session to IOThread owner
- mark disconnected
- query owner IOThread
- query session snapshot

### IOThread Local Session Cache

Each IOThread keeps local session/cache state for hot fd paths.

Local cache examples:

```text
fd -> SessionId
SessionId -> local Session snapshot
```

Rules:

- local cache is owned by one IOThread
- local cache does not need locks
- global registry is authoritative
- global writes require unique lock
- global reads use shared lock

This gives:

- fast local IO path
- global consistency for reconnect/resume
- worker-readable session owner routing

### Worker Session Access

Workers do not own or mutate session state.

Workers may read global session routing data:

```text
SessionId -> IoThreadId
```

This read uses shared lock.

Worker uses it to choose:

```text
workerToIo[workerId][ioThreadId]
```

## Room Routing

V2 needs an explicit room-to-worker rule.

Initial rule can be deterministic:

```text
workerId = roomId % workerCount
```

or:

```text
workerId = (roomId - 1) % workerCount
```

The second rule is easier for room ids starting at 1.

Rules:

- each room has exactly one owner WorkerThread
- IOThread never mutates room directly
- IOThread only routes inbound messages to the owner worker
- WorkerThread only ticks rooms it owns

Later this can become a `RoomRouter` class with dynamic room migration.

## IO Model

V2 moves IOThread from polling all clients every 1ms to epoll.

Each IOThread owns:

```text
epoll fd
eventfd
accepted fd queue
client fd map
```

### eventfd Usage

eventfd wakes an IOThread when another thread gives it work.

Primary wake sources:

- Acceptor assigns a newly accepted fd
- Worker produces outbound messages for that IOThread

Minimum flow:

```text
producer thread:
  queue.push(item)
  eventfd_write(io.eventFd)

IOThread:
  epoll_wait()
  if eventfd readable:
      drain eventfd
      drain accepted fd queue
      drain outbound queues
```

### epoll Usage

IOThread epoll listens to:

- eventfd
- client socket fds

Initial epoll mode:

```text
level-triggered
```

Reason:

- simpler correctness
- less risk of missed reads/writes
- easier V2 debugging

Events:

- `EPOLLIN`: read client data
- `EPOLLRDHUP`: peer closed
- `EPOLLERR`: socket error
- `EPOLLOUT`: optional later for partial-send handling

## Protocol Direction

V2 should remove fake reconnect behavior.

Current V1 command:

```text
RECONNECT
```

Problem:

- it runs on the same TCP connection
- it injects fake stale input server-side
- it is a generation test, not real reconnect

V2 target:

```text
RESUME <sessionId>
```

Expected real flow:

```text
client A connects
JOIN creates Session
client A disconnects
Session marked Disconnected
client B connects as new TCP fd
RESUME <sessionId>
Session generation++
Session owner IoThreadId updated
Worker receives BindPlayer with new generation
```

Server code must not fabricate stale input for testing.

Generation rejection should be tested by an external client scenario if needed.

## V2 Iteration Plan

### V2.1 Runtime Topology

Goal:

- introduce real multi-thread component topology

Tasks:

- add `IoThreadId` and `WorkerId`
- fixed 2 IOThreads
- `workerThreadCount` controls WorkerThread count, default 4
- create multiple WorkerThread instances
- create multiple IOThread instances
- assign static rooms across workers
- keep implementation compile-safe

Validation:

- compile only

### V2.2 RuntimeChannels

Goal:

- replace single V1.5 `MessageChannels` with multi-worker/multi-IO channels

Tasks:

- add `RuntimeChannels`
- add `workerInbound[workerId]`
- add `workerToIo[workerId][ioThreadId]`
- IOThread can route inbound to worker inbound queue
- WorkerThread consumes only its own inbound queue
- WorkerThread writes output to `workerToIo[workerId][ioThreadId]`

Validation:

- compile only

### V2.3 Global SessionRegistry And Local Cache

Goal:

- support sessions across multiple IOThreads

Tasks:

- add `SessionRegistry`
- use `std::shared_mutex`
- global maps:
  - `SessionId -> Session`
  - `PlayerId -> SessionId`
  - `SessionId -> IoThreadId`
- IOThread keeps local fd/session cache
- disconnect marks session as `Disconnected`
- IO local cache remains lock-free

Validation:

- compile only

### V2.4 Room And Worker Routing

Goal:

- route commands to owner workers

Tasks:

- add static room-to-worker routing
- WorkerThread only creates rooms assigned to itself
- IOThread routes `JOIN` and `INPUT` by room owner
- Worker output resolves target IOThread through `SessionRegistry`

Validation:

- compile only

### V2.5 Acceptor To IO eventfd Handoff

Goal:

- remove direct acceptor manipulation of IO internals

Tasks:

- each IOThread owns accepted fd queue
- each IOThread owns eventfd
- Acceptor selects IOThread round-robin
- Acceptor pushes accepted fd to target queue
- Acceptor wakes target IOThread using eventfd
- IOThread drains accepted fd queue after wake

Validation:

- compile only

### V2.6 IOThread epoll And Real Resume

Goal:

- complete V2 runtime behavior

Tasks:

- IOThread uses epoll for eventfd and client fds
- remove fake `RECONNECT` server logic
- add real `RESUME <sessionId>`
- implement new TCP connection resume path
- generation increments on resume
- Worker receives new-generation bind
- external bot/client verifies the complete flow

Validation:

- compile
- run server
- run external test client once
- verify:
  - two IOThreads receive clients
  - worker count matches config
  - rooms are owned by workers
  - JOIN / INPUT / snapshot path works
  - disconnect then RESUME works
  - generation increments
  - server shuts down cleanly

## Documentation Rule

V2 implementation notes start at:

```text
V2.1.md
V2.2.md
...
```

`V2_Design.md` is the overall V2 design plan and should remain the stable reference for the V2 direction.

## Final V2 Acceptance

V2 is accepted when:

- server starts with 1 Acceptor, 2 IOThreads, and configured WorkerThreads
- IOThreads use epoll
- Acceptor uses eventfd handoff
- IO-to-Worker routing uses worker inbound queues
- Worker-to-IO routing uses `workerToIo[workerId][ioThreadId]`
- global SessionRegistry supports cross-IO resume
- fake reconnect logic is removed
- external client verifies the full flow once
