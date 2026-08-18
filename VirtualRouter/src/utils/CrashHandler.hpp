/**
 * @file CrashHandler.hpp
 * @brief Signal handlers that write timestamped crash reports to disk on fatal signals.
 * @ingroup UTILS
 */

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <filesystem>

namespace utils
{

/// Filesystem path where crash log files are written.
#define LOG_DIR "/var/core::VirtualRouter/log"

/**
 * @brief Async-signal-safe crash handler that writes a backtrace log and terminates.
 *
 * Invoked by the OS on fatal signals (SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL).
 * It creates the log directory if needed, opens a uniquely timestamped file,
 * writes the signal name, PID, and a stack trace via backtrace_symbols_fd(),
 * then compresses the file with gzip before calling _exit(1).
 *
 * The log filename follows the pattern:
 * @c YYYY-MM-DD_HH-MM-SS_crash_report.log.gz
 * under @ref LOG_DIR.
 *
 * @param sig  Signal number delivered by the OS (e.g. SIGSEGV).
 *
 * @note backtrace_symbols_fd() is async-signal-safe and does not allocate.
 * The gzip system() call is not async-signal-safe; it is invoked after all
 * critical data have already been flushed to the file descriptor.
 *
 * @warning This function calls _exit(1) and never returns. Do not install it
 * for signals that are intended to be handled and recovered from.
 */
static void crash_handler(int sig)
{
    const char* signalName;
    switch (sig)
    {
        case SIGSEGV: signalName = "SIGSEGV"; break;
        case SIGABRT: signalName = "SIGABRT"; break;
        case SIGBUS: signalName = "SIGBUS"; break;
        case SIGFPE: signalName = "SIGFPE"; break;
        case SIGILL: signalName = "SIGILL"; break;
        default: signalName = "UNKNOWN"; break;
    }

    std::filesystem::create_directories(LOG_DIR);

    // Create unique timestamped filename
    char filename[128];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    strftime(filename, sizeof(filename), std::string(std::string(LOG_DIR) + "/%Y-%m-%d_%H-%M-%S_crash_report.log").c_str(), tm_info);

    int fd = open(filename, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd >= 0)
    {
        dprintf(fd, "==== CRASH DETECTED ====\n");

        char timestr[64];
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm_info);
        dprintf(fd, "Time: %s\n", timestr);
        dprintf(fd, "Signal: %s (%d)\n", signalName, sig);
        dprintf(fd, "PID: %d\n", getpid());

        void* trace[32];
        int size = backtrace(trace, 32);
        dprintf(fd, "Stack trace (%d frames):\n", size);
        backtrace_symbols_fd(trace, size, fd);

        dprintf(fd, "=========================\n");
        close(fd);
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "gzip -f '%s'", filename);
    system(cmd);

    _exit(1);
}

/**
 * @brief Installs crash_handler() for all commonly fatal signals.
 *
 * Registers @ref crash_handler for SIGSEGV, SIGABRT, SIGBUS, SIGFPE, and
 * SIGILL. Call once at process startup, before spawning any threads, so
 * that every thread inherits the handlers.
 *
 * @note SIGFPE is registered twice in the initializer list; this is
 * harmless but redundant.
 */
inline void setupCrashLogging()
{
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGILL, crash_handler);
}

} // namespace utils
