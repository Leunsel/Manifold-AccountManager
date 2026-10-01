#include "UI/Trash.h"

#include "Core/Model.h"
#include "Core/Mutate.h"

namespace Bunker::UI::Trash
{
    namespace
    {
        struct Held
        {
            Vault::Entry Entry;
            std::size_t Index = 0;
        };

        std::vector<Held> g_held;
        std::string g_firstTitle;
        double g_until = 0.0;
    }

    std::size_t Delete(Vault::Document& doc, const std::vector<std::uint64_t>& ids, double now)
    {
        Forget();
        for (std::uint64_t id : ids)
        {
            Held held;
            if (!Vault::TakeEntry(doc, id, held.Entry, held.Index).IsOk())
                continue;
            if (g_held.empty())
                g_firstTitle = held.Entry.Title;
            g_held.push_back(std::move(held));
        }
        g_until = now + kUndoSeconds;
        return g_held.size();
    }

    bool Pending(double now)
    {
        return !g_held.empty() && now < g_until;
    }

    std::size_t Count()
    {
        return g_held.size();
    }

    const std::string& FirstTitle()
    {
        return g_firstTitle;
    }

    std::vector<std::uint64_t> Restore(Vault::Document& doc)
    {
        std::vector<std::uint64_t> back;
        for (auto it = g_held.rbegin(); it != g_held.rend(); ++it)
        {
            const std::uint64_t id = it->Entry.Id;
            if (Vault::RestoreEntry(doc, std::move(it->Entry), it->Index).IsOk())
                back.push_back(id);
        }
        Forget();
        return back;
    }

    void Expire(double now)
    {
        if (!g_held.empty() && now >= g_until)
            Forget();
    }

    void Forget()
    {
        g_held.clear();
        g_held.shrink_to_fit();
        g_firstTitle.clear();
        g_until = 0.0;
    }
}
