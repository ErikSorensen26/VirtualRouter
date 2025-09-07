#include <Profiler.h>

thread_local Profiler::ThreadBuffer* Profiler::tlsBuf_ = nullptr;
