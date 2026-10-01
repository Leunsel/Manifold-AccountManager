#include "TestRunner.h"

#include "Core/AccentColour.h"

#include <cmath>
#include <set>
#include <string>

using namespace Bunker;

namespace
{
    constexpr double kPromisedContrast = 3.5;

    bool Near(double a, double b, double tolerance = 0.01)
    {
        return std::fabs(a - b) <= tolerance;
    }
}

TEST_CASE(Accent_ContrastMatchesTheKnownAnchors)
{
    const Rgb black{ 0, 0, 0 };
    const Rgb white{ 255, 255, 255 };

    CHECK(Near(ContrastRatio(black, white), 21.0, 0.05));
    CHECK(Near(ContrastRatio(white, black), 21.0, 0.05));
    CHECK(Near(ContrastRatio(white, white), 1.0));
    CHECK(Near(RelativeLuminance(white), 1.0, 0.001));
    CHECK(Near(RelativeLuminance(black), 0.0, 0.001));

    CHECK_MSG(Near(RelativeLuminance(Rgb{ 128, 128, 128 }), 0.216, 0.005),
              "the channels are not linearised");
}

TEST_CASE(Accent_TheThresholdIsNotQuietlyLowered)
{
    double threshold = kMinContrast;
    CHECK_MSG(threshold >= kPromisedContrast,
              "the contrast threshold was lowered below the promise");
}

TEST_CASE(Accent_EveryHueIsReadableOnThePanel)
{
    for (int hue = 0; hue < 360; ++hue)
    {
        const Rgb c = ReadableFromHue(static_cast<double>(hue));
        const double ratio = ContrastRatio(c, kPanelColour);
        CHECK_MSG(ratio >= kPromisedContrast,
                  "hue " + std::to_string(hue) + " measures only " +
                  std::to_string(ratio));
    }
}

TEST_CASE(Accent_EveryPlatformIsReadable)
{
    const char* const platforms[] = {
        "steam", "google", "github", "discord", "epic-games", "ubisoft",
        "rockstar", "gog", "nintendo", "ea", "battle-net", "unreal-engine",
        "amazon", "apple", "microsoft", "paypal", "dropbox", "spotify",
        "a-platform-that-does-not-exist", "x", "0",
    };
    for (const char* id : platforms)
    {
        const Rgb c = DerivedAccent(id);
        CHECK_MSG(ContrastRatio(c, kPanelColour) >= kPromisedContrast, id);
    }
}

TEST_CASE(Accent_IsStableForTheSameName)
{
    for (int i = 0; i < 16; ++i)
        CHECK(DerivedAccent("steam").Packed() == DerivedAccent("steam").Packed());

    CHECK_MSG(DerivedAccent("steam").Packed() != DerivedAccent("discord").Packed(),
              "two platforms got the same colour");
}

TEST_CASE(Accent_NoPlatformMeansNoColour)
{
    CHECK_EQ(DerivedAccent("").Packed(), std::uint32_t{ 0 });
    CHECK_EQ(DerivedAccent(nullptr).Packed(), std::uint32_t{ 0 });
    CHECK_MSG(DerivedAccent("steam").Packed() != 0,
              "an existing platform got no colour");
}

TEST_CASE(Accent_TheHuesAreSpreadOut)
{
    const char* const platforms[] = {
        "steam", "google", "github", "discord", "epic-games", "ubisoft",
        "rockstar", "gog", "nintendo", "ea", "battle-net", "unreal-engine",
        "amazon", "apple", "microsoft", "paypal", "dropbox", "spotify",
    };
    std::set<std::uint32_t> seen;
    for (const char* id : platforms)
        seen.insert(DerivedAccent(id).Packed());

    CHECK_MSG(seen.size() >= 16, "too many platforms share a colour");
}

TEST_CASE(Accent_PackingRoundTrips)
{
    for (std::uint32_t v : { 0x000000u, 0xFFFFFFu, 0x123456u, 0xABCDEFu })
        CHECK_EQ(Rgb::FromPacked(v).Packed(), v);

    const Rgb c = DerivedAccent("steam");
    const Rgb back = Rgb::FromPacked(c.Packed());
    CHECK(back.R == c.R && back.G == c.G && back.B == c.B);
}

TEST_CASE(Accent_EveryTunedColourIsReadableToo)
{
    const char* const tuned[] = {
        "epic-games", "ubisoft", "rockstar", "gog", "nintendo", "ea",
    };
    for (const char* id : tuned)
    {
        const std::uint32_t packed = TunedAccent(id);
        CHECK_MSG(packed != 0, id);
        const double ratio = ContrastRatio(Rgb::FromPacked(packed), kPanelColour);
        CHECK_MSG(ratio >= kPromisedContrast,
                  std::string(id) + " measures only " + std::to_string(ratio));
    }
}

TEST_CASE(Accent_AnUnknownPlatformHasNoTunedColour)
{
    CHECK_EQ(TunedAccent("a-platform-that-does-not-exist"), std::uint32_t{ 0 });
    CHECK_EQ(TunedAccent(""), std::uint32_t{ 0 });
    CHECK_EQ(TunedAccent(nullptr), std::uint32_t{ 0 });
    CHECK_EQ(TunedAccent("e"), std::uint32_t{ 0 });
    CHECK_EQ(TunedAccent("ubisoft-connect"), std::uint32_t{ 0 });
}

TEST_CASE(Accent_TheOwnersChoiceWins)
{
    CHECK_EQ(ResolveAccent(0x123456u, "ubisoft"), std::uint32_t{ 0x123456u });
    CHECK_EQ(ResolveAccent(0x123456u, "steam"), std::uint32_t{ 0x123456u });
    CHECK_EQ(ResolveAccent(0x123456u, ""), std::uint32_t{ 0x123456u });
}

TEST_CASE(Accent_TheTunedColourBeatsTheDerivedOne)
{
    for (const char* id : { "ubisoft", "rockstar", "gog", "nintendo", "ea", "epic-games" })
    {
        CHECK_MSG(ResolveAccent(0, id) == TunedAccent(id), id);
        CHECK_MSG(ResolveAccent(0, id) != DerivedAccent(id).Packed(),
                  std::string(id) + ": tuned and derived are equal, "
                                    "the test checks nothing");
    }
}

TEST_CASE(Accent_WithoutAnythingItDerives)
{
    CHECK_EQ(ResolveAccent(0, "steam"), DerivedAccent("steam").Packed());
    CHECK(ResolveAccent(0, "steam") != 0);
}

TEST_CASE(Accent_NoPlatformAndNoChoiceIsNoColour)
{
    CHECK_EQ(ResolveAccent(0, ""), std::uint32_t{ 0 });
    CHECK_EQ(ResolveAccent(0, nullptr), std::uint32_t{ 0 });
}

TEST_CASE(Accent_TheSymbolColoursOnlyWithoutAPlatform)
{
    CHECK_EQ(ResolveAccent(0, "", "building-columns"),
             DerivedAccent("building-columns").Packed());
    CHECK(ResolveAccent(0, "", "building-columns") != 0);
    CHECK_EQ(ResolveAccent(0, nullptr, "building-columns"),
             ResolveAccent(0, "", "building-columns"));
    CHECK_EQ(ResolveAccent(0, "steam", "building-columns"), ResolveAccent(0, "steam"));
    CHECK_EQ(ResolveAccent(0x123456u, "", "building-columns"), std::uint32_t{ 0x123456u });
    CHECK_EQ(ResolveAccent(0, "", ""), std::uint32_t{ 0 });
    CHECK_EQ(ResolveAccent(0, nullptr, nullptr), std::uint32_t{ 0 });
}

TEST_CASE(Accent_ResolveDropsTheHigherByte)
{
    CHECK_EQ(ResolveAccent(0xFF123456u, "steam"), std::uint32_t{ 0x123456u });
    CHECK_MSG(ResolveAccent(0xFF000000u, "ubisoft") == TunedAccent("ubisoft"),
              "a value only in the high byte counted as an own choice");
}
