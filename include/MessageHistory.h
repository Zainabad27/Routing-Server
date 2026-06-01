#pragma once
#include <string>

class MessageHistory {
public:
    explicit MessageHistory(int capacity = 20);
    ~MessageHistory();

    void push(const std::string& message);
    std::string pop();
    std::string peek() const;
    bool isEmpty() const;
    bool isFull() const;
    int size() const;

private:
    struct Node {
        std::string data;
        Node* next;
        Node* prev;
        explicit Node(const std::string& msg) : data(msg), next(nullptr), prev(nullptr) { }
    };

    Node* head;
    Node* tail;
    int currentSize;
    int capacity;
};