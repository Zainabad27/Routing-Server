#pragma once

#include <vector>
#include <string>
#include "Client.h"
#include "MessageQueue.h"

class ClientManager {
public:

    ClientManager(int initialCapacity = 10);
    ~ClientManager();

    void addClient(int fd, const std::string& ip, const std::string& name);
    void removeClient(int fd);
    bool exists(int fd) const;
    int size() const;
    Client* getClient(int fd);
    MessageQueue* getQueue(int fd);

    // hash table entry (ideally this should be made in include)
    struct Entry {
        Client client;
        MessageQueue queue;

        bool occupied;
        bool deleted; // will be helpful, as we are doing linear probing

        Entry() : occupied(false), deleted(false) {}
    };

    std::vector<Entry> table;

private:

    int currentSize;
    int capacity;

    int hashFunction(int fd) const;
    int findIndex(int fd) const;
    void rehash();
};