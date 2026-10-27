# Feature 02: Central Tracker Registry & Dynamic Peer Discovery

## 1. Overview and Purpose

In a pure peer-to-peer network, peers cannot know the IP addresses and port numbers of other active peers in advance. The **Tracker** ([`src/tracker.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c)) serves as a lightweight, centralized rendezvous service. 

Peers register their listening endpoints with the tracker for a given `file_id` and receive a dynamic list of other peers participating in the same transfer.

> [!IMPORTANT]
> The Tracker **never routes, receives, or stores actual file data**. It is strictly a control-plane directory service. All binary piece transfers occur directly between peers over separate TCP sockets.

---

## 2. Technical Architecture & Data Structures

### Tracker Entry Structure ([`src/tracker.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c#L105-L110))
```c
typedef struct {
    char file_id_hex[65];
    char ip[64];
    uint16_t port;
    time_t last_seen;
} tracker_entry_t;
```

### Peer Endpoint Definition ([`include/tracker.h`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/include/tracker.h#L6-L9))
```c
typedef struct {
    char ip[64];
    uint16_t port;
} peer_endpoint_t;
```

---

## 3. Communication Protocol

The tracker listens on a configurable TCP port (e.g. `16000`). It uses a simple, robust text-based protocol:

### Request Format:
```text
ANNOUNCE <file_id_hex> <listening_port>\n
```
*Example:* `ANNOUNCE 58170a638ee3719dc46c68809821a3bf1f70966ead3de730cd4a21dab0efbfc6 17002\n`

### Response Format:
```text
PEERS <peer_count>\n
<peer_1_ip> <peer_1_port>\n
<peer_2_ip> <peer_2_port>\n
...
```
*Example:*
```text
PEERS 2
127.0.0.1 17001
127.0.0.1 17003
```

---

## 4. Key Functions

- [`tracker_announce()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c#L26-L99): Client-side helper called by peers. Connects to tracker socket, issues `ANNOUNCE`, parses `PEERS` response, and populates `peer_endpoint_t peers[]`. Uses a 3-second socket timeout (`SO_RCVTIMEO`) so offline trackers do not freeze the peer.
- [`register_peer()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c#L116-L144): Server-side function protected by `pthread_mutex_t g_tracker_mutex`. Updates `last_seen` timestamp if entry exists, or adds a new active peer endpoint.
- [`handle_client()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c#L146-L187): Reads request, extracts client IP via `getpeername()` / `inet_ntop()`, registers the endpoint, and returns all peers associated with the specified `file_id_hex`.

---

## 5. Flow Diagram

```text
Peer (Port 17002)                                  Tracker (Port 16000)
       |                                                    |
       |--- TCP Connect ----------------------------------->|
       |--- "ANNOUNCE <file_id_hex> 17002\n" -------------->|
       |                                                    | (Lock Mutex)
       |                                                    | (Register/Update Peer)
       |                                                    | (Collect matching peers)
       |                                                    | (Unlock Mutex)
       |<-- "PEERS 2\n127.0.0.1 17001\n127.0.0.1 17003\n"---|
       |--- TCP Close ------------------------------------->|
       |
       +---> Peer then connects directly to 127.0.0.1:17001 and 127.0.0.1:17003
```

---

## 6. Viva Defense Questions & Answers

### Q1: What is the purpose of the Tracker in BitTorrent?
**Answer:**
The Tracker solves the **peer discovery problem**. Without a central directory or distributed hash table (DHT), peers entering the network would not know where other participants are located. The tracker provides a rendezvous point where peers register their listening port and discover other active peers interested in the same `file_id`.

### Q2: Does the tracker handle file piece uploads and downloads?
**Answer:**
**No.** The tracker only handles metadata and endpoint discovery. Routing file data through the tracker would transform the system into an inefficient client-server model, overloading the tracker's network bandwidth. In our implementation, file pieces travel directly between peers over peer-to-peer TCP connections.

### Q3: How does the tracker distinguish between different files?
**Answer:**
Using the 32-byte `file_id` (represented as a 64-character hex string). Multiple swarms for different files can register on the same tracker simultaneously. When answering an `ANNOUNCE` query, the tracker filters its in-memory registry table and only returns peers matching the requested `file_id`.

### Q4: How is concurrency handled in the tracker server?
**Answer:**
The tracker protects its global registry array (`g_registry`) using a POSIX mutex:
```c
static pthread_mutex_t g_tracker_mutex = PTHREAD_MUTEX_INITIALIZER;
```
Whenever an entry is registered, updated, or scanned for peers, `pthread_mutex_lock(&g_tracker_mutex)` is acquired and subsequently released. This prevents race conditions and corrupted registry state when multiple peers announce simultaneously.

### Q5: What happens if the tracker crashes after peers have discovered each other?
**Answer:**
The peers **continue transferring and sharing pieces uninterrupted**. Because peer-to-peer data connections are direct TCP streams between peers, existing connections and ongoing downloads do not depend on the tracker. The only limitation is that new peers would not be able to discover the swarm until the tracker restarts.
