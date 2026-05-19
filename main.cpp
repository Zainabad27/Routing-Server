// ============================================================
//  main.cpp  —  Single entry point for the DSA Chat Server
//
//  Responsibility split:
//    server.cpp  → networking (poll-based TCP, Zain's work)
//    main.cpp    → program entry, startup banner, runServer()
//    ClientManager  → hash table for tracking connected clients
//    MessageHistory → stack for replaying recent messages
//    MessageQueue   → per-client FIFO for outgoing messages
//
//  Build (Linux):
//    g++ -std=c++17 -o server
//        src/main.cpp src/server.cpp
//        src/ClientManager.cpp
//        src/MessageHistory.cpp
//        src/MessageQueue.cpp
//        -Iinclude
//
//  Run:  ./server
// ============================================================

#include <iostream>
#include <string>

#include "include/Server.h"


int main(){
    std::cout << "========================================\n";
    std::cout << "  DSA Chat Server\n";
    std::cout << "  Networking : poll-based TCP (Zain)\n";
    std::cout << "  DSA Layer (Hasan):\n";
    std::cout << "    - ClientManager  (Hash Table)\n";
    std::cout << "    - MessageHistory (Stack)\n";
    std::cout << "    - MessageQueue   (Linked-list FIFO)\n";
    std::cout << "========================================\n\n";

    runServer();   // hands control to the networking

    
}