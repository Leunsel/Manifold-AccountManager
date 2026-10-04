#include "TestRunner.h"

#include "Core/LinkSafety.h"

#include <string>

using namespace Bunker;

TEST_CASE(Link_OpensOnlyTheTwoSchemesItPromises)
{
    CHECK(IsOpenableWebLink("https://example.de"));
    CHECK(IsOpenableWebLink("http://example.de/pfad?a=b#c"));
    CHECK(IsOpenableWebLink("HTTPS://EXAMPLE.DE"));
    CHECK(IsOpenableWebLink("HtTp://example.de"));
    CHECK(IsOpenableWebLink("  https://example.de  "));
    CHECK(LinkToOpen("  https://example.de  ") == "https://example.de");
}

TEST_CASE(Link_FlagsOnlyUnencryptedPublicAddresses)
{
    const char* flagged[] = {
        "http://example.de",
        "HTTP://Example.de/login?next=/",
        "  http://shop.example.com  ",
        "http://nutzer:geheim@shop.example.com/",
        "http://example.de:8080/admin",
        "http://8.8.8.8/",
        "http://172.32.0.1",
        "http://www.example.local.de/",
    };
    for (const char* link : flagged)
        CHECK_MSG(IsUnencryptedWebLink(link), std::string("not flagged: ") + link);

    const char* fine[] = {
        "https://example.de",
        "example.de",
        "ftp://example.de",
        "http://",
        "http://localhost:3000",
        "http://127.0.0.1/",
        "http://10.0.0.5:5000/x",
        "http://172.16.0.1",
        "http://192.168.178.1",
        "http://169.254.1.1",
        "http://fritz.box",
        "http://FRITZ.BOX/",
        "http://speedport.ip",
        "http://nas",
        "http://drucker.local/",
        "http://router.home.arpa",
        "http://example.lan.",
        "http://[::1]:8080/",
        "http://[fd00::1]/",
    };
    for (const char* link : fine)
        CHECK_MSG(!IsUnencryptedWebLink(link), std::string("flagged: ") + link);
}

TEST_CASE(Link_RefusesEverythingElse)
{
    const char* refused[] = {
        "file:///C:/Windows/System32",
        "\\\\server\\freigabe\\datei.exe",
        "C:\\Windows\\System32\\cmd.exe",
        "ms-settings:privacy",
        "javascript:alert(1)",
        "data:text/html,<script>alert(1)</script>",
        "vbscript:msgbox",
        "shell:startup",
        "search-ms:query=passwort",
        "ftp://example.de",
        "mailto:jemand@example.de",
        "example.de",
        "www.example.de",
        "//example.de",
        "http:/example.de",
        "https:example.de",
        "http://",
        "https://",
        "",
        "   ",
    };
    for (const char* text : refused)
    {
        CHECK_MSG(!IsOpenableWebLink(text), text);
        CHECK_MSG(LinkToOpen(text).empty(), text);
    }
}

TEST_CASE(Link_RefusesControlCharacters)
{
    CHECK(!IsOpenableWebLink("https://example.de\nhttps://woother.de"));
    CHECK(!IsOpenableWebLink("https://example.de\r\nirgendwas"));
    CHECK(!IsOpenableWebLink(std::string("https://example.de\tmehr")));
    CHECK(!IsOpenableWebLink(std::string("https://beispiel") + '\0' + "de"));
    CHECK(!IsOpenableWebLink(std::string("https://beispiel\x7F.de")));
}

TEST_CASE(Link_RefusesSomethingAbsurdlyLong)
{
    const std::string longEnough = "https://example.de/" + std::string(2000, 'a');
    CHECK(IsOpenableWebLink(longEnough));

    const std::string tooLong = "https://example.de/" + std::string(kMaxLinkBytes, 'a');
    CHECK(!IsOpenableWebLink(tooLong));
}

TEST_CASE(Link_WhatIsOpenedIsWhatWasChecked)
{
    CHECK(LinkToOpen("file:///C:/").empty());
    CHECK(LinkToOpen("https://example.de") == "https://example.de");
}
