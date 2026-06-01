#pragma once
#include <string>

struct Client {
    int fd; // socket file descriptor (socket number)
    std::string ip; // IP address
    long connectedAt; // timestamp of when they joined
    std::string name; 
};