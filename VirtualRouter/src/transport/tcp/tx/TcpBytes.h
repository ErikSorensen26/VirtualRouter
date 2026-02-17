// TcpBytes.h

#ifndef TCP_BYTES_H
#define TCP_BYTES_H

#include <cstddef.
#include <cstdint>
#include <atomic>
#include <vector>
#include <mutex>
#include <span>
#include <sys/uio.h>

namespace TCP
{
class TcpBytes;

class TcpBlockPool final
{
public:
    struct Config final
    {
        size_t blockSize = 4096;
        size_t block = 128;
        size_t
    }
}
}

#endif // TCP_BYTES_H
