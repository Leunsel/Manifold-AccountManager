#include "BunkerFonts.h"

#include "Resource.h"
#include "Generated/FaIcons.h"
#include "Generated/FaBrands.h"

#include <windows.h>
#include <string.h>

namespace Bunker::Fonts
{
    namespace
    {
        Set g_set;
        Set g_monoSet;
        bool g_useMono = false;

        constexpr ImWchar kTextExcludePua[] = {
            0xE000, 0xF8FF,
            0,
        };

        constexpr ImWchar kIconExcludeText[] = {
            0x0001, 0xDFFF,
            0,
        };

        struct Blob { const void* data = nullptr; int size = 0; };

        Blob LoadBlob(int id)
        {
            Blob b;
            HMODULE mod = ::GetModuleHandleW(nullptr);
            HRSRC res = ::FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA);
            if (!res)
                return b;
            HGLOBAL handle = ::LoadResource(mod, res);
            if (!handle)
                return b;
            b.data = ::LockResource(handle);
            b.size = static_cast<int>(::SizeofResource(mod, res));
            if (!b.data || b.size <= 0)
                b = Blob{};
            return b;
        }

        ImFontConfig BaseConfig(const char* name)
        {
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            ::strncpy_s(cfg.Name, name, _TRUNCATE);
            return cfg;
        }

        bool MergeSolidIcons(ImFontAtlas* atlas, const Blob& solid)
        {
            ImFontConfig cfg = BaseConfig("FA Solid");
            cfg.MergeMode = true;
            cfg.GlyphExcludeRanges = kIconExcludeText;
            cfg.GlyphMinAdvanceX = Sizes::Body;
            cfg.GlyphOffset = ImVec2(0.0f, 1.0f);
            return atlas->AddFontFromMemoryTTF(const_cast<void*>(solid.data), solid.size,
                                               Sizes::Body, &cfg) != nullptr;
        }
    }

    const Set& Current() { return (g_useMono && g_monoSet.Valid()) ? g_monoSet : g_set; }

    void UseMonospace(bool monospace)
    {
        g_useMono = monospace;
        if (Current().Text)
            ImGui::GetIO().FontDefault = Current().Text;
    }

    bool Build(Set& out, const char** error)
    {
        out = Set{};
        const char* dummy = nullptr;
        if (!error)
            error = &dummy;
        *error = nullptr;

        const Blob interRegular = LoadBlob(IDR_FONT_INTER_REGULAR);
        const Blob interMedium = LoadBlob(IDR_FONT_INTER_MEDIUM);
        const Blob interSemi = LoadBlob(IDR_FONT_INTER_SEMIBOLD);
        const Blob mono = LoadBlob(IDR_FONT_MONO_REGULAR);
        const Blob monoMedium = LoadBlob(IDR_FONT_MONO_MEDIUM);
        const Blob faSolid = LoadBlob(IDR_FONT_FA_SOLID);
        const Blob faBrands = LoadBlob(IDR_FONT_FA_BRANDS);

        if (!interRegular.data || !interMedium.data || !interSemi.data ||
            !mono.data || !monoMedium.data || !faSolid.data || !faBrands.data)
        {
            *error = "Embedded fonts are missing. Was App.rc compiled in?";
            return false;
        }

        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        atlas->Clear();

        auto addText = [&](const Blob& blob, const char* name) -> ImFont* {
            ImFontConfig cfg = BaseConfig(name);
            cfg.GlyphExcludeRanges = kTextExcludePua;
            ImFont* f = atlas->AddFontFromMemoryTTF(const_cast<void*>(blob.data), blob.size,
                                                    Sizes::Body, &cfg);
            if (f && !MergeSolidIcons(atlas, faSolid))
                return nullptr;
            return f;
        };

        out.Text = addText(interRegular, "Inter Regular");
        out.Medium = addText(interMedium, "Inter Medium");
        out.Strong = addText(interSemi, "Inter SemiBold");

        {
            ImFontConfig cfg = BaseConfig("JetBrains Mono");
            cfg.GlyphExcludeRanges = kTextExcludePua;
            out.Mono = atlas->AddFontFromMemoryTTF(const_cast<void*>(mono.data), mono.size,
                                                   Sizes::Body, &cfg);
        }
        {
            ImFontConfig cfg = BaseConfig("FA Brands");
            out.Brands = atlas->AddFontFromMemoryTTF(const_cast<void*>(faBrands.data), faBrands.size,
                                                     Sizes::IconRow, &cfg);
        }

        Set monoSet;
        monoSet.Text = addText(mono, "JetBrains Mono UI");
        monoSet.Medium = addText(monoMedium, "JetBrains Mono Medium UI");
        monoSet.Strong = monoSet.Medium;
        monoSet.Mono = out.Mono;
        monoSet.Brands = out.Brands;

        if (!out.Valid() || !monoSet.Valid())
        {
            *error = "Could not build the font atlas.";
            out = Set{};
            return false;
        }

        ImGui::GetIO().FontDefault = out.Text;
        g_set = out;
        g_monoSet = monoSet;
        g_useMono = false;
        return true;
    }
}
