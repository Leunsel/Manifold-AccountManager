#pragma once

#include "Core/SecureString.h"

#include <cstddef>

namespace Bunker
{
    struct ChunkLimitForTest
    {
        explicit ChunkLimitForTest(std::size_t n)
        {
            SecureArena::Instance().SetChunkLimitForTests(n);
        }
        ~ChunkLimitForTest()
        {
            SecureArena::Instance().SetChunkLimitForTests(SecureArena::kDefaultChunkLimit);
        }
        ChunkLimitForTest(const ChunkLimitForTest&) = delete;
        ChunkLimitForTest& operator=(const ChunkLimitForTest&) = delete;
    };

    struct NoHeapFallbackForTest
    {
        NoHeapFallbackForTest() { SecureArena::Instance().SetHeapFallbackForTests(false); }
        ~NoHeapFallbackForTest() { SecureArena::Instance().SetHeapFallbackForTests(true); }
        NoHeapFallbackForTest(const NoHeapFallbackForTest&) = delete;
        NoHeapFallbackForTest& operator=(const NoHeapFallbackForTest&) = delete;
    };
}
