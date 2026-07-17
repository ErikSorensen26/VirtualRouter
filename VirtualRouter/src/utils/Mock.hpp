/**
 * @file Mock.hpp
 * @brief Mock/test utilities for unit testing.
 */

#ifndef MOCK_HPP
#define MOCK_HPP

// Enforce consistent build across all translation units
#if defined(DEBUG) && defined(NDEBUG)
#error "DEBUG and NDEBUG both defined — invalid configuration"
#endif

#if defined(DEBUG) || defined(NDEBUG)
    #define ENABLE_MOCK
#endif

// Mock is used for creating virtual MOCK functions only during debug and testing phases.
#ifdef ENABLE_MOCK
    #define MOCK virtual
#else
    #define MOCK
#endif

// MOCK HELPER INJECTORS

#ifdef ENABLE_MOCK
    #define INJECT_MOCK(MOCK_IMPL) MOCK_IMPL
#else
    #define INJECT_MOCK(MOCK_IMPL)
#endif

// TCP

#define MOCK_TCP_ENGINE_SWAP_H \
    public: \
    void swapEngineForTesting(TcpEngine* newEngine);

#define MOCK_TCP_ENGINE_SWAP_CPP \
    void Tcp::swapEngineForTesting(TcpEngine* newEngine) \
    { \
        delete engine; \
        engine = newEngine; \
    }

// PROCESS QUEUE

#define MOCK_PROCESS_QUEUE_MUTEX \
    public: std::recursive_mutex mu;

#define MOCK_PROCESS_QUEUE_MUTEX_GETTER \
    public: std::recursive_mutex& getLock() { return id->sub.mu; }

#define MOCK_PROCESS_QUEUE_LOCK \
    std::lock_guard<std::recursive_mutex> lock(mu);

#endif // MOCK_HPP
