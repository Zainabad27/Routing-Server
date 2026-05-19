// ============================================================
//  Poll-based TCP Broadcast Server  —  Proper C++ Version
//  Original networking logic by Zain (unchanged).
//  Converted to idiomatic C++ for readability.
//
//  Build (Linux):  g++ -std=c++17 -o server Server.cpp
//  Run:            ./server
//  Port:           9034
// ============================================================

// ── System headers (Linux / POSIX) ──────────────────────────
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

// ── Standard C++ headers ────────────────────────────────────
#include <iostream>      // std::cout, std::cerr
#include <vector>        // std::vector
#include <string>        // std::string
#include <cstring>       // memset
#include <cstdlib>       // exit()

// ── Constants ───────────────────────────────────────────────
static constexpr const char* PORT          = "9034";
static constexpr int         BUFFER_SIZE   = 256;
static constexpr int         MAX_CLIENTS   = 500;   // reserve hint for vector
static constexpr int         BACKLOG       = 10;    // max pending connections

// ============================================================
//  HELPER: inet_ntop2
//  Converts a sockaddr_storage (which can hold IPv4 or IPv6)
//  into a human-readable IP string like "192.168.1.5".
//  Works for both address families transparently.
// ============================================================
static std::string addressToString(sockaddr_storage& addr)
{
    char buf[INET6_ADDRSTRLEN];

    switch (addr.ss_family)
    {
        case AF_INET:
        {
            // IPv4 — cast to sockaddr_in and grab sin_addr
            auto* sa4 = reinterpret_cast<sockaddr_in*>(&addr);
            inet_ntop(AF_INET, &sa4->sin_addr, buf, sizeof buf);
            break;
        }
        case AF_INET6:
        {
            // IPv6 — cast to sockaddr_in6 and grab sin6_addr
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
//  Sets up the server's main listening socket.
//  Steps: getaddrinfo → socket → setsockopt → bind → listen
//  Returns the file descriptor, or -1 on failure.
// ============================================================
static int createListenerSocket()
{
    addrinfo hints{};                  // zero-initialize with {} — C++ style
    hints.ai_family   = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM;   // TCP (reliable, ordered)
    hints.ai_flags    = AI_PASSIVE;    // bind to local machine's IP

    addrinfo* results = nullptr;
    int rv = getaddrinfo(nullptr, PORT, &hints, &results);
    if (rv != 0)
    {
        std::cerr << "getaddrinfo error: " << gai_strerror(rv) << "\n";
        exit(1);
    }

    int listenerFd = -1;
    int yes = 1;

    for (addrinfo* p = results; p != nullptr; p = p->ai_next)
    {
        listenerFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenerFd < 0)
            continue;   // try next address

        // Allow reuse of port immediately after server restart
        setsockopt(listenerFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

        if (bind(listenerFd, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listenerFd);
            continue;   // try next address
        }

        break;  // success — socket is bound
    }

    freeaddrinfo(results);  // done with the linked list

    if (listenerFd == -1)
        return -1;

    if (listen(listenerFd, BACKLOG) == -1)
        return -1;

    return listenerFd;
}

// ============================================================
//  CONNECTION MANAGEMENT: addConnection / removeConnection
//
//  addConnection   — registers a new fd with poll()
//  removeConnection — removes a fd by swapping with the last
//                     element (O(1) removal from a vector).
// ============================================================
static void addConnection(std::vector<pollfd>& pfds, int fd)
{
    pollfd entry{};
    entry.fd     = fd;
    entry.events = POLLIN;   // notify us when data arrives
    pfds.push_back(entry);
}

static void removeConnection(std::vector<pollfd>& pfds, int index)
{
    // Swap with the last element, then pop — avoids shifting the whole array
    pfds[index] = pfds.back();
    pfds.pop_back();
}

// ============================================================
//  EVENT HANDLER: handleNewConnection
//  Called when poll() signals the listener socket is readable,
//  meaning a new client is knocking. accept() completes the
//  TCP handshake and gives us a dedicated fd for that client.
// ============================================================
static void handleNewConnection(int listenerFd, std::vector<pollfd>& pfds)
{
    sockaddr_storage clientAddr{};
    socklen_t addrLen = sizeof clientAddr;

    int newFd = accept(listenerFd,
                       reinterpret_cast<sockaddr*>(&clientAddr),
                       &addrLen);

    if (newFd == -1)
    {
        std::cerr << "accept() failed\n";
        return;
    }

    addConnection(pfds, newFd);

    std::cout << "pollserver: new connection from "
              << addressToString(clientAddr)
              << " on socket " << newFd << "\n";
}

// ============================================================
//  EVENT HANDLER: handleClientData
//  Called when poll() signals a regular client socket has data.
//  recv() reads the data, then we broadcast it to every other
//  connected client (excluding the sender and listener).
//
//  If recv() returns 0  → client disconnected cleanly (TCP FIN)
//  If recv() returns -1 → socket error
// ============================================================
static void handleClientData(int listenerFd,
                              std::vector<pollfd>& pfds,
                              int& index)          // index passed by reference
{
    char buf[BUFFER_SIZE];
    int  senderFd = pfds[index].fd;

    int bytesReceived = recv(senderFd, buf, sizeof buf, 0);

    if (bytesReceived <= 0)
    {
        // Client disconnected or error
        if (bytesReceived == 0)
            std::cout << "pollserver: socket " << senderFd << " hung up\n";
        else
            std::cerr << "recv() error on socket " << senderFd << "\n";

        close(senderFd);
        removeConnection(pfds, index);

        // Step back — the slot we deleted now holds a different fd
        // (the one that was at the back). We need to re-examine it.
        index--;
        return;
    }

    // ── Broadcast to everyone else ───────────────────────────
    std::cout << "pollserver: received " << bytesReceived
              << " bytes from fd " << senderFd << "\n";

    for (const pollfd& pfd : pfds)
    {
        int destFd = pfd.fd;

        // Skip the listener socket and the sender themselves
        if (destFd == listenerFd || destFd == senderFd)
            continue;

        if (send(destFd, buf, bytesReceived, 0) == -1)
            std::cerr << "send() error to socket " << destFd << "\n";
    }
}

// ============================================================
//  MAIN LOOP HELPER: processConnections
//  After poll() returns, we scan through every fd in our list.
//  poll() sets the revents field to tell us what happened:
//    POLLIN  → data is ready to read
//    POLLHUP → the connection was closed on the other end
// ============================================================
static void processConnections(int listenerFd, std::vector<pollfd>& pfds)
{
    for (int i = 0; i < static_cast<int>(pfds.size()); i++)
    {
        if (!(pfds[i].revents & (POLLIN | POLLHUP)))
            continue;   // nothing happened on this fd

        if (pfds[i].fd == listenerFd)
            handleNewConnection(listenerFd, pfds);  // new client knocking
        else
            handleClientData(listenerFd, pfds, i);  // existing client has data
    }
}

// ============================================================
//  ENTRY POINT: main
// ============================================================
int main()
{
    int listenerFd = createListenerSocket();
    if (listenerFd == -1)
    {
        std::cerr << "Failed to create listener socket. Exiting.\n";
        return 1;
    }

    // The poll-fd list — first entry is always the listener itself
    std::vector<pollfd> pfds;
    pfds.reserve(MAX_CLIENTS);   // pre-allocate to avoid reallocs
    addConnection(pfds, listenerFd);

    std::cout << "pollserver: waiting for connections on port " << PORT << "...\n";

    // ── Main event loop ──────────────────────────────────────
    // poll() blocks here until at least one fd has activity.
    // Timeout = -1 means wait forever (no timeout).
    while (true)
    {
        int eventCount = poll(pfds.data(), pfds.size(), -1 /* no timeout */);

        if (eventCount == -1)
        {
            std::cerr << "poll() failed. Exiting.\n";
            return 1;
        }

        processConnections(listenerFd, pfds);
    }
}