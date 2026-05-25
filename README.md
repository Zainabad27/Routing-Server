# Multi Client Chat Server

A multi-client TCP chat server built from scratch in C++17, developed as a 3rd semester Data Structures & Algorithms course project. The project combines a real-world Linux networking layer with custom-implemented data structures to create a functional group chat system.

---

## Overview

The server runs on **port 9034** and allows multiple clients to connect simultaneously over TCP. When a client connects, they enter their name. From that point they can send and receive messages in real time with all other connected clients — and see the recent chat history from before they joined.

The project is split into two clear layers:

- **Networking Layer** — handles all TCP socket communication using the `poll()` system call, enabling a single-threaded server to manage many clients concurrently without using threads.
- **DSA Layer** — three data structures implemented from scratch that sit on top of the networking layer and give the server intelligent client management, reliable message delivery, and persistent chat history.

---

## Networking Layer

Built by **Zain Ul Abad** using standard POSIX socket APIs on Linux.

The core of the networking layer is the `poll()` system call — an I/O multiplexing technique that allows the server to watch all connected sockets simultaneously and react the moment any one of them has data. This means the server never blocks on a single client while others are waiting.

**Key components:**

- `socket()` / `bind()` / `listen()` — opens port 9034 and starts accepting connections
- `accept()` — completes the TCP three-way handshake for each new client
- `recv()` — reads incoming data from a client socket
- `send()` — delivers data to a client socket
- `poll()` — watches all sockets at once and signals which ones are ready
- `SO_REUSEADDR` — allows the server to restart immediately without waiting for the OS to release the port

---

## Data Structures Layer

Built by **Muhammad Hasan**. All three structures are implemented from scratch for the DSA functionality.

### 1. ClientManager — Hash Table (Open Addressing)

Stores all connected clients keyed by their socket file descriptor.

- **Hash function:** `fd % capacity`
- **Collision resolution:** Linear probing — on collision, walks forward one slot at a time
- **Deletion:** Tombstone marking — deleted slots are flagged rather than cleared, to avoid breaking the probe chain for other keys
- **Rehashing:** When load factor hits 0.7, the table doubles in size and re-inserts all live entries
- **Complexity:** O(1) average for insert, delete, and lookup

Each entry stores the client's fd, IP address, chosen name, connection timestamp, and their personal MessageQueue.

### 2. MessageHistory — Stack (Doubly Linked List)

Stores the last 20 broadcast messages globally on the server.

- **Backing structure:** Doubly linked list — `head` = oldest message, `tail` = newest (top of stack)
- **Push:** Appends to tail in O(1). When full, evicts the oldest message (head) first
- **Pop / Peek:** Operates on tail in O(1)
- **Purpose:** When a new client joins, the stack is replayed to them oldest-to-newest so they can see recent conversation history — similar to Discord's message history on join

Messages are stored pre-formatted as `"Name: text\n"` so history always shows who said what.

### 3. MessageQueue — Queue (Singly Linked List)

Each client has their own personal message queue stored inside their hash table entry.

- **Backing structure:** Singly linked list with separate `front` and `back` pointers
- **Enqueue:** Appends to back in O(1)
- **Dequeue:** Removes from front in O(1)
- **Purpose:** When a message is broadcast, it is enqueued into every recipient's queue before being sent. If a `send()` call fails (slow client, full buffer), the message is not lost — it stays in the queue and is retried on the next loop tick

---

## File Structure

```
Multi-Client-Chat-Server
├── .vscode/
│   ├── c_cpp_properties.json
│   ├── launch.json
│   └── settings.json
├── include/
│   ├── Client.h
│   ├── ClientManager.h
│   ├── MessageHistory.h
│   ├── MessageQueue.h
│   └── Server.h
├── src/
│   ├── ClientManager.cpp
│   ├── MessageHistory.cpp
│   ├── MessageQueue.cpp
│   └── server.h
├── main.cpp
└── README.md

```

---

## How to Run (Linux)

### Requirements

- Linux machine (Arch btw...)
- `g++` with C++17 support
- `netcat` (`nc`) for connecting as a client

Install dependencies if needed:

```bash
sudo apt update
sudo apt install g++ netcat
```

### Build

Clone the repository and compile:

```bash
git clone (https://github.com/muhammad-hasannn/Multi-Client-Chat-Server.git)
cd Multi-Client-Chat-Server
git checkout name-feature

g++ -std=c++17 -o server main.cpp src/server.cpp src/ClientManager.cpp src/MessageHistory.cpp src/MessageQueue.cpp -Iinclude
```

### Run the Server

```bash
./server
```

You should see:

```
pollserver: waiting for connections on port 9034...
ClientManager and MessageHistory initialised
```

### Connect as a Client

Open a new terminal and connect using netcat:

```bash
nc localhost 9034
```

The server will prompt you:

```
Enter your name:
```

Type your name and press Enter. You are now in the chat.

### Test with Multiple Clients

Open several terminals and run `nc localhost 9034` in each one. Each client gets their own name. Messages sent by one client are instantly broadcast to all others with the sender's name prefixed:

```
Zain: hey everyone
Hasan: what's up
```

New clients who join mid-conversation will see the last 20 messages as history before the live chat begins.

### Stop the Server

Press `Ctrl + C` in the server terminal.

---

## Key Concepts Demonstrated

| Concept | Where |
|---|---|
| TCP socket programming | `src/Server.cpp` |
| I/O multiplexing with `poll()` | `src/Server.cpp` |
| Hash table with open addressing | `src/ClientManager.cpp` |
| Linear probing & tombstone deletion | `src/ClientManager.cpp` |
| Dynamic rehashing | `src/ClientManager.cpp` |
| Stack (LIFO) with doubly linked list | `src/MessageHistory.cpp` |
| Queue (FIFO) with singly linked list | `src/MessageQueue.cpp` |
| Separation of concerns (layered architecture) | Project structure |

---

## Developers

| Name | Role | GitHub |
|---|---|---|
| Zain Ul Abad | Networking Layer | [GitHub Profile](https://github.com/Zainabad27) |
| Muhammad Hasan | DSA Integration | [GitHub Profile](https://github.com/muhammad-hasannn) |

---
