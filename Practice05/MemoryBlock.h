#pragma once
#include <windows.h>

#ifdef MEMORYBLOCK_EXPORTS
#define MEMORY_API __declspec(dllexport)
#else
#define MEMORY_API __declspec(dllimport)
#endif

struct MemoryBlock {
    HLOCAL handle{};
    wchar_t* data{};
    size_t capacity{};
};

MEMORY_API bool AllocateBlock(MemoryBlock& block, size_t capacity);
MEMORY_API bool LockBlock(MemoryBlock& block);
MEMORY_API bool UnlockBlock(MemoryBlock& block);
MEMORY_API bool FreeBlock(MemoryBlock& block);
MEMORY_API bool WriteBlock(MemoryBlock& block, const wchar_t* text);
MEMORY_API const wchar_t* ReadBlock(const MemoryBlock& block);
MEMORY_API bool CopyBlock(const MemoryBlock& from, MemoryBlock& to);
