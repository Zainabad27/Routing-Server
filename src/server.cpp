// ============================================================
//  Poll-based TCP Broadcast Server  —  Proper C++ Version
//  Original networking logic unchaged
//  DSA integration added on top of networking layer.
//
//  Build (Linux):
//    g++ -std=c++17 -o server src/Server.cpp src/main.cpp
//        src/ClientManager.cpp src/MessageHistory.cpp

//  Run:   ./server
//  Port:  9034
// ============================================================

// Linux-only networking headers
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

// C++ standard headers
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>


#include "../include/Server.h"
#include "../include/ClientManager.h"
#include "../include/MessageHistory.h"

// constants
static constexpr const char* PORT        = "9034";
static constexpr int         BUFFER_SIZE = 256;
static constexpr int         MAX_CLIENTS = 500;
static constexpr int         BACKLOG     = 10;

// ============================================================
//  HELPER: addressToString
//  Converts a sockaddr_storage (IPv4 or IPv6) into a
//  human-readable IP string such as "192.168.1.5".
// ============================================================
static std::string addressToString(sockaddr_storage& addr){
    char buf[INET6_ADDRSTRLEN];

    switch (addr.ss_family) {
        case AF_INET: {
            auto* sa4 = reinterpret_cast<sockaddr_in*>(&addr);
            inet_ntop(AF_INET, &sa4->sin_addr, buf, sizeof buf);
            break;
        }
        case AF_INET6: {
            auto* sa6 = reinterpret_cast<sockaddr_in6*>(&addr);
            inet_ntop(AF_INET6, &sa6->sin6_addr, buf, sizeof buf);
            break;
        }
        default:
            return "(unknown address family)";
    }
    return std::string(buf);
}

// ============================================================
//  CORE SETUP: createListenerSocket
//  getaddrinfo → socket → setsockopt → bind → listen
//  Returns the file descriptor, or -1 on failure.
// ============================================================
static int createListenerSocket(){
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    addrinfo* results = nullptr;
    int rv = getaddrinfo(nullptr, PORT, &hints, &results);
    if (rv != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(rv) << "\n";
        exit(1);
    }

    int listenerFd = -1;
    int yes = 1;

    for (addrinfo* p = results; p != nullptr; p = p->ai_next) {
        listenerFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenerFd < 0)
            continue;

        setsockopt(listenerFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

        if (bind(listenerFd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listenerFd);
            continue;
        }
        break;
    }

    freeaddrinfo(results);

    if (listenerFd == -1)
        return -1;

    if (listen(listenerFd, BACKLOG) == -1)
        return -1;

    return listenerFd;
}

// ============================================================
//  CONNECTION MANAGEMENT: addConnection / removeConnection
//  Manages the pollfd vector that poll() watches.
// ============================================================
static void addConnection(std::vector<pollfd>& pfds, int fd){
    pollfd entry{};
    entry.fd = fd;
    entry.events = POLLIN;
    pfds.push_back(entry);
}

static void removeConnection(std::vector<pollfd>& pfds, int index){
    pfds[index] = pfds.back();
    pfds.pop_back();
}

// ============================================================
//  DSA HELPER: replayHistory
//  Sends all messages stored in MessageHistory to a newly
//  connected client so they can catch up on the conversation.
//  We make a temporary copy of the history to preserve it.
// ============================================================
static void replayHistory(int newFd, MessageHistory& history){
    if (history.isEmpty())
        return;

    // Collect messages from the stack (top = most recent)
    // We want to send oldest-first so we reverse into a temp buffer
    std::vector<std::string> msgs;
    msgs.reserve(static_cast<size_t>(history.size()));

    // Temporarily drain and re-push to read all messages in order
    // (stack is LIFO, so we pop into a vector then push back)
    while (!history.isEmpty())
        msgs.push_back(history.pop());

    // Push back so the stack is unchanged after replay
    for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; i--)
        history.push(msgs[i]);

    // Send oldest → newest (reverse of how we popped)
    std::string header = "--- chat history ---\n";
    send(newFd, header.c_str(), static_cast<int>(header.size()), 0);

    for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; i--) {
        send(newFd, msgs[i].c_str(), static_cast<int>(msgs[i].size()), 0);
    }

    std::string footer = "--- end of history ---\n";
    send(newFd, footer.c_str(), static_cast<int>(footer.size()), 0);
}

// ============================================================
//  EVENT HANDLER: handleNewConnection
//  Called when poll() signals the listener socket is readable.
//  accept() completes the TCP handshake.
//
//  DSA additions:
//    • Registers the client in ClientManager (hash table)
//    • Replays MessageHistory to the new client
//  — Zain's networking logic preserved exactly —
// ============================================================
static void handleNewConnection(int listenerFd, std::vector<pollfd>& pfds, ClientManager& clients, MessageHistory& history){
    sockaddr_storage clientAddr{};
    socklen_t addrLen = sizeof clientAddr;

    // networking: accept the incoming connection ──
    int newFd = accept(listenerFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    if (newFd == -1) {
        std::cerr << "accept() failed\n";
        return;
    }

    addConnection(pfds, newFd);  // register with poll()

    std::string ip = addressToString(clientAddr);
    std::cout << "pollserver: new connection from " << ip << " on socket " << newFd << "\n";

    /* ---DSA Integration--- */
    // register client in hash table ──
    clients.addClient(newFd, ip);
    std::cout << "[DSA] ClientManager now tracking " << clients.size() << " client(s)\n";

    // replay recent message history to the new joiner
    replayHistory(newFd, history);
}

// ============================================================
//  DSA HELPER: flushQueues
//  Iterates through every live client in the pollfd list and
//  drains their MessageQueue by sending each message over the
//  socket. Called after every broadcast.
// ============================================================
static void flushQueues(int listenerFd, const std::vector<pollfd>& pfds, ClientManager& clients){
    for (const pollfd& pfd : pfds){
        if (pfd.fd == listenerFd)
            continue;

        MessageQueue* q = clients.getQueue(pfd.fd);
        if (!q) continue;

        while (!q->isEmpty()) {
            std::string msg = q->dequeue();
            if (send(pfd.fd, msg.c_str(), static_cast<int>(msg.size()), 0) == -1)
                std::cerr << "send() error to socket " << pfd.fd << "\n";
        }
    }
}

// ============================================================
//  EVENT HANDLER: handleClientData
//  Called when poll() signals a regular client has data ready.
//
//  DSA additions:
//    • Stores broadcast message in MessageHistory (stack)
//    • Routes message through each recipient's MessageQueue
//      before flushing to their socket
// ============================================================
static void handleClientData(int listenerFd, std::vector<pollfd>& pfds, int& index, ClientManager& clients, MessageHistory& history){
    char buf[BUFFER_SIZE];
    int  senderFd = pfds[index].fd;

    // receive data
    int bytesReceived = recv(senderFd, buf, sizeof buf, 0);

    if (bytesReceived <= 0) {
        if (bytesReceived == 0)
            std::cout << "pollserver: socket " << senderFd << " hung up\n";
        else
            std::cerr << "recv() error on socket " << senderFd << "\n";

        close(senderFd);                     // close the socket
        removeConnection(pfds, index); // drop from poll list

        // remove from hash table
        clients.removeClient(senderFd);
        std::cout << "[DSA] ClientManager now tracking " << clients.size() << " client(s)\n";

        index--;  // step back after swap-remove
        return;
    }

    std::cout << "pollserver: received " << bytesReceived << " bytes from fd " << senderFd << "\n";

    // store this message in the shared history stack
    std::string message(buf, static_cast<size_t>(bytesReceived));
    history.push(message);
    std::cout << "[DSA] MessageHistory now holds " << history.size() << " message(s)\n";

    // enqueue message into each recipient's MessageQueue
    for (const pollfd& pfd : pfds) {
        int destFd = pfd.fd;

        if (destFd == listenerFd || destFd == senderFd)
            continue;   // skip listener and sender

        MessageQueue* q = clients.getQueue(destFd);
        if (q) {
            q->enqueue(message);
            std::cout << "[DSA] Enqueued to fd " << destFd << " (queue size: " << q->size() << ")\n";
        }
    }

    // flush all queues -> actually send over the network
    flushQueues(listenerFd, pfds, clients);
}

// ============================================================
//  MAIN LOOP HELPER: processConnections
//  Scans every fd after poll() returns and dispatches events.
// ============================================================
static void processConnections(int listenerFd, std::vector<pollfd>& pfds, ClientManager& clients, MessageHistory& history){
    for (int i = 0; i < static_cast<int>(pfds.size()); i++) {
        if (!(pfds[i].revents & (POLLIN | POLLHUP)))
            continue;

        if (pfds[i].fd == listenerFd)
            handleNewConnection(listenerFd, pfds, clients, history);
        else
            handleClientData(listenerFd, pfds, i, clients, history);
    }
}

// ============================================================
//  SERVER ENTRY POINT (called from main.cpp)
//  Owns the poll() event loop. DSA objects are created here
//  and passed down into every handler.
// ============================================================
void runServer(){
    // ── Zain's networking setup ──
    int listenerFd = createListenerSocket();
    if (listenerFd == -1) {
        std::cerr << "Failed to create listener socket. Exiting.\n";
        return;
    }

    std::vector<pollfd> pfds;
    pfds.reserve(MAX_CLIENTS);
    addConnection(pfds, listenerFd);

    
    ClientManager clients(16);   // hash table, starts with 16 buckets
    MessageHistory history(20);  // remembers last 20 broadcast messages

    std::cout << "pollserver: waiting for connections on port " << PORT << "...\n";
    std::cout << "[DSA] ClientManager and MessageHistory initialised\n";

    // main event loop
    while (true) {
        int eventCount = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1);   // block until activity

        if (eventCount == -1) {
            std::cerr << "poll() failed. Exiting.\n";
            return;
        }

        processConnections(listenerFd, pfds, clients, history);
    }
}