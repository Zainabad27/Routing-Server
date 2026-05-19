#include "../include/MessageHistory.h"

MessageHistory::MessageHistory(int capacity) : head(nullptr), tail(nullptr), currentSize(0), capacity(capacity) {}

MessageHistory::~MessageHistory() {
    // traverse on every node and delete it
    while (head != nullptr) {
        Node* temp = head;
        head = head->next;
        delete temp;
    }
}

bool MessageHistory::isEmpty() const {
    return currentSize == 0;
}

bool MessageHistory::isFull() const {
    return currentSize == capacity;
}

/*
Adds a new message to the tail (top of stack).

If full: deletes head (oldest), then append new to tail
*/
void MessageHistory::push(const std::string& message) {

    if (isFull()) {
        // deletin the head
        Node* temp = head;
        head = head->next;

        if (head != nullptr)
            head->prev = nullptr;
        else
            tail = nullptr; // however this will never be reached

        delete temp;
        currentSize--;
    }

    // new node at tail
    Node* node = new Node(message);

    if (tail == nullptr) {
        head = tail = node;
    } else {
        node->prev = tail;
        tail->next = node;
        tail = node;
    }

    currentSize++;
}

/*
Removes and returns the top (tail)
Rewires tail to its previous
*/
std::string MessageHistory::pop() {

    if (isEmpty()) return "";

    Node* removed = tail;
    std::string removedMessage = removed->data;

    tail = tail->prev;

    if (tail != nullptr)
        tail->next = nullptr;
    else
        head = nullptr; // list is now empty

    delete removed;
    currentSize--;

    return removedMessage;
}

std::string MessageHistory::peek() const {
    if (isEmpty()) return "";
    return tail->data;
}

int MessageHistory::size() const {
    return currentSize;
}