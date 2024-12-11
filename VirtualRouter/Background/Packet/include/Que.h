#pragma once

#include <vector>
#include <stdexcept>
#include <ByteString.hpp>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <queue>

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

template class RingBuffer<ByteString>;

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


template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue(size_t maxSize = 0) : maxSize_(maxSize), stopped_(false) {}

    // Enqueue an item; blocks if the queue is full (when maxSize > 0)
    void enqueue(T item) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (maxSize_ > 0) {
            cvFull_.wait(lock, [&]() { return queue_.size() < maxSize_ || stopped_; });
        }
        if (stopped_) return; // Do not enqueue if stopped
        queue_.push(std::move(item));
        cvEmpty_.notify_one();
    }

    // Dequeue an item; blocks until an item is available or the queue is stopped
    std::optional<T> dequeue() {
        std::unique_lock<std::mutex> lock(mtx_);
        cvEmpty_.wait(lock, [&]() { return !queue_.empty() || stopped_; });
        if (queue_.empty()) return std::nullopt; // Return nullopt if stopped and empty
        T item = std::move(queue_.front());
        queue_.pop();
        cvFull_.notify_one();
        return item;
    }

    // Stop the queue and notify all waiting threads
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stopped_ = true;
        }
        cvEmpty_.notify_all();
        cvFull_.notify_all();
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

private:
    mutable std::mutex mtx_;
    std::queue<T> queue_;
    std::condition_variable cvEmpty_;
    std::condition_variable cvFull_;
    size_t maxSize_;
    bool stopped_;
};
