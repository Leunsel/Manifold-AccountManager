#pragma once

#include "TestRunner.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Test
{
    class HeapWatch
    {
    public:
        HeapWatch();
        ~HeapWatch();
        HeapWatch(const HeapWatch&) = delete;
        HeapWatch& operator=(const HeapWatch&) = delete;

        bool Watch(const void* needle, std::size_t length);
        bool Watch(std::string_view needle)
        {
            return Watch(needle.data(), needle.size());
        }

        void Arm();
        void Disarm();

        std::size_t Hits() const;
        std::size_t Scanned() const;
        std::size_t Unscanned() const;
    };

    template <class Fn>
    std::size_t HeapCopiesDuring(std::initializer_list<std::string_view> needles, Fn&& work)
    {
        HeapWatch watch;
        for (std::string_view n : needles)
            CHECK(watch.Watch(n));
        watch.Arm();
        work();
        watch.Disarm();
        CHECK_MSG(watch.Unscanned() == 0,
                  std::to_string(watch.Unscanned()) +
                      " freed blocks in the window could not be scanned by the guard");
        return watch.Hits();
    }
}
