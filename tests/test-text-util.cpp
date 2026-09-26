#include <doctest.h>

#include "core/text-util.hpp"

using namespace unified_chat;

TEST_CASE("Trim removes surrounding whitespace only")
{
	CHECK(Trim("  hello world \r\n") == "hello world");
	CHECK(Trim("") == "");
	CHECK(Trim(" \t ") == "");
}

TEST_CASE("Utf8Length counts code points")
{
	CHECK(Utf8Length("abc") == 3);
	CHECK(Utf8Length("h\xC3\xA9llo") == 5);             // é
	CHECK(Utf8Length("\xF0\x9F\x98\x80") == 1);         // 😀
	CHECK(Utf8Length("\xE6\x97\xA5\xE6\x9C\xAC") == 2); // 日本
}

TEST_CASE("StripLineBreaks prevents protocol line injection")
{
	CHECK(StripLineBreaks("hi\r\nPRIVMSG #other :spam") == "hi  PRIVMSG #other :spam");
	CHECK(StripLineBreaks(std::string("a\0b", 3)) == "ab");
}

TEST_CASE("HtmlEscape escapes markup characters")
{
	CHECK(HtmlEscape("<img src=x onerror='a'>&\"") == "&lt;img src=x onerror=&#39;a&#39;&gt;&amp;&quot;");
	CHECK(HtmlEscape("plain") == "plain");
}

TEST_CASE("SanitizeColor accepts only #rrggbb")
{
	CHECK(SanitizeColor("#FF00aa") == "#ff00aa");
	CHECK(SanitizeColor(" #123456 ") == "#123456");
	CHECK(SanitizeColor("") == "");
	CHECK(SanitizeColor("red") == "");
	CHECK(SanitizeColor("#12345") == "");
	CHECK(SanitizeColor("#12345g") == "");
	CHECK(SanitizeColor("#123456;background:url(x)") == "");
}

TEST_CASE("ParseRfc3339 converts YouTube timestamps to unix seconds")
{
	CHECK(ParseRfc3339("1970-01-01T00:00:00Z") == 0);
	CHECK(ParseRfc3339("2026-09-26T02:13:45.123456+00:00") == 1790388825);
	CHECK(ParseRfc3339("2026-09-26T02:13:45Z") == 1790388825);
	CHECK(ParseRfc3339("2026-09-25T19:13:45-07:00") == 1790388825);
	CHECK(ParseRfc3339("2024-02-29T12:00:00Z") == 1709208000); // leap day
	CHECK(ParseRfc3339("") == 0);
	CHECK(ParseRfc3339("2026-09-26") == 0);
	CHECK(ParseRfc3339("2026-13-01T00:00:00Z") == 0);
	CHECK(ParseRfc3339("2026-09-26T02:13:45") == 0); // no zone
}

TEST_CASE("FormatDuration reads naturally for timeout notices")
{
	CHECK(FormatDuration(1) == "1 second");
	CHECK(FormatDuration(30) == "30 seconds");
	CHECK(FormatDuration(60) == "1 minute");
	CHECK(FormatDuration(600) == "10 minutes");
	CHECK(FormatDuration(3600) == "1 hour");
	CHECK(FormatDuration(5400) == "1 hour 30 minutes");
	CHECK(FormatDuration(86400) == "1 day");
	CHECK(FormatDuration(1209600) == "14 days");
	CHECK(FormatDuration(-5) == "0 seconds");
}

TEST_CASE("UrlEncode and FormEncode")
{
	CHECK(UrlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
	CHECK(UrlEncode("safe-_.~") == "safe-_.~");
	CHECK(FormEncode({{"client_id", "abc"}, {"scope", "chat:read chat:edit"}}) ==
	      "client_id=abc&scope=chat%3Aread%20chat%3Aedit");
	CHECK(FormEncode({}) == "");
}
