#include "TestRunner.h"

#include <algorithm>

namespace Test
{
    std::vector<Case>& Registry()
    {
        static std::vector<Case> registry;
        return registry;
    }

    void Fail(const char* file, int line, const char* expr, const std::string& extra)
    {
        std::string msg = std::string(file) + ":" + std::to_string(line) + "  " + expr;
        if (!extra.empty())
            msg += "\n        " + extra;
        throw Failure{ msg };
    }

    int RunAll(const char* filter)
    {
        auto& cases = Registry();
        std::sort(cases.begin(), cases.end(),
                  [](const Case& a, const Case& b) { return std::strcmp(a.Name, b.Name) < 0; });

        int passed = 0, failed = 0, skipped = 0;
        for (const Case& c : cases)
        {
            if (filter && *filter && !std::strstr(c.Name, filter))
            {
                ++skipped;
                continue;
            }
            try
            {
                c.Fn();
                std::printf("  [  ok  ] %s\n", c.Name);
                ++passed;
            }
            catch (const Failure& f)
            {
                std::printf("  [ FAIL ] %s\n        %s\n", c.Name, f.Message.c_str());
                ++failed;
            }
            catch (const std::exception& e)
            {
                std::printf("  [ FAIL ] %s\n        unexpected exception: %s\n", c.Name, e.what());
                ++failed;
            }
            catch (...)
            {
                std::printf("  [ FAIL ] %s\n        unexpected, unknown exception\n", c.Name);
                ++failed;
            }
        }

        std::printf("\n  %d passed, %d failed", passed, failed);
        if (skipped)
            std::printf(", %d skipped", skipped);
        std::printf("\n");

        if (passed == 0 && failed == 0)
        {
            std::printf("  ERROR: not a single test case ran");
            if (filter && *filter)
                std::printf(" (filter '%s' matches nothing)", filter);
            std::printf("\n");
            return 2;
        }
        return failed == 0 ? 0 : 1;
    }
}
