#pragma once
#include <string>

// A simple FIFO queue for outgoing messages per client.
// Built from scratch using a singly linked list internally.

class MessageQueue {
public:
    MessageQueue();
    ~MessageQueue();
    
    void enqueue(const std::string& message);
    std::string dequeue();
    bool isEmpty() const;
    int size() const;
    
    private:
    struct Node {
        std::string data;
        Node* next;
        
        Node(const std::string& msg) : data(msg), next(nullptr) {}
    };

    Node* front;
    Node* back;
    int   count;
};