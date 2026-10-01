#include "UI/ImGuiSecure.h"

#include "imgui.h"

#include <sodium.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

namespace Bunker::UI
{
    namespace
    {
        struct alignas(16) BlockHeader
        {
            std::size_t Size;
            std::size_t Guard;
        };
        static_assert(sizeof(BlockHeader) == 16, "header must preserve 16-byte alignment");

        constexpr std::size_t kGuard = 0x42554E4B45520001ull;

        std::atomic<std::size_t> g_live{ 0 };
        std::atomic<std::size_t> g_wiped{ 0 };

        void* Alloc(std::size_t size, void*)
        {
            if (size == 0)
                size = 1;
            void* raw = std::malloc(sizeof(BlockHeader) + size);
            if (!raw)
                return nullptr;
            auto* head = static_cast<BlockHeader*>(raw);
            head->Size = size;
            head->Guard = kGuard;
            g_live.fetch_add(size, std::memory_order_relaxed);
            return static_cast<unsigned char*>(raw) + sizeof(BlockHeader);
        }

        void Free(void* ptr, void*)
        {
            if (!ptr)
                return;
            auto* head = reinterpret_cast<BlockHeader*>(
                static_cast<unsigned char*>(ptr) - sizeof(BlockHeader));
            if (head->Guard != kGuard)
            {
                std::free(ptr);
                return;
            }
            const std::size_t size = head->Size;
            sodium_memzero(ptr, size);
            head->Guard = 0;
            head->Size = 0;
            g_live.fetch_sub(size, std::memory_order_relaxed);
            g_wiped.fetch_add(size, std::memory_order_relaxed);
            std::free(head);
        }
    }

    void InstallWipeOnFreeAllocator()
    {
        IM_ASSERT(ImGui::GetCurrentContext() == nullptr &&
                  "InstallWipeOnFreeAllocator must run before ImGui::CreateContext");
        ImGui::SetAllocatorFunctions(&Alloc, &Free, nullptr);
    }

    std::size_t ImGuiLiveBytes() { return g_live.load(std::memory_order_relaxed); }
    std::size_t ImGuiWipedBytes() { return g_wiped.load(std::memory_order_relaxed); }
}
