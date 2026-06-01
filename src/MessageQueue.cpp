#include "../include/MessageQueue.h"

// constructor
MessageQueue::MessageQueue() {
    front = nullptr;
    back  = nullptr;
    count = 0;
}

// destructor
MessageQueue::~MessageQueue() {
    while (!isEmpty()) {
        dequeue();
    }
}

bool MessageQueue::isEmpty() const {
    return (front == nullptr);
}

void MessageQueue::enqueue(const std::string& message) {

    Node* newNode = new Node(message);

    if (isEmpty()) {
        front = newNode;
        back  = newNode;
    }
    else {
        back->next = newNode;
        back = newNode;
    }
    count++;
}

std::string MessageQueue::dequeue() {

    if (isEmpty()) return "";    

    // Store front data
    std::string removedMessage = front->data;

    // Temporary pointer
    Node* temp = front;

    // Move front ahead
    front = front->next;

    // If queue became empty
    if (front == nullptr) {
        back = nullptr;
    }

    delete temp;
    count--;
    return removedMessage;
}

int MessageQueue::size() const {
    return count;
}