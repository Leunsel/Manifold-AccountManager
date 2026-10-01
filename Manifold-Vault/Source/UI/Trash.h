#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bunker::Vault
{
    struct Document;
}

namespace Bunker::UI::Trash
{
    constexpr double kUndoSeconds = 10.0;

    std::size_t Delete(Vault::Document& doc, const std::vector<std::uint64_t>& ids, double now);

    bool Pending(double now);
    std::size_t Count();
    const std::string& FirstTitle();

    std::vector<std::uint64_t> Restore(Vault::Document& doc);

    void Expire(double now);
    void Forget();
}
