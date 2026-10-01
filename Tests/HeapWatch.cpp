#include "HeapWatch.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

#include <crtdbg.h>
#include <malloc.h>

namespace
{
    constexpr std::size_t kMaxNeedles = 8;
    constexpr std::size_t kMaxNeedleBytes = 512;

    constexpr std::size_t kSliceFrom = 16;
    constexpr std::size_t kSliceBytes = 12;

    struct WatchState
    {
        std::atomic<bool> Armed{ false };
        bool InUse = false;
        unsigned char Needles[kMaxNeedles][kMaxNeedleBytes] = {};
        std::size_t Lengths[kMaxNeedles] = {};
        std::size_t Count = 0;
        std::atomic<std::size_t> Hits{ 0 };
        std::atomic<std::size_t> Scanned{ 0 };
        std::atomic<std::size_t> Unscanned{ 0 };
    };

    constinit WatchState g_watch;

    bool Contains(const unsigned char* hay, std::size_t hayLen,
                  const unsigned char* needle, std::size_t needleLen)
    {
        if (needleLen == 0 || hayLen < needleLen)
            return false;
        for (std::size_t i = 0; i + needleLen <= hayLen; ++i)
            if (hay[i] == needle[0] && std::memcmp(hay + i, needle, needleLen) == 0)
                return true;
        return false;
    }

    bool ContainsNeedleOrPiece(const unsigned char* hay, std::size_t hayLen,
                               const unsigned char* needle, std::size_t needleLen)
    {
        if (Contains(hay, hayLen, needle, needleLen))
            return true;
        if (needleLen < kSliceFrom)
            return false;
        const std::size_t offsets[] = { 1, needleLen / 2 - kSliceBytes / 2,
                                        needleLen - kSliceBytes };
        for (std::size_t at : offsets)
            if (Contains(hay, hayLen, needle + at, kSliceBytes))
                return true;
        return false;
    }

    void Inspect(void* block, std::size_t size)
    {
        if (!block || !g_watch.Armed.load(std::memory_order_acquire))
            return;
        g_watch.Scanned.fetch_add(1, std::memory_order_relaxed);
        const unsigned char* bytes = static_cast<const unsigned char*>(block);
        for (std::size_t i = 0; i < g_watch.Count; ++i)
        {
            if (ContainsNeedleOrPiece(bytes, size, g_watch.Needles[i], g_watch.Lengths[i]))
            {
                g_watch.Hits.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    void NoteUnsized(void* block)
    {
        if (block && g_watch.Armed.load(std::memory_order_acquire))
            g_watch.Unscanned.fetch_add(1, std::memory_order_relaxed);
    }

    void ReleaseBlock(void* block)
    {
        _free_dbg(block, _UNKNOWN_BLOCK);
    }
}

void operator delete(void* block) noexcept
{
    NoteUnsized(block);
    ReleaseBlock(block);
}

void operator delete(void* block, std::size_t size) noexcept
{
    Inspect(block, size);
    ReleaseBlock(block);
}

void operator delete[](void* block) noexcept
{
    NoteUnsized(block);
    ReleaseBlock(block);
}

void operator delete[](void* block, std::size_t size) noexcept
{
    Inspect(block, size);
    ReleaseBlock(block);
}

void operator delete(void* block, std::align_val_t) noexcept
{
    NoteUnsized(block);
    _aligned_free(block);
}

void operator delete(void* block, std::size_t size, std::align_val_t) noexcept
{
    Inspect(block, size);
    _aligned_free(block);
}

void operator delete[](void* block, std::align_val_t) noexcept
{
    NoteUnsized(block);
    _aligned_free(block);
}

void operator delete[](void* block, std::size_t size, std::align_val_t) noexcept
{
    Inspect(block, size);
    _aligned_free(block);
}

namespace Test
{
    HeapWatch::HeapWatch()
    {
        if (g_watch.InUse)
            std::abort();
        g_watch.InUse = true;
        g_watch.Count = 0;
        g_watch.Hits.store(0);
        g_watch.Scanned.store(0);
        g_watch.Unscanned.store(0);
    }

    HeapWatch::~HeapWatch()
    {
        Disarm();
        g_watch.Count = 0;
        std::memset(g_watch.Needles, 0, sizeof(g_watch.Needles));
        g_watch.InUse = false;
    }

    bool HeapWatch::Watch(const void* needle, std::size_t length)
    {
        if (g_watch.Armed.load() || !needle || length == 0 ||
            length > kMaxNeedleBytes || g_watch.Count >= kMaxNeedles)
            return false;
        std::memcpy(g_watch.Needles[g_watch.Count], needle, length);
        g_watch.Lengths[g_watch.Count] = length;
        ++g_watch.Count;
        return true;
    }

    void HeapWatch::Arm() { g_watch.Armed.store(true, std::memory_order_release); }
    void HeapWatch::Disarm() { g_watch.Armed.store(false, std::memory_order_release); }

    std::size_t HeapWatch::Hits() const { return g_watch.Hits.load(); }
    std::size_t HeapWatch::Scanned() const { return g_watch.Scanned.load(); }
    std::size_t HeapWatch::Unscanned() const { return g_watch.Unscanned.load(); }
}
