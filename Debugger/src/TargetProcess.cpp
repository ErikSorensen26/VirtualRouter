// TargetProcess.cpp

#include "TargetProcess.h"
#include <sys/ptrace.h>
#include <errno.h>
#include <cstring>
#include <iostream>

TargetProcess::TargetProcess(pid_t pid)
    : pid(pid) {}

pid_t TargetProcess::getPid() const
{
    return pid;
}

uint64_t TargetProcess::readWord(uint64_t addr) const
{
    errno = 0;
    uint64_t data = ptrace(PTRACE_PEEKDATA, pid, addr, nullptr);
    if (errno != 0)
        perror("ptrace(PTRACE_PEEKDATA)");
    return data;
}

bool TargetProcess::readMemory(uint64_t addr, uint8_t* buffer, size_t size) const
{
    size_t bytesRead = 0;
    while (bytesRead < size)
    {
        uint64_t word = readWord(addr + bytesRead);
        size_t copySize = std::min(sizeof(uint64_t), size - bytesRead);
        std::memcpy(buffer + bytesRead, &word, copySize);
        bytesRead += copySize;
    }
    return true;
}

bool TargetProcess::writeWord(uint64_t addr, uint64_t data) const
{
    if (ptrace(PTRACE_POKEDATA, pid, addr, data) != 0)
    {
        perror("ptrace(PTRACE_POKEDATA)");
        return false;
    }
    return true;
}
