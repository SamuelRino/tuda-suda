#include "MemoryBlock.h"
#include <cwchar>

bool AllocateBlock(MemoryBlock& block, size_t capacity)
{
    if (block.handle || capacity == 0 || capacity > SIZE_MAX / sizeof(wchar_t)) return false;
    HLOCAL handle = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, capacity * sizeof(wchar_t));
    if (!handle) return false;
    block = {handle, nullptr, capacity};
    return true;
}

bool LockBlock(MemoryBlock& block)
{
    if (!block.handle || block.data) return false;
    block.data = static_cast<wchar_t*>(LocalLock(block.handle));
    return block.data != nullptr;
}

bool UnlockBlock(MemoryBlock& block)
{
    if (!block.data) return false;
    SetLastError(ERROR_SUCCESS);
    const BOOL stillLocked = LocalUnlock(block.handle);
    if (!stillLocked && GetLastError() != ERROR_SUCCESS) return false;
    block.data = nullptr;
    return true;
}

bool FreeBlock(MemoryBlock& block)
{
    if (!block.handle || block.data || LocalFree(block.handle) != nullptr) return false;
    block = {};
    return true;
}

bool WriteBlock(MemoryBlock& block, const wchar_t* text)
{
    if (!block.data || !text) return false;
    const size_t length = std::wcslen(text);
    if (length >= block.capacity) return false;
    std::wmemmove(block.data, text, length + 1);
    return true;
}

const wchar_t* ReadBlock(const MemoryBlock& block)
{
    return block.data;
}

bool CopyBlock(const MemoryBlock& from, MemoryBlock& to)
{
    return WriteBlock(to, ReadBlock(from));
}
