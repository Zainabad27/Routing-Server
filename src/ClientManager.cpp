#include "../include/ClientManager.h"
#include <ctime>

ClientManager::ClientManager(int initialCapacity){
    capacity = initialCapacity;
    currentSize = 0;
    table.resize(capacity);
}

ClientManager::~ClientManager() { }

/*---Hashing Functions---*/
int ClientManager::hashFunction(int fd) const {
    return fd % capacity;
}

// linear probing
int ClientManager::findIndex(int fd) const {

    int index = hashFunction(fd);
    int start = index;

    while (table[index].occupied){
        if (!table[index].deleted && table[index].client.fd == fd) {
            return index;
        }
        index = (index + 1) % capacity;

        // we didn't found any index
        if (index == start) break;
    }
    return -1;
}


void ClientManager::rehash() {
    std::vector<Entry> oldTable = table; // preserving the whole table

    capacity *= 2;

    table.clear();
    table.resize(capacity);

    currentSize = 0;

    for (Entry& entry : oldTable) {

        if (entry.occupied && !entry.deleted) {
            addClient(entry.client.fd, entry.client.ip, entry.client.name);
        }
    }
}

/*---Actual Functions---*/
void ClientManager::addClient(int fd, const std::string& ip, const std::string& name){

    if ((float)currentSize / capacity >= 0.7f)
        rehash();

    int index = hashFunction(fd);

    while (table[index].occupied && !table[index].deleted)
        index = (index + 1) % capacity;

    table[index].client.fd          = fd;
    table[index].client.ip          = ip;
    table[index].client.name        = name;          // store the name
    table[index].client.connectedAt = time(nullptr);

    table[index].occupied = true;
    table[index].deleted  = false;

    currentSize++;
}

void ClientManager::removeClient(int fd){
    int index = findIndex(fd);

    if (index == -1) return;

    table[index].deleted = true;
    currentSize--;
}

bool ClientManager::exists(int fd) const{
    return (findIndex(fd) != -1);
}

int ClientManager::size() const {
    return currentSize;
}

Client* ClientManager::getClient(int fd){
    int index = findIndex(fd);
    if (index == -1) return nullptr;
    return &table[index].client;
}

MessageQueue* ClientManager::getQueue(int fd) {
    int index = findIndex(fd);
    if (index == -1) return nullptr;
    return &table[index].queue;
}