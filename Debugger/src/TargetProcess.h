// TargetProcess.h

#ifndef TARGET_PROCESS_H
#define TARGET_PROCESS_H

#include <sys/types.h>
#include <cstdint>
#include <vector>

class TargetProcess
{
public:
    explicit TargetProcess(pid_t pid);

    pid_t getPid() const;

    bool readMemory(uint64_t addr, uint8_t* buffer, size_t size) const;
    uint64_t readWord(uint64_t addr) const;

    bool writeWord(uint64_t addr, uint64_t data) const;

private:
    pid_t pid;
};

#endif // TARGET_PROCESS_H
