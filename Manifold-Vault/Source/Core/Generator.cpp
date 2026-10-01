#include "Core/Generator.h"

#include <cmath>
#include <cstring>

namespace Bunker
{
    namespace
    {
        constexpr const char* kLower = "abcdefghijklmnopqrstuvwxyz";
        constexpr const char* kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr const char* kDigits = "0123456789";
        constexpr const char* kSymbols = "!#$%&()*+,-./:;<=>?@[]^_{|}~";

        constexpr const char* kAmbiguous = "0O1lI";

        bool IsAmbiguous(char c)
        {
            return std::strchr(kAmbiguous, c) != nullptr;
        }

        void AppendClass(std::string& out, const char* set, bool avoidAmbiguous)
        {
            for (const char* p = set; *p; ++p)
                if (!avoidAmbiguous || !IsAmbiguous(*p))
                    out.push_back(*p);
        }

        bool ContainsAnyOf(const std::string& text, const std::string& set)
        {
            for (char c : text)
                if (set.find(c) != std::string::npos)
                    return true;
            return false;
        }
    }

    Result<GeneratedPassword> GeneratePassword(const GeneratorOptions& options,
                                               RandomSource& source)
    {
        std::string classes[4];
        int used = 0;
        if (options.UseLower)   AppendClass(classes[used++], kLower, options.AvoidAmbiguous);
        if (options.UseUpper)   AppendClass(classes[used++], kUpper, options.AvoidAmbiguous);
        if (options.UseDigits)  AppendClass(classes[used++], kDigits, options.AvoidAmbiguous);
        if (options.UseSymbols) AppendClass(classes[used++], kSymbols, options.AvoidAmbiguous);

        if (used == 0)
            return Error{ ErrorCode::InvalidArgument, "no character class enabled" };
        if (options.Length == 0)
            return Error{ ErrorCode::InvalidArgument, "length 0" };

        if (options.RequireEveryClass && options.Length < static_cast<std::size_t>(used))
            return Error{ ErrorCode::InvalidArgument,
                          "length below the number of required character classes" };

        std::string alphabet;
        for (int i = 0; i < used; ++i)
            alphabet += classes[i];
        if (alphabet.empty())
            return Error{ ErrorCode::InvalidArgument, "alphabet empty" };

        GeneratedPassword result;
        result.AlphabetSize = alphabet.size();

        const int kMaxRedraws = 10000;
        for (int attempt = 0; attempt <= kMaxRedraws; ++attempt)
        {
            std::string candidate;
            candidate.reserve(options.Length);
            for (std::size_t i = 0; i < options.Length; ++i)
                candidate.push_back(
                    alphabet[source.Uniform(static_cast<std::uint32_t>(alphabet.size()))]);

            bool ok = true;
            if (options.RequireEveryClass)
                for (int i = 0; i < used && ok; ++i)
                    ok = ContainsAnyOf(candidate, classes[i]);

            if (ok)
            {
                result.Text = std::move(candidate);
                result.Redraws = attempt;
                break;
            }
        }

        if (result.Text.empty())
            return Error{ ErrorCode::InvalidArgument, "constraints cannot be satisfied" };

        const double raw = static_cast<double>(options.Length) *
                           std::log2(static_cast<double>(alphabet.size()));
        double penalty = 0.0;
        if (options.RequireEveryClass && used > 1)
        {
            double accept = 0.0;
            const int subsets = 1 << used;
            for (int mask = 0; mask < subsets; ++mask)
            {
                std::size_t excluded = 0;
                int bits = 0;
                for (int i = 0; i < used; ++i)
                    if (mask & (1 << i)) { excluded += classes[i].size(); ++bits; }
                const double fraction =
                    static_cast<double>(alphabet.size() - excluded) /
                    static_cast<double>(alphabet.size());
                const double term = std::pow(fraction, static_cast<double>(options.Length));
                accept += (bits % 2 == 0) ? term : -term;
            }
            if (accept > 0.0 && accept < 1.0)
                penalty = -std::log2(accept);
        }
        result.EntropyBits = raw - penalty;
        return result;
    }

    namespace
    {
        constexpr const char* kWords[] = {
            "anker","apfel","ast","bach","balken","band","bank","baum","beere","berg",
            "beton","biber","birke","blatt","blei","blitz","boden","bogen","boot","brett",
            "brise","brot","bruch","buch","dach","damm","daumen","deich","distel","docht",
            "dorn","draht","dune","eiche","eimer","eis","elch","engel","ernte","esche",
            "faden","fahne","falke","farn","feder","feld","fels","fenster","feuer","fisch",
            "flachs","flamme","floss","fluss","forst","fracht","frost","fuchs","funke","gabel",
            "garten","gast","geige","gerste","gipfel","glanz","glas","gleis","gold","granit",
            "gras","greif","grund","hafen","hafer","hagel","hain","halde","hammer","hang",
            "harfe","harz","hase","haus","heide","helm","herbst","heu","himmel","hirsch",
            "hof","holz","honig","horn","hugel","hummel","hund","hutte","igel","insel",
            "jacke","jagd","kabel","kachel","kahn","kamm","kanal","kante","kappe","karte",
            "kasten","kegel","kelch","kerze","kette","kiefer","kies","kiste","klang","klee",
            "klinge","knoten","kohle","kolben","komet","korb","korn","kraft","kran","kranz",
            "kreide","kreis","krone","kupfer","lampe","land","lanze","laub","leiter","licht",
            "linde","luchs","luft","magnet","mantel","marder","markt","mast","mauer","meer",
            "messer","metall","mond","moor","moos","motor","muschel","nadel","nagel","nebel",
            "nest","netz","nuss","oase","ofen","otter","palme","panzer","pappel","pfad",
            "pfeil","pferd","pflug","pilz","planke","platte","post","quelle","rabe","rad",
            "rahmen","rand","rast","raupe","regen","reifen","reis","riegel","riff","ring",
            "rinne","ritter","rohr","rose","rost","rubin","ruder","sand","saum","schacht",
            "schaf","schale","schiff","schild","schnee","schuh","see","segel","seil","sense",
            "sichel","silber","span","spatz","speer","spiegel","spule","stab","stahl","stein",
            "stern","stiel","storch","strom","sturm","tanne","tau","teich","teller","tisch",
            "ton","tor","truhe","tunnel","turm","ufer","uhr","urne","vogel","wagen",
            "wald","wand","warte","wasser","weg","weide","welle","werft","wiese","wind",
            "wolke","wurzel","zange","zaun","zeder","zelt","ziegel","zimmer","zinn","zweig",
        };
        constexpr std::size_t kWordCount = sizeof(kWords) / sizeof(kWords[0]);
    }

    std::size_t PassphraseWordCount() { return kWordCount; }

    Result<GeneratedPassword> GeneratePassphrase(std::size_t words, char separator,
                                                 RandomSource& source)
    {
        if (words == 0)
            return Error{ ErrorCode::InvalidArgument, "zero words" };
        if (words > 32)
            return Error{ ErrorCode::InvalidArgument, "absurdly many words" };

        GeneratedPassword result;
        result.AlphabetSize = kWordCount;
        for (std::size_t i = 0; i < words; ++i)
        {
            if (i > 0)
                result.Text.push_back(separator);
            result.Text += kWords[source.Uniform(static_cast<std::uint32_t>(kWordCount))];
        }
        result.EntropyBits = static_cast<double>(words) *
                             std::log2(static_cast<double>(kWordCount));
        return result;
    }
}
