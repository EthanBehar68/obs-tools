#include <doctest.h>

#include "core/chat-config.hpp"
#include "core/stream-info.hpp"

#include <algorithm>
#include <json.hpp>

using namespace unified_chat;
using json = nlohmann::json;

TEST_CASE("Twitch Get Channel Information is parsed")
{
	// Twitch's reference example, with tags.
	const auto info =
		ParseTwitchChannelInfo(R"({"data":[{"broadcaster_id":"141981764","broadcaster_login":"twitchdev",
		"broadcaster_name":"TwitchDev","broadcaster_language":"en","game_id":"509670",
		"game_name":"Science & Technology","title":"TwitchDev Monthly Update // May 6, 2021","delay":0,
		"tags":["DevsInTheKnow"],"content_classification_labels":[],"is_branded_content":false}]})");
	REQUIRE(info);
	CHECK(info->title == "TwitchDev Monthly Update // May 6, 2021");
	CHECK(info->category.id == "509670");
	CHECK(info->category.name == "Science & Technology");
	CHECK(info->tags == std::vector<std::string>{"DevsInTheKnow"});

	CHECK_FALSE(ParseTwitchChannelInfo(R"({"data":[]})"));
	CHECK_FALSE(ParseTwitchChannelInfo("not json"));
	CHECK(TwitchChannelUrl("141981764") == "https://api.twitch.tv/helix/channels?broadcaster_id=141981764");
}

TEST_CASE("Twitch category search")
{
	CHECK(TwitchCategorySearchUrl(" elden ring ") ==
	      "https://api.twitch.tv/helix/search/categories?first=10&query=elden%20ring");
	const auto categories = ParseTwitchCategories(R"({"data":[
		{"id":"33214","name":"Fortnite","box_art_url":"https://static-cdn.jtvnw.net/ttv-boxart/33214-52x72.jpg"},
		{"id":"","name":"broken"},
		{"id":"512710","name":"Call of Duty: Warzone","box_art_url":""}],"pagination":{"cursor":"x"}})");
	REQUIRE(categories.size() == 2);
	CHECK(categories[0].name == "Fortnite");
	CHECK(categories[1].id == "512710");
	CHECK(ParseTwitchCategories("{}").empty());
}

TEST_CASE("Box art at the size shown")
{
	TwitchCategory search{"33214", "Fortnite", "https://static-cdn.jtvnw.net/ttv-boxart/33214-52x72.jpg"};
	CHECK(BoxArtUrl(search, 104, 144) == "https://static-cdn.jtvnw.net/ttv-boxart/33214-104x144.jpg");
	TwitchCategory games{"33214", "Fortnite", "https://static-cdn.jtvnw.net/ttv-boxart/33214-{width}x{height}.jpg"};
	CHECK(BoxArtUrl(games, 26, 36) == "https://static-cdn.jtvnw.net/ttv-boxart/33214-26x36.jpg");
	TwitchCategory channel{"509658", "Just Chatting", ""};
	CHECK(BoxArtUrl(channel, 26, 36) == "https://static-cdn.jtvnw.net/ttv-boxart/509658-26x36.jpg");
	CHECK(BoxArtUrl({}, 26, 36).empty());
	// Not a size: left alone.
	TwitchCategory odd{"1", "Odd", "https://example.com/box-art.jpg"};
	CHECK(BoxArtUrl(odd, 26, 36) == "https://example.com/box-art.jpg");
}

TEST_CASE("Modify Channel Information body sends only what changes")
{
	TwitchChannelUpdate update;
	CHECK(BuildTwitchChannelPatch(update) == "{}");
	update.title = "Hello \"chat\"";
	update.categoryId = "33214";
	update.tags = std::vector<std::string>{"English", "Chill"};
	const json body = json::parse(BuildTwitchChannelPatch(update));
	CHECK(body["title"] == "Hello \"chat\"");
	CHECK(body["game_id"] == "33214");
	CHECK(body["tags"] == json::array({"English", "Chill"}));

	TwitchChannelUpdate clear;
	clear.categoryId = "";
	clear.tags = std::vector<std::string>{};
	const json cleared = json::parse(BuildTwitchChannelPatch(clear));
	CHECK(cleared["game_id"] == "0");
	CHECK(cleared["tags"] == json::array());
	CHECK_FALSE(cleared.contains("title"));
}

TEST_CASE("Tags as Twitch takes them")
{
	CHECK(NormalizeTag("#Speedrun") == "Speedrun");
	CHECK(NormalizeTag(" English ") == "English");
	CHECK(NormalizeTag("Español") == "Español");
	CHECK(NormalizeTag("no-backseating").empty());
	CHECK(NormalizeTag("ThisTagIsLongerThan25Chars").empty());
	CHECK(NormalizeTag("Exactly25CharactersLongTg") == "Exactly25CharactersLongTg");
	CHECK(NormalizeTag("#").empty());

	std::vector<std::string> rejected;
	const auto tags = ParseTags("English, speedrun #Chill english,, bad-tag", &rejected);
	CHECK(tags == std::vector<std::string>{"English", "speedrun", "Chill"});
	CHECK(rejected == std::vector<std::string>{"bad-tag"});
	CHECK(JoinTags(tags) == "English, speedrun, Chill");

	// No more than 10.
	CHECK(ParseTags("a b c d e f g h i j k l").size() == kTwitchMaxTags);
	CHECK(ParseTags("").empty());
}

TEST_CASE("Recent categories: newest first, once each, at most 10")
{
	std::vector<RecentCategory> recent;
	for (int i = 0; i < 12; ++i)
		RememberCategory(recent, {{std::to_string(i), "Game " + std::to_string(i), ""}, {}});
	REQUIRE(recent.size() == kMaxRecentCategories);
	CHECK(recent.front().category.id == "11");
	CHECK(recent.back().category.id == "2");

	RememberCategory(recent, {{"5", "Game 5", ""}, {"Chill"}});
	CHECK(recent.size() == kMaxRecentCategories);
	CHECK(recent.front().category.id == "5");
	CHECK(recent.front().tags == std::vector<std::string>{"Chill"});
	CHECK(std::count_if(recent.begin(), recent.end(), [](const auto &r) { return r.category.id == "5"; }) == 1);

	RememberCategory(recent, {{}, {}}); // no category: nothing to remember
	CHECK(recent.front().category.id == "5");
}

TEST_CASE("YouTube broadcast to update: the live one, else the soonest upcoming")
{
	CHECK(YouTubeBroadcastsUrl(true).find("broadcastStatus=active") != std::string::npos);
	CHECK(YouTubeBroadcastsUrl(false).find("broadcastStatus=upcoming") != std::string::npos);

	const auto upcoming = PickYouTubeBroadcast(R"({"items":[
		{"id":"later","snippet":{"title":"Later","scheduledStartTime":"2026-10-20T20:00:00Z"}},
		{"id":"unscheduled","snippet":{"title":"Whenever"}},
		{"id":"soon","snippet":{"title":"Soon","scheduledStartTime":"2026-10-14T20:00:00Z"}}]})",
						   false);
	REQUIRE(upcoming);
	CHECK(upcoming->id == "soon");
	CHECK(upcoming->title == "Soon");
	CHECK_FALSE(upcoming->live);

	const auto onlyUnscheduled = PickYouTubeBroadcast(R"({"items":[{"id":"u","snippet":{"title":"U"}}]})", false);
	REQUIRE(onlyUnscheduled);
	CHECK(onlyUnscheduled->id == "u");

	const auto live = PickYouTubeBroadcast(R"({"items":[{"id":"now","snippet":{"title":"Live!"}}]})", true);
	REQUIRE(live);
	CHECK(live->live);
	CHECK_FALSE(PickYouTubeBroadcast(R"({"items":[]})", false));
	CHECK_FALSE(PickYouTubeBroadcast("{}", true));
}

TEST_CASE("YouTube videos.update keeps every writable snippet field")
{
	CHECK(YouTubeVideoUrl("abc").find("fields=items/snippet(title,description,tags,categoryId,defaultLanguage,"
					  "defaultAudioLanguage)") != std::string::npos);
	const auto snippet = ParseYouTubeVideoSnippet(R"({"items":[{"snippet":{"title":"Old title",
		"description":"My links","tags":["a","b"],"categoryId":"22","defaultLanguage":"en"}}]})");
	REQUIRE(snippet);
	CHECK(snippet->title == "Old title");
	CHECK(snippet->categoryId == "22");

	const auto body = BuildYouTubeVideoUpdate("abc", *snippet, std::string("New title"), std::string("20"));
	REQUIRE(body);
	const json obj = json::parse(*body);
	CHECK(obj["id"] == "abc");
	CHECK(obj["snippet"]["title"] == "New title");
	CHECK(obj["snippet"]["categoryId"] == "20");
	CHECK(obj["snippet"]["description"] == "My links");
	CHECK(obj["snippet"]["tags"] == json::array({"a", "b"}));
	CHECK(obj["snippet"]["defaultLanguage"] == "en");

	// Only the category: the title goes back unchanged.
	const json categoryOnly =
		json::parse(*BuildYouTubeVideoUpdate("abc", *snippet, std::nullopt, std::string("24")));
	CHECK(categoryOnly["snippet"]["title"] == "Old title");

	// Title and category are required.
	YouTubeVideoSnippet noCategory{"T", "", R"({"title":"T"})"};
	CHECK_FALSE(BuildYouTubeVideoUpdate("abc", noCategory, std::nullopt, std::nullopt));
	CHECK(BuildYouTubeVideoUpdate("abc", noCategory, std::nullopt, std::string("20")));
	CHECK_FALSE(BuildYouTubeVideoUpdate("", *snippet, std::nullopt, std::nullopt));
	CHECK_FALSE(ParseYouTubeVideoSnippet(R"({"items":[]})"));
}

TEST_CASE("YouTube titles and categories")
{
	CHECK(YouTubeTitleAllowed("Ranked grind & chill"));
	CHECK_FALSE(YouTubeTitleAllowed("<3 subs"));
	const auto gaming = std::find_if(kYouTubeCategories.begin(), kYouTubeCategories.end(),
					 [](const YouTubeCategory &c) { return std::string(c.id) == "20"; });
	REQUIRE(gaming != kYouTubeCategories.end());
	CHECK(std::string(gaming->name) == "Gaming");
}

TEST_CASE("Stream info errors give the platform's reason")
{
	CHECK(StreamInfoErrorMessage(R"({"error":"Bad Request","status":400,"message":"The tag is not valid."})") ==
	      "The tag is not valid.");
	CHECK(StreamInfoErrorMessage(R"({"error":{"code":403,"message":"Forbidden"}})") == "Forbidden");
}

TEST_CASE("Stream info settings round trip through config.json")
{
	ChatConfig config;
	CHECK(config.streamInfoTwitch);
	CHECK(config.streamInfoYouTube);
	config.streamInfoYouTube = false;
	RememberCategory(config.recentCategories, {{"1", "Older", ""}, {}});
	RememberCategory(config.recentCategories,
			 {{"33214", "Fortnite", "https://static-cdn.jtvnw.net/ttv-boxart/33214-52x72.jpg"}, {"Chill"}});

	const ChatConfig back = ParseConfig(SerializeConfig(config));
	CHECK(back.streamInfoTwitch);
	CHECK_FALSE(back.streamInfoYouTube);
	REQUIRE(back.recentCategories.size() == 2);
	CHECK(back.recentCategories[0].category.name == "Fortnite");
	CHECK(back.recentCategories[0].category.boxArtUrl == config.recentCategories[0].category.boxArtUrl);
	CHECK(back.recentCategories[0].tags == std::vector<std::string>{"Chill"});
	CHECK(back.recentCategories[1].category.id == "1");

	// Hand-edited: bad tags and nameless entries are dropped.
	const ChatConfig edited = ParseConfig(
		R"({"stream_info":{"recent_categories":[{"id":"2","name":"Ok","tags":["good","bad tag!"]},{"id":"3"}]}})");
	REQUIRE(edited.recentCategories.size() == 1);
	CHECK(edited.recentCategories[0].tags == std::vector<std::string>{"good"});
}

TEST_CASE("Scene categories: one link per scene, replaced, removed, saved")
{
	std::vector<SceneCategory> links;
	LinkSceneCategory(links, {"uuid-game", "Gaming", {"32982", "Grand Theft Auto V", ""}});
	LinkSceneCategory(links, {"uuid-chat", "Just Chatting", {"509658", "Just Chatting", ""}});
	LinkSceneCategory(links, {"uuid-game", "Gaming", {"33214", "Fortnite", ""}}); // relinked
	LinkSceneCategory(links, {"", "No id", {"1", "x", ""}});                      // ignored
	LinkSceneCategory(links, {"uuid-x", "No category", {}});                      // ignored
	REQUIRE(links.size() == 2);
	REQUIRE(FindSceneCategory(links, "uuid-game"));
	CHECK(FindSceneCategory(links, "uuid-game")->category.name == "Fortnite");
	CHECK_FALSE(FindSceneCategory(links, "uuid-brb"));

	ChatConfig config;
	CHECK_FALSE(config.categoryByScene);
	CHECK(config.categorySceneDelaySeconds == kDefaultSceneCategoryDelay);
	config.categoryByScene = true;
	config.categorySceneDelaySeconds = 25;
	config.sceneCategories = links;
	const ChatConfig back = ParseConfig(SerializeConfig(config));
	CHECK(back.categoryByScene);
	CHECK(back.categorySceneDelaySeconds == 25);
	REQUIRE(back.sceneCategories.size() == 2);
	CHECK(back.sceneCategories[0].sceneUuid == "uuid-game");
	CHECK(back.sceneCategories[0].category.id == "33214");
	CHECK(back.sceneCategories[1].sceneName == "Just Chatting");

	UnlinkSceneCategory(links, "uuid-game");
	CHECK(links.size() == 1);
	CHECK(ParseConfig(R"({"stream_info":{"scene_delay_seconds":999}})").categorySceneDelaySeconds ==
	      kMaxSceneCategoryDelay);
}
