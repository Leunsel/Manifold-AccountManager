#include "TestRunner.h"

#include "Core/Endian.h"
#include "Core/Secret.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;

TEST_CASE(Secret_AllocateWipeAndRelease)
{
    const std::size_t before = LiveSecretCount();
    {
        SecureBuffer b(64);
        CHECK(b.IsValid());
        CHECK_EQ(b.Size(), std::size_t{ 64 });
        CHECK_EQ(LiveSecretCount(), before + 1);

        for (std::size_t i = 0; i < b.Size(); ++i)
            CHECK_EQ(b.Data()[i], static_cast<unsigned char>(0));

        std::memset(b.Data(), 0xAB, b.Size());
        b.Wipe();
        for (std::size_t i = 0; i < b.Size(); ++i)
            CHECK_EQ(b.Data()[i], static_cast<unsigned char>(0));
    }
    CHECK_EQ(LiveSecretCount(), before);
}

TEST_CASE(Secret_ZeroSizedBufferIsInvalidNotCrashing)
{
    SecureBuffer b(0);
    CHECK(!b.IsValid());
    CHECK_EQ(b.Size(), std::size_t{ 0 });
    b.Wipe();
    b.Reset();
}

TEST_CASE(Secret_MoveTransfersOwnershipExactlyOnce)
{
    const std::size_t before = LiveSecretCount();

    SecureBuffer a(32);
    CHECK(a.IsValid());
    std::memset(a.Data(), 0x5A, a.Size());
    const unsigned char* raw = a.Data();

    SecureBuffer b(std::move(a));
    CHECK(!a.IsValid());
    CHECK(b.IsValid());
    CHECK(b.Data() == raw);
    CHECK_EQ(LiveSecretCount(), before + 1);

    SecureBuffer c(8);
    c = std::move(b);
    CHECK(!b.IsValid());
    CHECK(c.Data() == raw);
    CHECK_EQ(c.Size(), std::size_t{ 32 });
    CHECK_EQ(LiveSecretCount(), before + 1);

    for (std::size_t i = 0; i < c.Size(); ++i)
        CHECK_EQ(c.Data()[i], static_cast<unsigned char>(0x5A));
}

TEST_CASE(Secret_SelfMoveIsHarmless)
{
    SecureBuffer a(16);
    std::memset(a.Data(), 0x11, a.Size());
    const unsigned char* raw = a.Data();

    SecureBuffer& ref = a;
    a = std::move(ref);

    CHECK(a.IsValid());
    CHECK(a.Data() == raw);
    CHECK_EQ(a.Data()[0], static_cast<unsigned char>(0x11));
}

TEST_CASE(Secret_ManyBuffersBalanceOut)
{
    const std::size_t before = LiveSecretCount();
    {
        std::vector<SecureBuffer> v;
        for (int i = 0; i < 64; ++i)
            v.emplace_back(48);
        CHECK_EQ(LiveSecretCount(), before + 64);
        v.reserve(256);
        CHECK_EQ(LiveSecretCount(), before + 64);
    }
    CHECK_EQ(LiveSecretCount(), before);
}

TEST_CASE(Secret_EqualsIsConstantTimeApi)
{
    unsigned char a[32], b[32];
    std::memset(a, 0x42, sizeof(a));
    std::memset(b, 0x42, sizeof(b));
    CHECK(SecretEquals(a, b, sizeof(a)));

    b[sizeof(b) - 1] ^= 0x01;
    CHECK(!SecretEquals(a, b, sizeof(a)));

    CHECK(!SecretEquals(nullptr, b, sizeof(b)));
    CHECK(!SecretEquals(a, nullptr, sizeof(a)));
}

TEST_CASE(Secret_ProtectRoundTrip)
{
    SecureBuffer b(32);
    CHECK(b.IsValid());
    std::memset(b.Data(), 0x7E, b.Size());
    b.ProtectNoAccess();
    b.Unprotect();
    CHECK_EQ(b.Data()[0], static_cast<unsigned char>(0x7E));
}

TEST_CASE(Endian_RoundTripsAndByteOrder)
{
    unsigned char buf[8] = {};

    StoreU16Le(buf, 0x1234);
    CHECK_EQ(buf[0], static_cast<unsigned char>(0x34));
    CHECK_EQ(buf[1], static_cast<unsigned char>(0x12));
    CHECK_EQ(LoadU16Le(buf), static_cast<std::uint16_t>(0x1234));

    StoreU32Le(buf, 0xDEADBEEFu);
    CHECK_EQ(buf[0], static_cast<unsigned char>(0xEF));
    CHECK_EQ(buf[3], static_cast<unsigned char>(0xDE));
    CHECK(LoadU32Le(buf) == 0xDEADBEEFu);

    StoreU64Le(buf, 0x0102030405060708ull);
    CHECK_EQ(buf[0], static_cast<unsigned char>(0x08));
    CHECK_EQ(buf[7], static_cast<unsigned char>(0x01));
    CHECK(LoadU64Le(buf) == 0x0102030405060708ull);
}

TEST_CASE(Endian_ExtremesSurvive)
{
    unsigned char buf[8];
    for (std::uint64_t v : { std::uint64_t{ 0 },
                             std::uint64_t{ 1 },
                             std::uint64_t{ 0xFFFFFFFFull },
                             std::uint64_t{ 0xFFFFFFFFFFFFFFFFull } })
    {
        StoreU64Le(buf, v);
        CHECK(LoadU64Le(buf) == v);
    }

    StoreU32Le(buf, 0xFFFFFFFFu);
    CHECK(LoadU32Le(buf) == 0xFFFFFFFFu);
    StoreU16Le(buf, 0xFFFFu);
    CHECK(LoadU16Le(buf) == 0xFFFFu);
}

namespace
{
    struct RegionEvent
    {
        const void* Address = nullptr;
        std::size_t Size = 0;
        bool Live = false;
    };

    std::vector<RegionEvent> g_events;

    void RecordRegion(const void* address, std::size_t size, bool live) noexcept
    {
        g_events.push_back(RegionEvent{ address, size, live });
    }

    struct HookGuard
    {
        HookGuard() { g_events.clear(); SetSecretRegionHook(&RecordRegion); }
        ~HookGuard() { SetSecretRegionHook(nullptr); g_events.clear(); }
    };
}

TEST_CASE(Secret_TheRegionHookSeesEveryBuffer)
{
    HookGuard guard;

    const void* seen = nullptr;
    {
        SecureBuffer b(4096);
        CHECK(b.IsValid());
        seen = b.Data();

        CHECK_EQ(g_events.size(), std::size_t{ 1 });
        if (g_events.empty())
            return;
        CHECK_MSG(g_events[0].Live, "the creation was not reported as 'alive'");
        CHECK_MSG(g_events[0].Address == seen,
                  "a different address than the payload was reported");
        CHECK_EQ(g_events[0].Size, std::size_t{ 4096 });
    }

    CHECK_EQ(g_events.size(), std::size_t{ 2 });
    if (g_events.size() < 2)
        return;
    CHECK_MSG(!g_events[1].Live, "the release was not reported");
    CHECK_MSG(g_events[1].Address == seen,
              "the release reported a different address than the creation");
}

TEST_CASE(Secret_TheRegionIsReportedBeforeTheMemoryGoesAway)
{
    static bool readable = false;
    static const unsigned char* watch = nullptr;

    SetSecretRegionHook([](const void* address, std::size_t size, bool live) noexcept
    {
        if (!live && address == watch && size > 0)
            readable = *static_cast<const unsigned char*>(address) == 0x5A;
    });

    {
        SecureBuffer b(64);
        CHECK(b.IsValid());
        watch = b.Data();
        std::memset(b.Data(), 0x5A, b.Size());
    }
    SetSecretRegionHook(nullptr);

    CHECK_MSG(readable,
              "at release the payload was no longer readable -- the "
              "report comes too late");
}

TEST_CASE(Secret_NoHookIsNoProblem)
{
    SetSecretRegionHook(nullptr);
    SecureBuffer b(128);
    CHECK(b.IsValid());
    b.Reset();
    CHECK(!b.IsValid());
}

TEST_CASE(Secret_AFailedAllocationTellsNobody)
{
    HookGuard guard;
    SecureBuffer empty(0);
    CHECK(!empty.IsValid());
    CHECK_EQ(g_events.size(), std::size_t{ 0 });
}
