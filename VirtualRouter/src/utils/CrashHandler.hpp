// CrashHandler.hpp

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <filesystem>

#define LOG_DIR "/var/VirtualRouter/log"

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

inline void setupCrashLogging()
{
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGILL, crash_handler);
}
