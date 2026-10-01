#include "TestRunner.h"

#include "Core/GermanText.h"
#include "Core/Redact.h"

#include <set>
#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    const std::string kMask = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";

    bool Contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

TEST_CASE(Redact_KeepsThePrefixAndTheWholeDomain)
{
    const std::string r = RedactAccountHint("alex.doe@example.com", 3);
    CHECK_MSG(r == "ale" + kMask + "@example.com", "gave \"" + r + "\"");

    CHECK(Contains(r, "@example.com"));
    CHECK_MSG(!Contains(r, "doe"), "the hidden part is still there");
}

TEST_CASE(Redact_MasksAFixedRunNotOnePerCharacter)
{
    const std::string shortOne = RedactAccountHint("abcdefgh@x.de", 3);
    const std::string longOne =
        RedactAccountHint("abcdefghijklmnopqrstuvwxyz0123456789@x.de", 3);

    CHECK_MSG(shortOne.size() == longOne.size(),
              "\"" + shortOne + "\" and \"" + longOne + "\" differ in length");
    CHECK(Contains(shortOne, kMask));
    CHECK(Contains(longOne, kMask));
}

TEST_CASE(Redact_ShowsNoMaskWhenNothingWasHidden)
{
    CHECK(RedactAccountHint("ab", 3) == "ab");
    CHECK(RedactAccountHint("abc", 3) == "abc");
    CHECK(RedactAccountHint("ab@x.de", 3) == "ab@x.de");
    CHECK(RedactAccountHint("abc@x.de", 3) == "abc@x.de");
    CHECK(RedactAccountHint("", 3) == "");

    CHECK(RedactAccountHint("abcd", 3) == "abc" + kMask);
    CHECK(RedactAccountHint("abcd@x.de", 3) == "abc" + kMask + "@x.de");
}

TEST_CASE(Redact_HandlesThingsThatAreNotAddresses)
{
    CHECK(RedactAccountHint("AlexDoe#1234", 3) == "Ale" + kMask);
    CHECK(RedactAccountHint("+49 170 1234567", 3) == "+49" + kMask);

    const std::string weird = RedactAccountHint("a@b@c.de", 3);
    CHECK_MSG(weird == "a@b" + kMask, "gave \"" + weird + "\"");

    CHECK(RedactAccountHint("@nurdomain.de", 3) == "@nu" + kMask);
    CHECK(RedactAccountHint("nurlokal@", 3) == "nur" + kMask);
}

TEST_CASE(Redact_NeverCutsAUtf8CharacterInHalf)
{
    const char* values[] = {
        "j\xC3\xB6rg.m\xC3\xBCller@example.de",
        "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F@x.de",
        "\xF0\x9F\x94\x92\xF0\x9F\x94\x92\xF0\x9F\x94\x92\xF0\x9F\x94\x92@x.de",
    };
    for (const char* v : values)
    {
        for (std::size_t keep = 0; keep <= 8; ++keep)
        {
            const std::string r = RedactAccountHint(v, keep);
            CHECK_MSG(Utf8IsValid(r),
                      std::string("broken UTF-8 at keep=") + std::to_string(keep) +
                          " from \"" + v + "\"");
        }
    }
}

TEST_CASE(Redact_WidensUntilTwoEntriesCanBeToldApart)
{
    const std::vector<std::string_view> values = {
        "alex.doe@example.com",
        "alex.work@example.com",
    };
    const std::vector<std::string> shown = RedactAccountHints(values);
    CHECK_EQ(shown.size(), std::size_t{ 2 });
    CHECK_MSG(shown[0] != shown[1],
              "both rows show \"" + shown[0] + "\"");

    CHECK_MSG(shown[0].find("alex.d") == 0,
              "\"" + shown[0] + "\" does not reveal exactly as much as needed");
    CHECK_MSG(!Contains(shown[0], "doe"),
              "\"" + shown[0] + "\" shows more than needed");
}

TEST_CASE(Redact_OnlyTheCollidingRowsWiden)
{
    const std::vector<std::string_view> values = {
        "alex.doe@example.com",
        "alex.work@example.com",
        "ganzandereradresse@example.org",
    };
    const std::vector<std::string> shown = RedactAccountHints(values);
    CHECK(shown[0] != shown[1]);
    CHECK_MSG(shown[2] == "gan" + kMask + "@example.org",
              "the third line was revealed too: \"" + shown[2] + "\"");
}

TEST_CASE(Redact_IdenticalValuesAreNotACollision)
{
    const std::vector<std::string_view> values = {
        "gleich@example.de",
        "gleich@example.de",
    };
    const std::vector<std::string> shown = RedactAccountHints(values);
    CHECK(shown[0] == shown[1]);
    CHECK_MSG(shown[0] == "gle" + kMask + "@example.de",
              "revealed needlessly: \"" + shown[0] + "\"");
}

TEST_CASE(Redact_StopsAtTheCapRatherThanRevealingEverything)
{
    const std::vector<std::string_view> values = {
        "sehrlangeradressenname1@example.de",
        "sehrlangeradressenname2@example.de",
    };
    const std::vector<std::string> shown = RedactAccountHints(values);
    for (const std::string& s : shown)
    {
        CHECK_MSG(Contains(s, kMask), "the mask disappeared: \"" + s + "\"");
        CHECK_MSG(!Contains(s, "adressenname1") && !Contains(s, "adressenname2"),
                  "the whole address was revealed: \"" + s + "\"");
    }
}

TEST_CASE(Redact_NeverRevealsMoreThanTheCap)
{
    std::vector<std::string> owned;
    for (int i = 0; i < 40; ++i)
        owned.push_back("immergleicheranfang" + std::to_string(i) + "@example.de");
    const std::vector<std::string_view> values(owned.begin(), owned.end());

    const std::vector<std::string> shown = RedactAccountHints(values);
    CHECK_EQ(shown.size(), values.size());
    for (const std::string& s : shown)
    {
        const std::size_t at = s.find(kMask);
        CHECK_MSG(at != std::string::npos, "no mask in \"" + s + "\"");
        if (at == std::string::npos)
            continue;
        CHECK_MSG(at <= kRedactKeepMax,
                  "\"" + s + "\" shows " + std::to_string(at) + " characters");
    }
}

TEST_CASE(Redact_IsStableAndOrderPreserving)
{
    const std::vector<std::string_view> values = {
        "eins@example.de", "zwei@example.de", "drei@example.de",
    };
    const std::vector<std::string> a = RedactAccountHints(values);
    const std::vector<std::string> b = RedactAccountHints(values);
    CHECK(a == b);
    CHECK_EQ(a.size(), values.size());

    CHECK(RedactAccountHints({}).empty());
    const std::vector<std::string> withEmpty = RedactAccountHints({ "", "", "x@y.de" });
    CHECK_EQ(withEmpty.size(), std::size_t{ 3 });
    CHECK(withEmpty[0].empty());
    CHECK(withEmpty[1].empty());
}

TEST_CASE(Redact_OutputIsAlwaysValidUtf8)
{
    const std::vector<std::string_view> values = {
        "j\xC3\xB6rg@example.de", "j\xC3\xB6rgen@example.de", "j\xC3\xB6rgensen@example.de",
        "\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4@x.de", "\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4@x.de",
    };
    for (const std::string& s : RedactAccountHints(values))
        CHECK_MSG(Utf8IsValid(s), "broken UTF-8: \"" + s + "\"");
}
