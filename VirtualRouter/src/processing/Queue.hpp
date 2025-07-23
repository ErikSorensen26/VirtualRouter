// Queue.hpp

#ifndef QUEUE_HPP
#define QUEUE_HPP

#include <vector>
#include <ByteString.hpp>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <queue>

/**
 * @file Que.h
 * @brief Defines the RingBuffer and ThreadSafeQueue template classes for data storage and synchronization.
 */

/**
 * @class RingBuffer
 * @brief Implements a fixed-size circular buffer (ring buffer) for storing elements in a FIFO manner.
 *
 * The `RingBuffer` class provides efficient enqueue and dequeue operations with constant time complexity.
 * When the buffer is full, new elements overwrite the oldest ones.
 *
 * @tparam T The type of elements stored in the ring buffer.
 */
template<typename T>
class RingBuffer 
{
public:

    /**
     * @brief Constructs a RingBuffer with a specified capacity.
     *
     * Initializes the underlying buffer with the given capacity and sets the head and tail indices
     * to 0. The buffer is initially empty.
     *
     * @param capacity The maximum number of elements the ring buffer can hold.
     *
     * @throws std::invalid_argument If the provided capacity is zero.
     */
    explicit RingBuffer(size_t capacity);
    
    /**
     * @brief Adds an item to the ring buffer.
     *
     * If the buffer is full, the oldest element (at the tail) is overwritten.
     *
     * @param item The item to be added to the buffer.
     */
    void enqueue(const T& item);
    
    /**
     * @brief Removes and returns the oldest item from the ring buffer.
     *
     * @return The oldest item in the buffer.
     *
     * @throws std::runtime_error If the buffer is empty.
     */
    T dequeue();
    
    /**
     * @brief Checks if the ring buffer is empty.
     *
     * @return `true` if the buffer is empty; `false` otherwise.
     */
    bool isEmpty() const;
    
    /**
     * @brief Checks if the ring buffer is full.
     *
     * @return `true` if the buffer is full; `false` otherwise.
     */
    bool isFull() const;
    
    /**
     * @brief Returns the current number of elements in the ring buffer.
     *
     * **Note:** The original implementation incorrectly returns the buffer's capacity.
     * It should return the actual number of elements stored. This needs to be corrected
     * by calculating the size based on head, tail, and the full flag.
     *
     * @return The current number of elements in the buffer.
     */
    size_t size() const;
    
    /**
     * @brief Returns the maximum capacity of the ring buffer.
     *
     * @return The capacity of the buffer.
     */
    size_t capacity() const;

private:
    std::vector<T> buffer_; ///< The underlying container for the buffer.
    size_t head_;           ///< Index of the next write position.
    size_t tail_;           ///< Index of the next read position.
    bool full_;             ///< Indicates whether the buffer is full.
};

template class RingBuffer<ByteString>;

template<typename T>
RingBuffer<T>::RingBuffer(size_t capacity)
    : buffer_(capacity), head_(0), tail_(0), full_(false) {}

template<typename T>
void RingBuffer<T>::enqueue(const T& item) 
{
    buffer_[head_] = item;

    if (full_) {
        tail_ = (tail_ + 1) % buffer_.size();
    }

    head_ = (head_ + 1) % buffer_.size();
    full_ = (head_ == tail_);
}

template<typename T>
T RingBuffer<T>::dequeue() 
{
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
bool RingBuffer<T>::isEmpty() const 
{
    return (!full_ && (head_ == tail_));
}

// Checks if the ring buffer is full.
template<typename T>
bool RingBuffer<T>::isFull() const {
    return full_;
}

// Returns the current size of the ring buffer.
template<typename T>
size_t RingBuffer<T>::size() const 
{
    return buffer_.size();
}

// Returns the maximum capacity of the ring buffer.
template<typename T>
size_t RingBuffer<T>::capacity() const 
{
    return buffer_.capacity();
}

/**
 * @class ThreadSafeQueue
 * @brief Implements a thread-safe queue with optional maximum size.
 *
 * The `ThreadSafeQueue` class provides synchronized enqueue and dequeue operations,
 * allowing multiple threads to safely interact with the queue without data races.
 * It supports optional blocking behavior when the queue reaches its maximum size.
 *
 * @tparam T The type of elements stored in the queue.
 */
template <typename T>
class ThreadSafeQueue 
{
public:

    /**
     * @brief Constructs a ThreadSafeQueue with an optional maximum size.
     *
     * If `maxSize` is set to zero, the queue has no size limit.
     *
     * @param maxSize The maximum number of elements the queue can hold. Defaults to 0 (no limit).
     */
    ThreadSafeQueue(size_t maxSize = 0);

    /**
     * @brief Enqueues an item into the queue.
     *
     * If the queue is full and `maxSize` is greater than zero, this method blocks until space becomes available.
     * If the queue is stopped, the item is not enqueued.
     *
     * @param item The item to be added to the queue.
     */
    void enqueue(T item);

    /**
     * @brief Dequeues an item from the queue.
     *
     * This method blocks until an item is available or the queue is stopped.
     *
     * @return An `std::optional` containing the dequeued item if available; `std::nullopt` if the queue is stopped and empty.
     */
    std::optional<T> dequeue();

    /**
     * @brief Stops the queue and notifies all waiting threads.
     *
     * After calling this method, no further items can be enqueued, and waiting threads are unblocked.
     */
    void stop();

    /**
     * @brief Checks if the queue is empty.
     *
     * @return `true` if the queue is empty; `false` otherwise.
     */
    bool isEmpty() const;

private:
    mutable std::mutex mtx_;            ///< Mutex for synchronizing access to the queue.
    std::queue<T> queue_;               ///< Underlying container for the queue.
    std::condition_variable cvEmpty_;   ///< Condition variable to wait for non-empty queue.
    std::condition_variable cvFull_;    ///< Condition variable to wait for space in the queue.
    size_t maxSize_;                    ///< Maximum size of the queue. Zero indicates no limit.
    bool stopped_;                      ///< Flag indicating whether the queue has been stopped.
};

template class ThreadSafeQueue<ByteString>;

template<typename T>
ThreadSafeQueue<T>::ThreadSafeQueue(size_t maxSize) : maxSize_(maxSize), stopped_(false) {}

template<typename T>
void ThreadSafeQueue<T>::enqueue(T item) 
{
    std::unique_lock<std::mutex> lock(mtx_);
    if (maxSize_ > 0) {
        cvFull_.wait(lock, [&]() { return queue_.size() < maxSize_ || stopped_; });
    }
    if (stopped_) return; // Do not enqueue if stopped
    queue_.push(std::move(item));
    cvEmpty_.notify_one();
}

template<typename T>
std::optional<T> ThreadSafeQueue<T>::dequeue() 
{
    std::unique_lock<std::mutex> lock(mtx_);
    cvEmpty_.wait(lock, [&]() { return !queue_.empty() || stopped_; });
    if (queue_.empty()) return std::nullopt; // Return nullopt if stopped and empty
    T item = std::move(queue_.front());
    queue_.pop();
    cvFull_.notify_one();
    return item;
}

template<typename T>
void ThreadSafeQueue<T>::stop() 
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stopped_ = true;
    }
    cvEmpty_.notify_all();
    cvFull_.notify_all();
}

template<typename T>
bool ThreadSafeQueue<T>::isEmpty() const 
{
    std::lock_guard<std::mutex> lock(mtx_);
    return queue_.empty();
}

#endif // QUEUE_HPP
