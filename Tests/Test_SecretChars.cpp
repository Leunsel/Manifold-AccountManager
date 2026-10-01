#include "TestRunner.h"

#include "Core/SecretChars.h"

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

using namespace Bunker;

namespace
{
    constexpr std::size_t kSlack = 64;
    constexpr unsigned char kGuard = 0xCC;

    struct Field
    {
        explicit Field(std::size_t capacity)
            : Capacity(capacity), Bytes(capacity + kSlack, kGuard)
        {
        }

        std::string Text() const
        {
            return std::string(reinterpret_cast<const char*>(Bytes.data()), Length);
        }

        bool GuardIntact() const
        {
            for (std::size_t i = Capacity; i < Bytes.size(); ++i)
                if (Bytes[i] != kGuard)
                    return false;
            return true;
        }

        std::size_t Capacity;
        std::size_t Length = 0;
        std::vector<unsigned char> Bytes;
    };

    std::string Paste(const wchar_t* text, std::size_t units, std::size_t capacity = 512)
    {
        Field f(capacity);
        AppendUtf16(f.Bytes.data(), f.Capacity, f.Length, text, units);
        CHECK(f.GuardIntact());
        if (f.Length > 0)
            CHECK_EQ(f.Bytes[f.Length], 0);
        return f.Text();
    }

    std::string Paste(const std::wstring& text)
    {
        return Paste(text.c_str(), text.size() + 1);
    }

    void OldAppend(unsigned char* buf, std::size_t cap, std::size_t& len, unsigned int cp)
    {
        unsigned char enc[4];
        std::size_t n = 0;
        if (cp < 0x80) { enc[0] = static_cast<unsigned char>(cp); n = 1; }
        else if (cp < 0x800)
        {
            enc[0] = static_cast<unsigned char>(0xC0 | (cp >> 6));
            enc[1] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
            n = 2;
        }
        else if (cp < 0x10000)
        {
            enc[0] = static_cast<unsigned char>(0xE0 | (cp >> 12));
            enc[1] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
            enc[2] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
            n = 3;
        }
        else
        {
            enc[0] = static_cast<unsigned char>(0xF0 | (cp >> 18));
            enc[1] = static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F));
            enc[2] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
            enc[3] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
            n = 4;
        }

        if (len + n + 1 > cap)
            return;
        for (std::size_t i = 0; i < n; ++i)
            buf[len + i] = enc[i];
        len += n;
        buf[len] = 0;
    }

    void OldPaste(unsigned char* buf, std::size_t cap, std::size_t& len, const wchar_t* text)
    {
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0)
            return;
        std::string utf8(static_cast<std::size_t>(n), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), n, nullptr, nullptr);
        const char* clip = utf8.c_str();

        for (const char* q = clip; *q; )
        {
            const unsigned char lead = static_cast<unsigned char>(*q);
            unsigned int cp = lead;
            int extra = 0;
            if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1Fu; extra = 1; }
            else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0Fu; extra = 2; }
            else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07u; extra = 3; }
            ++q;
            for (int k = 0; k < extra && *q; ++k, ++q)
                cp = (cp << 6) | (static_cast<unsigned char>(*q) & 0x3Fu);
            if (cp >= 32 && cp != 127)
                OldAppend(buf, cap, len, cp);
        }
    }

    struct Rng
    {
        std::uint64_t State = 0x9E3779B97F4A7C15ull;
        std::uint32_t Below(std::uint32_t n)
        {
            State ^= State << 13;
            State ^= State >> 7;
            State ^= State << 17;
            return static_cast<std::uint32_t>(State >> 32) % n;
        }
    };

    wchar_t Unit(std::uint32_t v) { return static_cast<wchar_t>(v); }

    std::vector<wchar_t> RandomText(Rng& r)
    {
        std::vector<wchar_t> t;
        const std::uint32_t n = r.Below(48);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            switch (r.Below(11))
            {
            case 0: t.push_back(Unit(1 + r.Below(31))); break;
            case 1: t.push_back(Unit(0x7F + r.Below(0x21))); break;
            case 2: t.push_back(Unit(0xA0 + r.Below(0x760))); break;
            case 3: t.push_back(Unit(0x800 + r.Below(0xD000))); break;
            case 4: t.push_back(Unit(0xD800 + r.Below(0x400))); break;
            case 5: t.push_back(Unit(0xDC00 + r.Below(0x400))); break;
            case 6: t.push_back(Unit(0xE000 + r.Below(0x2000))); break;
            case 7:
                t.push_back(Unit(0xD800 + r.Below(0x400)));
                t.push_back(Unit(0xDC00 + r.Below(0x400)));
                break;
            case 8: t.push_back(r.Below(16) == 0 ? L'\0' : L'~'); break;
            default: t.push_back(Unit(0x20 + r.Below(0x5F))); break;
            }
        }
        t.push_back(L'\0');
        return t;
    }
}

TEST_CASE(SecretChars_FilterIsTheOldRule)
{
    for (unsigned int cp = 0; cp <= 0x10FFFF; ++cp)
        if (IsSecretFieldChar(cp) != (cp >= 32 && cp != 127))
            CHECK_MSG(false, "differs at U+" + std::to_string(cp));
}

TEST_CASE(SecretChars_Utf8AtEveryLengthBoundary)
{
    struct Case { unsigned int Cp; std::string Bytes; };
    const Case cases[] = {
        { 0x20,     "\x20" },
        { 0x7F,     "\x7F" },
        { 0x80,     "\xC2\x80" },
        { 0xE4,     "\xC3\xA4" },
        { 0x7FF,    "\xDF\xBF" },
        { 0x800,    "\xE0\xA0\x80" },
        { 0xFFFD,   "\xEF\xBF\xBD" },
        { 0xFFFF,   "\xEF\xBF\xBF" },
        { 0x10000,  "\xF0\x90\x80\x80" },
        { 0x1F600,  "\xF0\x9F\x98\x80" },
        { 0x10FFFF, "\xF4\x8F\xBF\xBF" },
    };
    for (const Case& c : cases)
    {
        Field f(16);
        CHECK(AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, c.Cp));
        CHECK(f.Text() == c.Bytes);
        CHECK_EQ(f.Bytes[f.Length], 0);
        CHECK(f.GuardIntact());
    }
}

TEST_CASE(SecretChars_AppendIsAllOrNothing)
{
    Field f(5);
    CHECK(AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 'a'));
    CHECK(AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 'b'));
    CHECK(AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 'c'));
    CHECK_EQ(f.Length, std::size_t{ 3 });

    CHECK(!AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 0xE4));
    CHECK_EQ(f.Length, std::size_t{ 3 });
    CHECK_EQ(f.Bytes[3], 0);
    CHECK_EQ(f.Bytes[4], kGuard);

    CHECK(AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 'd'));
    CHECK(f.Text() == "abcd");
    CHECK_EQ(f.Bytes[4], 0);

    CHECK(!AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 'e'));
    CHECK(f.Text() == "abcd");
    CHECK(f.GuardIntact());
}

TEST_CASE(SecretChars_AppendRefusesWithoutABuffer)
{
    std::size_t length = 0;
    CHECK(!AppendUtf8(nullptr, 16, length, 'a'));
    CHECK_EQ(length, std::size_t{ 0 });
    AppendUtf16(nullptr, 16, length, L"abc", 4);
    CHECK_EQ(length, std::size_t{ 0 });
}

TEST_CASE(SecretChars_TypingMatchesTheOldAppend)
{
    for (unsigned int cp = 0; cp <= 0xFFFF; ++cp)
    {
        for (std::size_t room = 0; room <= 5; ++room)
        {
            Field before(8), after(8);
            before.Length = after.Length = 8 - room;
            OldAppend(before.Bytes.data(), before.Capacity, before.Length, cp);
            AppendUtf8(after.Bytes.data(), after.Capacity, after.Length, cp);
            if (before.Length != after.Length || before.Bytes != after.Bytes)
            {
                CHECK_MSG(false, "differs at U+" + std::to_string(cp) +
                                     ", room " + std::to_string(room));
                return;
            }
        }
    }
}

TEST_CASE(SecretChars_PasteDropsControlCharacters)
{
    CHECK(Paste(L"otpauth://totp/X?secret=JBSWY3DPEHPK3PXP\r\n") ==
          "otpauth://totp/X?secret=JBSWY3DPEHPK3PXP");
    CHECK(Paste(L"a\tb\x7F" L"c\x1B" L"d") == "abcd");
    CHECK(Paste(L"a\x85" L"b") == "a\xC2\x85" "b");
}

TEST_CASE(SecretChars_PasteKeepsUnicode)
{
    CHECK(Paste(L"Gr\x00FC\x00DF" L"e") == "Gr\xC3\xBC\xC3\x9F" "e");
    CHECK(Paste(L"\x20AC") == "\xE2\x82\xAC");
    CHECK(Paste(L"\xD83D\xDE00") == "\xF0\x9F\x98\x80");
}

TEST_CASE(SecretChars_PasteReplacesLoneSurrogates)
{
    CHECK(Paste(L"\xD800" L"A") == "\xEF\xBF\xBD" "A");
    CHECK(Paste(L"A\xDC00") == "A\xEF\xBF\xBD");
    CHECK(Paste(L"\xD800\xD800\xDC00") == "\xEF\xBF\xBD\xF0\x90\x80\x80");
    CHECK(Paste(L"\xDC00\xD800") == "\xEF\xBF\xBD\xEF\xBF\xBD");
}

TEST_CASE(SecretChars_PasteStopsAtNulAndAtTheBlockEnd)
{
    const wchar_t embedded[] = { L'a', L'b', L'\0', L'c', L'\0' };
    CHECK(Paste(embedded, 5) == "ab");

    const wchar_t unterminated[] = { L'a', L'b', L'c', L'X', L'X', L'\0' };
    CHECK(Paste(unterminated, 3) == "abc");
    CHECK(Paste(unterminated, 0).empty());

    const wchar_t split[] = { L'a', Unit(0xD83D), Unit(0xDE00), L'\0' };
    CHECK(Paste(split, 2) == "a\xEF\xBF\xBD");
    CHECK(Paste(split, 3) == "a\xF0\x9F\x98\x80");

    CHECK(Paste(nullptr, 8).empty());
}

TEST_CASE(SecretChars_PasteAppendsAndStopsAtCapacity)
{
    Field f(8);
    AppendUtf8(f.Bytes.data(), f.Capacity, f.Length, 'x');
    AppendUtf16(f.Bytes.data(), f.Capacity, f.Length, L"yz", 3);
    CHECK(f.Text() == "xyz");

    AppendUtf16(f.Bytes.data(), f.Capacity, f.Length, L"ab\x20AC" L"cde", 7);
    CHECK(f.Text() == "xyzabcd");
    CHECK_EQ(f.Length, std::size_t{ 7 });
    CHECK_EQ(f.Bytes[7], 0);
    CHECK(f.GuardIntact());
}

TEST_CASE(SecretChars_PasteMatchesTheOldPath)
{
    Rng rng;
    std::size_t truncated = 0, withSurrogates = 0;
    for (int round = 0; round < 20000; ++round)
    {
        const std::vector<wchar_t> text = RandomText(rng);
        const std::size_t capacity = rng.Below(8) == 0 ? 512 : 1 + rng.Below(64);

        Field before(capacity), after(capacity);
        const std::uint32_t prefix = rng.Below(6);
        for (std::uint32_t i = 0; i < prefix; ++i)
        {
            OldAppend(before.Bytes.data(), before.Capacity, before.Length, 'p');
            AppendUtf8(after.Bytes.data(), after.Capacity, after.Length, 'p');
        }

        OldPaste(before.Bytes.data(), before.Capacity, before.Length, text.data());
        AppendUtf16(after.Bytes.data(), after.Capacity, after.Length, text.data(), text.size());

        if (before.Length != after.Length || before.Bytes != after.Bytes)
        {
            CHECK_MSG(false, "round " + std::to_string(round) + ": old " +
                                 std::to_string(before.Length) + " bytes, new " +
                                 std::to_string(after.Length));
            return;
        }
        CHECK(after.GuardIntact());

        if (after.Length + 4 >= capacity)
            ++truncated;
        for (wchar_t w : text)
            if (w >= 0xD800 && w <= 0xDFFF)
            {
                ++withSurrogates;
                break;
            }
    }
    CHECK(truncated > 1000);
    CHECK(withSurrogates > 1000);
}
