#pragma once

#include <vector>
#include <stdexcept>

template<typename T>
class RingBuffer {
public:
    // Constructor: Initializes the ring buffer with a specified capacity.
    explicit RingBuffer(size_t capacity);
    
    // Adds an item to the ring buffer.
    void enqueue(const T& item);
    
    // Removes and returns an item from the ring buffer.
    T dequeue();
    
    // Checks if the ring buffer is empty.
    bool isEmpty() const;
    
    // Checks if the ring buffer is full.
    bool isFull() const;
    
    // Returns the current size of the ring buffer.
    size_t size() const;
    
    // Returns the maximum capacity of the ring buffer.
    size_t capacity() const;

private:
    std::vector<T> buffer_; // The underlying container for the buffer.
    size_t head_;           // Index of the next write position.
    size_t tail_;           // Index of the next read position.
    bool full_;             // Indicates whether the buffer is full.
};

template class RingBuffer<std::string>;

// Constructor: Initializes the ring buffer with a specified capacity.
template<typename T>
RingBuffer<T>::RingBuffer(size_t capacity)
    : buffer_(capacity), head_(0), tail_(0), full_(false) {}

// Adds an item to the ring buffer.
template<typename T>
void RingBuffer<T>::enqueue(const T& item) {
    buffer_[head_] = item;

    if (full_) {
        tail_ = (tail_ + 1) % buffer_.size();
    }

    head_ = (head_ + 1) % buffer_.size();
    full_ = (head_ == tail_);
}

// Removes and returns an item from the ring buffer.
template<typename T>
T RingBuffer<T>::dequeue() {
    if (isEmpty()) {
        throw std::runtime_error("Queue is empty");
    }

    auto item = buffer_[tail_];
    full_ = false;
    tail_ = (tail_ + 1) % buffer_.size();
    return item;
}

// Checks if the ring buffer is empty.
template<typename T>
bool RingBuffer<T>::isEmpty() const {
    return (!full_ && (head_ == tail_));
}

// Checks if the ring buffer is full.
template<typename T>
bool RingBuffer<T>::isFull() const {
    return full_;
}

// Returns the current size of the ring buffer.
template<typename T>
size_t RingBuffer<T>::size() const {
    return buffer_.size();
}

// Returns the maximum capacity of the ring buffer.
template<typename T>
size_t RingBuffer<T>::capacity() const {
    return buffer_.capacity();
}
