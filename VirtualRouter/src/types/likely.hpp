// Likely

#ifndef LIKELY_HPP
#define LIKELY_HPP

// Likely/Unlikely macros to help branch prediction
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif
