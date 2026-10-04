#include "TestRunner.h"

#include "Core/AccentColour.h"
#include "Core/Palette.h"

#include <cstring>
#include <string>

using namespace Bunker;

namespace
{
    double Ratio(std::uint32_t a, std::uint32_t b)
    {
        return ContrastRatio(Rgb::FromPacked(a), Rgb::FromPacked(b));
    }

    void ExpectAtLeast(const Palette& p, const char* what, std::uint32_t fg,
                       std::uint32_t bg, double need)
    {
        const double r = Ratio(fg, bg);
        CHECK_MSG(r >= need, std::string(p.Id) + ": " + what + " " + std::to_string(r) +
                                 " < " + std::to_string(need));
    }
}

TEST_CASE(Palette_EveryThemeIsReadable)
{
    for (std::size_t i = 0; i < PaletteCount(); ++i)
    {
        const Palette& p = PaletteAt(i);
        for (std::uint32_t bg : { p.Surface, p.Panel, p.Elevated })
        {
            ExpectAtLeast(p, "TextPrimary", p.TextPrimary, bg, 7.0);
            ExpectAtLeast(p, "TextSecondary", p.TextSecondary, bg, 4.5);
        }
        for (std::uint32_t bg : { p.Surface, p.Panel })
        {
            ExpectAtLeast(p, "TextMuted", p.TextMuted, bg, 4.5);
            ExpectAtLeast(p, "AccentCore", p.AccentCore, bg, 3.0);
            ExpectAtLeast(p, "AccentBright", p.AccentBright, bg, 4.5);
            ExpectAtLeast(p, "WarnCore", p.WarnCore, bg, 3.0);
            ExpectAtLeast(p, "WarnBright", p.WarnBright, bg, 4.5);
            ExpectAtLeast(p, "RedCore", p.RedCore, bg, 3.0);
            ExpectAtLeast(p, "RedBright", p.RedBright, bg, 4.5);
            ExpectAtLeast(p, "GreenCore", p.GreenCore, bg, 3.0);
        }
        ExpectAtLeast(p, "Speichern-Knopf", p.TextPrimary, p.AccentDim, 3.0);
        ExpectAtLeast(p, "Hinweis-Pille", p.WarnBright, p.WarnDim, 3.0);
    }
}

TEST_CASE(Palette_LightMeansALightSurface)
{
    for (std::size_t i = 0; i < PaletteCount(); ++i)
    {
        const Palette& p = PaletteAt(i);
        const bool light = RelativeLuminance(Rgb::FromPacked(p.Surface)) > 0.5;
        CHECK_MSG(light == p.Light, std::string(p.Id) + ": Light does not match the surface");
        CHECK_MSG(RelativeLuminance(Rgb::FromPacked(p.Shade)) < 0.1,
                  std::string(p.Id) + ": Shade is not dark");
    }
}

TEST_CASE(Palette_OnlyTheOledThemesNeedColumnRules)
{
    for (std::size_t i = 0; i < PaletteCount(); ++i)
    {
        const Palette& p = PaletteAt(i);
        const bool oled = p.Surface == 0x000000 && p.Panel == 0x000000;
        CHECK_MSG(SeparatesByTone(p) == !oled,
                  std::string(p.Id) + (oled ? ": separates by tone"
                                            : ": does NOT separate by tone"));
        if (!SeparatesByTone(p))
            CHECK_MSG(Ratio(p.Line, p.Surface) >= 1.2,
                      std::string(p.Id) + ": the divider disappears into the ground");
    }
}

TEST_CASE(Palette_IdsAreUniqueAndTheFirstIsTheDefault)
{
    CHECK(PaletteCount() >= 9);
    CHECK(std::strcmp(PaletteAt(0).Id, "arc") == 0);
    for (std::size_t i = 0; i < PaletteCount(); ++i)
    {
        CHECK_EQ(PaletteIndex(PaletteAt(i).Id), i);
        for (std::size_t j = i + 1; j < PaletteCount(); ++j)
            CHECK_MSG(std::strcmp(PaletteAt(i).Id, PaletteAt(j).Id) != 0,
                      std::string("duplicate id ") + PaletteAt(i).Id);
    }
    CHECK_EQ(PaletteIndex(""), std::size_t{ 0 });
    CHECK_EQ(PaletteIndex("sepia-2031"), std::size_t{ 0 });
    CHECK_EQ(PaletteIndex("ARC"), std::size_t{ 0 });
    CHECK_MSG(&PaletteAt(PaletteCount() + 5) == &PaletteAt(0),
              "an out-of-range index did not fall back to the default");
}

TEST_CASE(Palette_EveryOneHasItsOwnHeaderNumber)
{
    for (std::size_t i = 0; i < PaletteCount(); ++i)
    {
        const char* id = PaletteAt(i).Id;
        const std::uint8_t code = PaletteHeaderCode(id);
        CHECK_MSG(code != 0, std::string(id) + ": no header number");
        CHECK_MSG(PaletteIdForHeaderCode(code) == id,
                  std::string(id) + ": the number does not map back");
        for (std::size_t j = i + 1; j < PaletteCount(); ++j)
            CHECK_MSG(PaletteHeaderCode(PaletteAt(j).Id) != code,
                      std::string("duplicate number for ") + id);
    }
    CHECK(PaletteHeaderCode("") == 0);
    CHECK(PaletteHeaderCode("sepia-2031") == 0);
    CHECK(PaletteIdForHeaderCode(0).empty());
    CHECK(PaletteIdForHeaderCode(200).empty());
}

TEST_CASE(Palette_HeaderNumbersNeverMove)
{
    CHECK(PaletteHeaderCode("arc") == 1);
    CHECK(PaletteHeaderCode("dark") == 2);
    CHECK(PaletteHeaderCode("light") == 3);
    CHECK(PaletteHeaderCode("black") == 4);
    CHECK(PaletteHeaderCode("fjord") == 5);
    CHECK(PaletteHeaderCode("mint") == 6);
    CHECK(PaletteHeaderCode("aubergine") == 7);
    CHECK(PaletteHeaderCode("midnight") == 8);
    CHECK(PaletteHeaderCode("moss") == 9);
    CHECK(PaletteHeaderCode("terminal") == 10);
    CHECK(PaletteHeaderCode("red") == 11);
    CHECK(PaletteHeaderCode("red-oled") == 12);
    CHECK(PaletteHeaderCode("red-light") == 13);
    CHECK(PaletteHeaderCode("green") == 14);
    CHECK(PaletteHeaderCode("green-oled") == 15);
    CHECK(PaletteHeaderCode("green-light") == 16);
    CHECK(PaletteHeaderCode("blue-oled") == 17);
    CHECK(PaletteHeaderCode("red-auto") == 18);
    CHECK(PaletteHeaderCode("green-auto") == 19);
    CHECK(PaletteHeaderCode("blue-auto") == 20);
}

TEST_CASE(Palette_FollowingWindowsPicksTheMatchingVariant)
{
    CHECK(ResolvePaletteId("red-auto", false) == "red");
    CHECK(ResolvePaletteId("red-auto", true) == "red-light");
    CHECK(ResolvePaletteId("green-auto", false) == "green");
    CHECK(ResolvePaletteId("green-auto", true) == "green-light");
    CHECK(ResolvePaletteId("blue-auto", false) == "dark");
    CHECK(ResolvePaletteId("blue-auto", true) == "light");
    for (const char* fixed : { "mint", "red-oled", "light", "black", "", "sepia-2031" })
    {
        CHECK_MSG(ResolvePaletteId(fixed, true) == fixed, std::string(fixed) + " changed with Windows");
        CHECK_MSG(ResolvePaletteId(fixed, false) == fixed, std::string(fixed) + " changed with Windows");
    }
    for (const char* automatic : { "red-auto", "green-auto", "blue-auto" })
        CHECK_MSG(PaletteIdForHeaderCode(PaletteHeaderCode(automatic)) == automatic,
                  std::string(automatic) + ": the unlock screen cannot follow Windows");
}

TEST_CASE(Palette_EveryChoiceInTheSettingsMapsBackToItself)
{
    std::size_t covered = 0;
    for (std::size_t family = 0; family < ThemeFamilyCount(); ++family)
    {
        CHECK_MSG(ThemeModeAvailable(family, ThemeMode::Dark), "a colour without a dark variant");
        for (std::size_t m = 0; m < kThemeModeCount; ++m)
        {
            const ThemeMode mode = static_cast<ThemeMode>(m);
            const std::string id(ThemeIdOf({ family, mode }));
            if (!ThemeModeAvailable(family, mode))
            {
                CHECK_MSG(id == ThemeIdOf({ family, ThemeMode::Dark }), id + ": no fallback to dark");
                continue;
            }
            const ThemeChoice back = ThemeChoiceOf(id);
            CHECK_MSG(back.Family == family && back.Mode == mode, id + ": does not map back");
            if (mode != ThemeMode::System)
            {
                CHECK_MSG(PaletteAt(PaletteIndex(id)).Id == id, id + ": not a palette");
                ++covered;
            }
        }
    }
    CHECK_EQ(covered, PaletteCount());

    CHECK(ThemeIdOf({}) == PaletteAt(0).Id);
    CHECK(ThemeChoiceOf("").Family == 0);
    CHECK(ThemeChoiceOf("sepia-2031").Family == 0);
    CHECK(ThemeIdOf(ThemeChoiceOf("moss")) == "green");
    CHECK(ThemeIdOf(ThemeChoiceOf("black")) == "blue-oled");
    CHECK(ThemeIdOf(ThemeChoiceOf("aubergine")) == "midnight");
    CHECK(ThemeIdOf({ ThemeFamilyCount(), ThemeMode::Light }) == PaletteAt(0).Id);
}

TEST_CASE(Palette_RetiredSchemesResolveToTheirSuccessor)
{
    CHECK_EQ(PaletteIndex("black"), PaletteIndex("blue-oled"));
    CHECK_EQ(PaletteIndex("fjord"), PaletteIndex("mint"));
    CHECK_EQ(PaletteIndex("aubergine"), PaletteIndex("midnight"));
    CHECK_EQ(PaletteIndex("moss"), PaletteIndex("green"));
    for (const char* retired : { "black", "fjord", "aubergine", "moss" })
    {
        CHECK_MSG(PaletteIndex(retired) != 0, std::string(retired) + " fell back to the default");
        CHECK_MSG(PaletteIdForHeaderCode(PaletteHeaderCode(retired)) == retired,
                  std::string(retired) + ": header number lost");
    }
}

TEST_CASE(Palette_EveryAccentComesInThreeVariants)
{
    for (const char* dark : { "red", "green", "dark" })
    {
        const Palette& d = PaletteAt(PaletteIndex(dark));
        const std::string oledId = std::strcmp(dark, "dark") == 0 ? "blue-oled" : std::string(dark) + "-oled";
        const Palette& o = PaletteAt(PaletteIndex(oledId));
        CHECK_MSG(o.AccentCore == d.AccentCore && o.AccentBright == d.AccentBright &&
                      o.AccentDim == d.AccentDim,
                  oledId + ": accent differs from the dark variant");
        CHECK_MSG(o.Surface == 0x000000, oledId + ": not black");
    }
    CHECK(PaletteAt(PaletteIndex("red-light")).Light);
    CHECK(PaletteAt(PaletteIndex("green-light")).Light);
    CHECK(PaletteAt(PaletteIndex("light")).Light);
}

TEST_CASE(Palette_TheDefaultGateKeepsItsRed)
{
    const Palette& p = PaletteAt(0);
    CHECK_EQ(p.GlowCore, p.RedCore);
    CHECK_EQ(p.GlowDim, p.RedDim);
}

TEST_CASE(Palette_TheDefaultIsWhatTheProgramHadBefore)
{
    const Palette& p = PaletteAt(0);
    CHECK_EQ(p.Panel, std::uint32_t{ 0x161B21 });
    CHECK_EQ(Rgb::FromPacked(p.Panel).Packed(), kPanelColour.Packed());
    CHECK_EQ(p.AccentCore, std::uint32_t{ 0xE8A33D });
    CHECK_EQ(p.WarnCore, p.AccentCore);
    CHECK_EQ(p.WarnBright, p.AccentBright);
    CHECK_EQ(p.WarnDim, p.AccentDim);
    CHECK_EQ(p.Shade, p.Void);
    CHECK(!p.Light);
    CHECK_MSG(!p.Square && !p.Monospace, "the default became square or monospace");
}

TEST_CASE(Palette_OnlyTheTerminalChangesShapeAndType)
{
    for (std::size_t i = 0; i < PaletteCount(); ++i)
    {
        const Palette& p = PaletteAt(i);
        const bool terminal = std::strcmp(p.Id, "terminal") == 0;
        CHECK_MSG(p.Square == terminal, std::string(p.Id) + ": Square does not match");
        CHECK_MSG(p.Monospace == terminal, std::string(p.Id) + ": Monospace does not match");
    }
}

TEST_CASE(Accent_AReadableColourIsLeftAlone)
{
    for (const char* id : { "steam", "github", "microsoft", "discord", "x" })
    {
        const Rgb derived = DerivedAccent(id);
        const Rgb back = ReadableAgainst(derived, kPanelColour);
        CHECK_MSG(back.Packed() == derived.Packed(), std::string(id) + " was changed");
    }
}

TEST_CASE(Accent_OnALightPanelEveryColourIsDarkenedUntilReadable)
{
    const Rgb light = Rgb::FromPacked(PaletteAt(PaletteIndex("light")).Panel);
    for (const char* id : { "steam", "github", "microsoft", "discord", "x", "rockstar",
                            "epic-games", "gog", "nintendo" })
    {
        const Rgb before = Rgb::FromPacked(ResolveAccent(0, id));
        const Rgb after = ReadableAgainst(before, light);
        CHECK_MSG(ContrastRatio(after, light) >= kMinContrast,
                  std::string(id) + ": not readable on a light ground");
        CHECK_MSG(RelativeLuminance(after) <= RelativeLuminance(before),
                  std::string(id) + ": became lighter instead of darker");
    }
}

TEST_CASE(Accent_ExtremesStillGiveAnAnswer)
{
    const Rgb white{ 255, 255, 255 };
    const Rgb black{ 0, 0, 0 };
    CHECK(ContrastRatio(ReadableAgainst(white, white), white) >= kMinContrast);
    CHECK(ContrastRatio(ReadableAgainst(black, black), black) >= kMinContrast);
}
