// Mock.hpp

#ifndef MOCK_HPP
#define MOCK_HPP

// Enforce consistent build across all translation units
#if defined(DEBUG) && defined(NDEBUG)
#error "DEBUG and NDEBUG both defined — invalid configuration"
#endif

#if defined(DEBUG) || defined(NDEBUG)
    #define ENABLE_MOCK_VIRTUAL
#endif

// Mock is used for creating virtual MOCK functions only during debug and testing phases.
#ifdef ENABLE_MOCK_VIRTUAL
    #define MOCK virtual
#else
    #define MOCK
#endif

#endif // MOCK_HPP
