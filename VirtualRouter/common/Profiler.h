// Profiler.h
#ifndef PROFILER_H
#define PROFILER_H

#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

class Profiler
{
public:
    // singleton accessor
    static Profiler& getInstance() {
        static Profiler instance;
        return instance;
    }

    // append a message with timestamp
    void notify(std::string_view msg) noexcept {
        ThreadBuffer* tb = getOrRegisterThreadBuffer_();

        if (!tb->started) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &tb->t0);
            tb->started = true;
        }

        timespec now;
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);
        uint64_t ns = diff_ns_(tb->t0, now);

        char prefix[48];
        int plen = std::snprintf(prefix, sizeof(prefix),
                                 "[%lu ns] ",
                                 (unsigned long)ns);
        if (plen < 0) plen = 0;
        if (plen > (int)sizeof(prefix)) plen = (int)sizeof(prefix);

        const size_t old = tb->buf.size();
        tb->buf.resize(old + (size_t)plen + msg.size() + 1);
        std::memcpy(tb->buf.data() + old, prefix, (size_t)plen);
        if (!msg.empty())
            std::memcpy(tb->buf.data() + old + (size_t)plen,
                        msg.data(), msg.size());
        tb->buf[old + (size_t)plen + msg.size()] = '\n';
    }

    // print header + all buffers
    void print() noexcept {
        writeHeader_(STDOUT_FILENO);
        ThreadBuffer* h = head_.load(std::memory_order_acquire);
        for (ThreadBuffer* p = h; p; p = p->next)
            flushBuffer_(STDOUT_FILENO, *p);
    }

    // write header + all buffers to file
    bool write(const std::string& path) noexcept {
        int fd = ::open(path.c_str(),
                        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                        0644);
        if (fd < 0) return false;

        bool ok = writeHeader_(fd);
        ThreadBuffer* h = head_.load(std::memory_order_acquire);
        for (ThreadBuffer* p = h; p; p = p->next)
            ok &= flushBuffer_(fd, *p);

        ::close(fd);
        return ok;
    }

private:
    Profiler() = default;
    ~Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    struct ThreadBuffer {
        std::vector<char> buf;
        ThreadBuffer* next{nullptr};
        timespec t0{};
        bool started{false};

        ThreadBuffer() { buf.reserve(64 * 1024); }
    };

    std::atomic<ThreadBuffer*> head_{nullptr};
    static thread_local ThreadBuffer* tlsBuf_;

    static inline uint64_t diff_ns_(const timespec& a, const timespec& b) {
        int64_t sec = (int64_t)b.tv_sec - (int64_t)a.tv_sec;
        int64_t nsec = (int64_t)b.tv_nsec - (int64_t)a.tv_nsec;
        return (uint64_t)sec * 1000000000ull + (uint64_t)nsec;
    }

    ThreadBuffer* getOrRegisterThreadBuffer_() {
        ThreadBuffer* tb = tlsBuf_;
        if (tb) return tb;

        tb = new ThreadBuffer();
        ThreadBuffer* old = head_.load(std::memory_order_relaxed);
        do { tb->next = old; }
        while (!head_.compare_exchange_weak(
            old, tb,
            std::memory_order_release,
            std::memory_order_relaxed));

        tlsBuf_ = tb;
        return tb;
    }

    static bool writeAll_(int fd, const char* p, size_t n) {
        size_t left = n;
        while (left) {
            ssize_t r = ::write(fd, p, left);
            if (r > 0) { p += r; left -= (size_t)r; continue; }
            if (r < 0 && errno == EINTR) continue;
            return false;
        }
        return true;
    }

    static bool flushBuffer_(int fd, const ThreadBuffer& tb) {
        if (tb.buf.empty()) return true;
        return writeAll_(fd, tb.buf.data(), tb.buf.size());
    }

    static bool writeHeader_(int fd) {
        static constexpr char h1[] = "------------------------------\n";
        static constexpr char h2[] = "------ Profiling Report ------\n";
        return writeAll_(fd, h1, sizeof(h1)-1)
            && writeAll_(fd, h2, sizeof(h2)-1);
    }
};

#endif // PROFILER_H
