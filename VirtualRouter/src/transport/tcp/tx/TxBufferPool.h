// TxBufferPool.h

#ifndef TCP_TX_BUFFER_POOL_H
#define TCP_TX_BUFFER_POOL_H

#include <atomic>
#include <vector>

namespace TCP
{
class TxBuffer;
struct PoolConfig;

class TxBufferPool final
{
public:
    explicit TxBufferPool(PoolConfig& c) noexcept;
    ~TxBufferPool();

    TxBufferPool(const TxBufferPool&) = delete;
    TxBufferPool& operator=(const TxBufferPool&) = delete;

    TxBuffer acquire() noexcept;

    size_t blockSize() const noexcept;

private:
    struct Block final
    {
        std::atomic<uint32_t> refs;
        Block* next = nullptr;
        uint32_t rd = 0;
        uint32_t wr = 0;
        uint8_t* data() noexcept { return reinterpret_cast<uint8_t*>(this + 1); }
        const uint8_t* data() const noexcept { return reinterpret_cast<const uint8_t*>(this + 1); }
    };

    friend class TxBuffer;

    Block* pop() noexcept;
    void addRef(Block* b) noexcept;
    void release(Block* b) noexcept;
    void grow(size_t blocks) noexcept;

    static Block* atomicPop(std::atomic<Block*>& head) noexcept;
    static void atomicPush(std::atomic<Block*>& head, Block* b) noexcept;

    PoolConfig& cfg;
    std::atomic<Block*> freeList{nullptr};
    std::atomic<size_t> totalBlocks{0};

    // slab ownership (freed in destructor)
    std::mutex growMtx;
    std::vector<void*> slabs;
};
}

#endif // TCP_TX_BUFFER_POOL_H
