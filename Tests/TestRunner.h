#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Test
{
    struct Case
    {
        const char* Name;
        void (*Fn)();
    };

    std::vector<Case>& Registry();

    struct Registrar
    {
        Registrar(const char* name, void (*fn)()) { Registry().push_back({ name, fn }); }
    };

    struct Failure
    {
        std::string Message;
    };

    void Fail(const char* file, int line, const char* expr, const std::string& extra);

    int RunAll(const char* filter);
}

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static ::Test::Registrar name##_registrar(#name, &name);                   \
    static void name()

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) ::Test::Fail(__FILE__, __LINE__, #expr, {});              \
    } while (false)

#define CHECK_MSG(expr, msg)                                                   \
    do {                                                                       \
        if (!(expr)) ::Test::Fail(__FILE__, __LINE__, #expr, (msg));           \
    } while (false)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        const auto va_ = (a); const auto vb_ = (b);                            \
        if (!(va_ == vb_))                                                     \
            ::Test::Fail(__FILE__, __LINE__, #a " == " #b,                     \
                         "got " + std::to_string(static_cast<long long>(va_)) +\
                         ", expected " + std::to_string(static_cast<long long>(vb_))); \
    } while (false)

#define CHECK_BYTES_EQ(a, b, n)                                                \
    do {                                                                       \
        if (std::memcmp((a), (b), (n)) != 0)                                   \
            ::Test::Fail(__FILE__, __LINE__, #a " == " #b " over " #n " bytes", {}); \
    } while (false)
