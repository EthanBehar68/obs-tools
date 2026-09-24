#include <doctest.h>

#include "core/chat-format.hpp"
#include "core/name-color.hpp"

#include <cstdio>

using namespace unified_chat;

namespace {

constexpr const char *kYami = "#3c404d"; // Yami's read-only QTextEdit background (--input_bg)

struct Channels {
	int r, g, b;
};

Channels Split(const std::string &hex)
{
	unsigned value = 0;
	std::sscanf(hex.c_str() + 1, "%x", &value);
	return {(int)(value >> 16) & 0xFF, (int)(value >> 8) & 0xFF, (int)value & 0xFF};
}

} // namespace

TEST_CASE("ContrastRatio matches WCAG reference values")
{
	CHECK(ContrastRatio("#000000", "#ffffff") == doctest::Approx(21.0));
	CHECK(ContrastRatio("#ffffff", "#ffffff") == doctest::Approx(1.0));
	CHECK(ContrastRatio("#777777", "#ffffff") == doctest::Approx(4.48).epsilon(0.01));
	CHECK(ContrastRatio("red", "#ffffff") == 0.0);
}

TEST_CASE("Readable colors are left exactly as picked")
{
	NameColorResolver colors;
	colors.SetBackground(kYami);
	CHECK(colors.Resolve("#ffd600") == "#ffd600");
	CHECK(colors.Resolve("#ffffff") == "#ffffff");
	for (Platform platform : {Platform::Twitch, Platform::YouTube}) {
		std::string brand(DefaultNameColor(platform));
		CHECK(colors.Resolve(brand) == brand);
	}
}

TEST_CASE("Dark colors are lightened just enough and keep their hue")
{
	NameColorResolver colors;
	colors.SetBackground(kYami);
	for (const char *picked : {"#8b0000", "#b22222", "#0000ff", "#8a2be2", "#2e8b57"}) {
		CAPTURE(picked);
		std::string shown = colors.Resolve(picked);
		double ratio = ContrastRatio(shown, kYami);
		CHECK(ratio >= kMinNameContrast);
		CHECK(ratio < kMinNameContrast + 0.25); // smallest change, not a jump to pastel
	}

	auto red = Split(colors.Resolve("#8b0000"));
	CHECK(red.r > red.g);
	CHECK(red.g == red.b);
	auto blue = Split(colors.Resolve("#0000ff"));
	CHECK(blue.b > blue.r);
	CHECK(blue.r == blue.g);
}

TEST_CASE("Light backgrounds darken colors instead")
{
	NameColorResolver colors;
	colors.SetBackground("#ffffff");
	std::string gold = colors.Resolve("#ffd600");
	CHECK(ContrastRatio(gold, "#ffffff") >= kMinNameContrast);
	CHECK(ContrastRatio(gold, "#000000") < ContrastRatio("#ffd600", "#000000"));
	CHECK(colors.Resolve("#8b0000") == "#8b0000");
}

TEST_CASE("Mid-grey backgrounds still reach the threshold")
{
	// Neither white nor black is far from #777777, but one of them always reaches 4.5.
	NameColorResolver colors;
	colors.SetBackground("#777777");
	for (const char *picked : {"#808080", "#ff0000", "#00ff00", "#0000ff"}) {
		CAPTURE(picked);
		CHECK(ContrastRatio(colors.Resolve(picked), "#777777") >= kMinNameContrast);
	}
}

TEST_CASE("Resolved colors are cached per background")
{
	NameColorResolver colors;
	colors.SetBackground(kYami);
	std::string first = colors.Resolve("#8b0000");
	CHECK(colors.Resolve("#8b0000") == first);
	CHECK(colors.CacheSize() == 1);

	colors.SetBackground("#3C404D"); // same color, different spelling: cache kept
	CHECK(colors.CacheSize() == 1);

	colors.SetBackground("#ffffff");
	CHECK(colors.CacheSize() == 0);
	CHECK(colors.Resolve("#8b0000") == "#8b0000");
}

TEST_CASE("The cache is bounded")
{
	NameColorResolver colors;
	colors.SetBackground(kYami);
	char hex[8];
	for (unsigned i = 0; i <= NameColorResolver::kMaxEntries; ++i) {
		std::snprintf(hex, sizeof(hex), "#%06x", i);
		colors.Resolve(hex);
	}
	CHECK(colors.CacheSize() <= NameColorResolver::kMaxEntries);
	CHECK(colors.CacheSize() >= 1);
}

TEST_CASE("Invalid input passes through without adjustment or caching")
{
	NameColorResolver colors;
	CHECK(colors.Resolve("#8b0000") == "#8b0000"); // no background yet

	colors.SetBackground("not a color");
	CHECK(colors.Resolve("#8b0000") == "#8b0000");

	colors.SetBackground(kYami);
	CHECK(colors.Resolve("red") == "red");
	CHECK(colors.CacheSize() == 0);
}

TEST_CASE("FormatMessageHtml uses the resolver for chatter and default colors")
{
	NameColorResolver colors;
	colors.SetBackground(kYami);

	ChatMessage picked{Platform::Twitch, "1", "dezad", "hello", "#8B0000"};
	auto html = FormatMessageHtml(picked, 16, {Platform::Twitch}, &colors);
	CHECK(html.find("color: " + colors.Resolve("#8b0000")) != std::string::npos);
	CHECK(html.find("#8b0000") == std::string::npos);

	ChatMessage unpicked{Platform::Twitch, "2", "viewer", "hi", ""};
	html = FormatMessageHtml(unpicked, 16, {Platform::Twitch}, &colors);
	CHECK(html.find("color: " + colors.Resolve(std::string(DefaultNameColor(Platform::Twitch)))) !=
	      std::string::npos);
}
