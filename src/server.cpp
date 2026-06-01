// ============================================================
//  server.cpp
//  Poll-based TCP Broadcast Server  — C++ Version
//  Original networking logic by Zain Ul Abad.
//  DSA integration added on top of networking layer by Muhammad Hasan.
//
//  Build (Linux):
//    g++ -std=c++17 -o server
//        src/Server.cpp src/ClientManager.cpp
//        src/MessageHistory.cpp src/MessageQueue.cpp
//        main.cpp -Iinclude
//
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

    switch (addr.ss_family){
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
//  HELPER: receiveName
//  After a client connects, we send them a prompt and do one
//  blocking recv() to read their chosen name.
//
//  We strip '\r' and '\n' from the end because terminal tools
//  like telnet/netcat append a newline when the user hits Enter.
//
//  If the client sends nothing or disconnects immediately,
//  we fall back to "Anonymous".
// ============================================================
static std::string receiveName(int fd){
    // send the prompt
    std::string prompt = "Enter your name: ";
    send(fd, prompt.c_str(), static_cast<int>(prompt.size()), 0);

    // blocking recv — wait for the client to type their name
    char buf[BUFFER_SIZE];
    int bytes = recv(fd, buf, sizeof buf - 1, 0);

    if (bytes <= 0)
        return "Anonymous";

    buf[bytes] = '\0';
    std::string name(buf);

    // strip trailing \r and \n (telnet sends \r\n, netcat sends \n)
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n'))
        name.pop_back();

    // if the name is blank after stripping, use fallback
    if (name.empty())
        return "Anonymous";

    return name;
}

// ============================================================
//  DSA HELPER: replayHistory
//  Sends all messages in MessageHistory to a newly connected
//  client so they can see what was said before they joined.
//
//  MessageHistory is a doubly linked list used as a stack:
//    - head = oldest message
//    - tail = newest message  (top of stack)
//
//  We pop everything into a temporary vector (newest first),
//  push it all back to restore the stack, then send oldest→newest.
//
//  Messages are already formatted as "Name: text" because
//  handleClientData formats them before calling history.push().
// ============================================================
static void replayHistory(int newFd, MessageHistory& history){ // no need to worry, the string of histriry contains name
    if (history.isEmpty())
        return;

    // drain into temp (pop gives tail = newest first)
    std::vector<std::string> msgs;
    msgs.reserve(static_cast<size_t>(history.size()));

    while (!history.isEmpty())
        msgs.push_back(history.pop());

    // restore the stack (msgs[0] is newest, push in reverse so oldest goes in first)
    for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; i--)
        history.push(msgs[i]);

    // send header
    std::string header = "--- chat history ---\n";
    send(newFd, header.c_str(), static_cast<int>(header.size()), 0);

    // send oldest → newest  (msgs is newest-first, so iterate in reverse)
    for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; i--)
        send(newFd, msgs[i].c_str(), static_cast<int>(msgs[i].size()), 0);

    std::string footer = "--- end of history ---\n";
    send(newFd, footer.c_str(), static_cast<int>(footer.size()), 0);
}

// ============================================================
//  EVENT HANDLER: handleNewConnection
//  Called when poll() signals the listener socket is readable.
//  accept() completes the TCP handshake.
//
//  Steps:
//    1. accept()  — networking, completes TCP handshake
//    2. receiveName() — asks the client to type their name
//    3. addClient() — stores fd + ip + name in the hash table
//    4. addConnection() — adds fd to poll() watch list
//    5. replayHistory() — sends recent messages to the joiner
//    6. broadcasts a join notification to all other clients
// ============================================================
static void handleNewConnection(int listenerFd, std::vector<pollfd>& pfds, ClientManager& clients, MessageHistory& history){
    sockaddr_storage clientAddr{};
    socklen_t addrLen = sizeof clientAddr;

    int newFd = accept(listenerFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    if (newFd == -1) {
        std::cerr << "accept() failed\n";
        return;
    }

    std::string ip = addressToString(clientAddr);

    // ask the client for their name (blocking, happens before poll watches this fd)
    std::string name = receiveName(newFd);

    // ---register in hash table with name---
    clients.addClient(newFd, ip, name);
    std::cout << "[+] New client: fd=" << newFd
              << "  name=\"" << name << "\""
              << "  ip=" << ip
              << "  total=" << clients.size() << "\n";

    // start watching this fd with poll()
    addConnection(pfds, newFd);

    // ---send recent chat history so they can catch up---
    replayHistory(newFd, history);

    // broadcast join notification to everyone already connected
    std::string joinMsg = "*** " + name + " joined the chat ***\n";
    for (const pollfd& pfd : pfds) {
        if (pfd.fd == listenerFd || pfd.fd == newFd)
            continue;
        MessageQueue* q = clients.getQueue(pfd.fd);
        if (q) q->enqueue(joinMsg);
    }

    // flush the join notification immediately
    for (const pollfd& pfd : pfds) {
        if (pfd.fd == listenerFd || pfd.fd == newFd)
            continue;
        MessageQueue* q = clients.getQueue(pfd.fd);
        if (!q) continue;
        while (!q->isEmpty()) {
            std::string msg = q->dequeue();
            send(pfd.fd, msg.c_str(), static_cast<int>(msg.size()), 0);
        }
    }
}

// ============================================================
//  DSA HELPER: flushQueues
//  Drains every connected client's MessageQueue and sends
//  each pending message over their socket.
//  Called after every broadcast so nothing stays pending.
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
//  Steps:
//    1. recv() reads raw bytes from the sender
//    2. Look up the sender's name from ClientManager (hash table)
//    3. Format the message as "Name: text\n"
//    4. Push formatted message to MessageHistory (stack)
//    5. Enqueue into every other client's MessageQueue (queue)
//    6. flushQueues() delivers all pending messages
// ============================================================
static void handleClientData(int listenerFd, std::vector<pollfd>& pfds, int& index, ClientManager& clients, MessageHistory& history){
    char buf[BUFFER_SIZE];
    int  senderFd = pfds[index].fd;

    int bytesReceived = recv(senderFd, buf, sizeof buf, 0);

    if (bytesReceived <= 0) {
        // client disconnected
        std::string leaverName = "Unknown";
        Client* c = clients.getClient(senderFd);
        if (c) leaverName = c->name;

        if (bytesReceived == 0)
            std::cout << "[-] \"" << leaverName << "\" (fd=" << senderFd << ") disconnected.\n";
        else
            std::cerr << "recv() error on socket " << senderFd << "\n";

        close(senderFd);
        removeConnection(pfds, index);

        // ---remove from hash table---
        clients.removeClient(senderFd);
        std::cout << "[DSA] ClientManager now tracking " << clients.size() << " client(s)\n";

        // broadcast leave notification
        std::string leaveMsg = "*** " + leaverName + " left the chat ***\n";
        for (const pollfd& pfd : pfds) {
            if (pfd.fd == listenerFd) continue;
            MessageQueue* q = clients.getQueue(pfd.fd);
            if (q) q->enqueue(leaveMsg);
        }
        flushQueues(listenerFd, pfds, clients);

        index--;
        return;
    }

    // normal message received

    // strip trailing \r\n so messages look clean
    std::string rawText(buf, static_cast<size_t>(bytesReceived));
    while (!rawText.empty() && (rawText.back() == '\r' || rawText.back() == '\n'))
        rawText.pop_back();

    if (rawText.empty())
        return; // ignoring the blank lines

    // ---look up sender's name from hash table---
    std::string senderName = "Unknown";
    Client* sender = clients.getClient(senderFd);
    if (sender) senderName = sender->name;

    // ***formating is done here: "Hasan: hello everyone\n"***
    std::string formatted = senderName + ": " + rawText + "\n";

    std::cout << "[→] " << formatted;

    // ---push to MessageHistory (stack) — stored with name attached---
    history.push(formatted);
    std::cout << "[DSA] MessageHistory now holds " << history.size() << " message(s)\n";

    // ---enqueue into every OTHER client's MessageQueue---
    for (const pollfd& pfd : pfds) {
        int destFd = pfd.fd;
        if (destFd == listenerFd || destFd == senderFd)
            continue;

        MessageQueue* q = clients.getQueue(destFd);
        if (q) {
            q->enqueue(formatted);
            std::cout << "[DSA] Enqueued to fd=" << destFd
                      << " (queue size: " << q->size() << ")\n";
        }
    }

    // ---flush all queues — deliver over the network---
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
    int listenerFd = createListenerSocket();
    if (listenerFd == -1) {
        std::cerr << "Failed to create listener socket. Exiting.\n";
        return;
    }

    std::vector<pollfd> pfds;
    pfds.reserve(MAX_CLIENTS);
    addConnection(pfds, listenerFd);

    ClientManager  clients(16);  // hash table, 16 initial buckets
    MessageHistory history(20);  // stack, remembers last 20 messages

    std::cout << "pollserver: waiting for connections on port " << PORT << "...\n";
    std::cout << "ClientManager and MessageHistory initialised\n";

    while (true) {
        int eventCount = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1);

        if (eventCount == -1) {
            std::cerr << "poll() failed. Exiting.\n";
            return;
        }

        processConnections(listenerFd, pfds, clients, history);
    }
}
