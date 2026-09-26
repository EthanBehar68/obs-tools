#include <doctest.h>

#include "core/twitch-irc.hpp"

using namespace unified_chat;
using namespace unified_chat::twitch;

TEST_CASE("ParseIrcLine handles tags, prefix, params and trailing")
{
	auto msg = ParseIrcLine("@badge-info=;color=#1E90FF;display-name=Cool\\sGuy;id=abc-123 "
				":coolguy!coolguy@coolguy.tmi.twitch.tv PRIVMSG #streamer :hello there :)\r\n");
	REQUIRE(msg);
	CHECK(msg->command == "PRIVMSG");
	CHECK(std::string(msg->Nick()) == "coolguy");
	CHECK(msg->Tag("display-name") == "Cool Guy");
	CHECK(msg->Tag("badge-info") == "");
	CHECK(msg->Tag("missing") == "");
	REQUIRE(msg->params.size() == 2);
	CHECK(msg->params[0] == "#streamer");
	CHECK(msg->params[1] == "hello there :)");
}

TEST_CASE("ParseIrcLine without tags or prefix")
{
	auto msg = ParseIrcLine("PING :tmi.twitch.tv");
	REQUIRE(msg);
	CHECK(msg->command == "PING");
	CHECK(msg->prefix.empty());
	REQUIRE(msg->params.size() == 1);
	CHECK(msg->params[0] == "tmi.twitch.tv");
}

TEST_CASE("ParseIrcLine rejects empty input")
{
	CHECK_FALSE(ParseIrcLine(""));
	CHECK_FALSE(ParseIrcLine("\r\n"));
	CHECK_FALSE(ParseIrcLine("@a=b"));
}

TEST_CASE("UnescapeTagValue follows the IRCv3 rules")
{
	CHECK(UnescapeTagValue("a\\sb") == "a b");
	CHECK(UnescapeTagValue("semi\\:colon") == "semi;colon");
	CHECK(UnescapeTagValue("back\\\\slash") == "back\\slash");
	CHECK(UnescapeTagValue("cr\\rlf\\n") == "cr\rlf\n");
	CHECK(UnescapeTagValue("trailing\\") == "trailing");
	CHECK(UnescapeTagValue("unknown\\x") == "unknownx");
}

TEST_CASE("NormalizeChannel accepts names, hashes and URLs")
{
	CHECK(NormalizeChannel("MyChannel") == "mychannel");
	CHECK(NormalizeChannel("#mychannel") == "mychannel");
	CHECK(NormalizeChannel(" https://www.twitch.tv/My_Channel?ref=x ") == "my_channel");
	CHECK(NormalizeChannel("twitch.tv/abc/videos") == "abc");
	CHECK(NormalizeChannel("") == "");
	CHECK(NormalizeChannel("bad name") == "");
	CHECK(NormalizeChannel("abcdefghijklmnopqrstuvwxyz") == ""); // 26 chars
}

TEST_CASE("LineBuffer reassembles split lines")
{
	LineBuffer buffer;
	CHECK(buffer.Append("PING :tmi").empty());
	auto lines = buffer.Append(".twitch.tv\r\n:a PRIVMSG #b :c\r\n:partial");
	REQUIRE(lines.size() == 2);
	CHECK(lines[0] == "PING :tmi.twitch.tv");
	CHECK(lines[1] == ":a PRIVMSG #b :c");
	lines = buffer.Append(" line\n\r\n");
	REQUIRE(lines.size() == 1);
	CHECK(lines[0] == ":partial line");
}

TEST_CASE("Anonymous session logs in read-only")
{
	IrcSession session("#Streamer", "", "", 777);
	auto lines = session.Start();
	REQUIRE(lines.size() == 3);
	CHECK(lines[0] == "CAP REQ :twitch.tv/tags twitch.tv/commands");
	CHECK(lines[1] == "NICK justinfan777");
	CHECK(lines[2] == "JOIN #streamer");
	CHECK_FALSE(session.IsAuthenticated());
	CHECK_FALSE(session.CanSend());
}

TEST_CASE("Authenticated session sends PASS before NICK and strips oauth: prefix")
{
	IrcSession session("streamer", "MyBot", "oauth:secret");
	auto lines = session.Start();
	REQUIRE(lines.size() == 4);
	CHECK(lines[1] == "PASS oauth:secret");
	CHECK(lines[2] == "NICK mybot");
	CHECK(lines[3] == "JOIN #streamer");
	CHECK(session.IsAuthenticated());
}

TEST_CASE("Session answers PING and reports RECONNECT")
{
	IrcSession session("streamer", "", "");
	SessionOutput out;
	session.HandleLine("PING :tmi.twitch.tv", out);
	REQUIRE(out.outgoing.size() == 1);
	CHECK(out.outgoing[0] == "PONG :tmi.twitch.tv");
	session.HandleLine(":tmi.twitch.tv RECONNECT", out);
	CHECK(out.reconnect);
}

TEST_CASE("Session converts PRIVMSG to a chat message")
{
	IrcSession session("streamer", "", "");
	SessionOutput out;
	session.HandleLine("@color=#FF0000;display-name=Viewer;id=m1 :viewer!viewer@viewer.tmi.twitch.tv "
			   "PRIVMSG #streamer :hi <b>there</b>",
			   out);
	REQUIRE(out.messages.size() == 1);
	const auto &chat = out.messages[0];
	CHECK(chat.platform == Platform::Twitch);
	CHECK(chat.id == "m1");
	CHECK(chat.author == "Viewer");
	CHECK(chat.color == "#ff0000");
	CHECK(chat.text == "hi <b>there</b>");
	CHECK_FALSE(chat.isAction);
	CHECK_FALSE(chat.isSelf);
}

TEST_CASE("Session falls back to nick and parses /me actions")
{
	IrcSession session("streamer", "", "");
	SessionOutput out;
	session.HandleLine(":lurker!lurker@lurker.tmi.twitch.tv PRIVMSG #streamer :\x01"
			   "ACTION waves\x01",
			   out);
	REQUIRE(out.messages.size() == 1);
	CHECK(out.messages[0].author == "lurker");
	CHECK(out.messages[0].isAction);
	CHECK(out.messages[0].text == "waves");
}

TEST_CASE("Session flags failed authentication")
{
	IrcSession session("streamer", "me", "badtoken");
	SessionOutput out;
	session.HandleLine(":tmi.twitch.tv NOTICE * :Login authentication failed", out);
	CHECK(out.authFailed);
	REQUIRE(out.notices.size() == 1);
}

TEST_CASE("Session reports USERNOTICE system messages")
{
	IrcSession session("streamer", "", "");
	SessionOutput out;
	session.HandleLine("@system-msg=Fan\\ssubscribed\\sfor\\s3\\smonths! :tmi.twitch.tv USERNOTICE #streamer :hype",
			   out);
	REQUIRE(out.notices.size() == 1);
	CHECK(out.notices[0] == "Fan subscribed for 3 months! - hype");
}

TEST_CASE("Session becomes sendable after USERSTATE and echoes with display name")
{
	IrcSession session("streamer", "me", "token");
	session.Start();
	CHECK_FALSE(session.CanSend());

	SessionOutput out;
	session.HandleLine("@color=#00FF00;display-name=Me_Display :tmi.twitch.tv USERSTATE #streamer", out);
	CHECK(session.CanSend());

	auto echo = session.LocalEcho("hello");
	CHECK(echo.author == "Me_Display");
	CHECK(echo.color == "#00ff00");
	CHECK(echo.isSelf);
	CHECK(echo.text == "hello");

	session.Start(); // reconnect resets join state
	CHECK_FALSE(session.CanSend());
}

TEST_CASE("BuildPrivmsg sanitizes and supports /me")
{
	IrcSession session("streamer", "me", "token");
	CHECK(session.BuildPrivmsg("hi\r\nJOIN #evil") == "PRIVMSG #streamer :hi  JOIN #evil");
	CHECK(session.BuildPrivmsg("/me dances") == "PRIVMSG #streamer :\x01"
						    "ACTION dances\x01");
	CHECK_FALSE(session.BuildPrivmsg("   "));
	CHECK(session.LocalEcho("/me dances").isAction);
}

TEST_CASE("Messages carry the login to mention the author by")
{
	IrcSession session("streamer", "", "");
	SessionOutput out;
	session.HandleLine("@display-name=\xE3\x81\x82\xE3\x81\x84;id=m1 :aiko_jp!aiko_jp@aiko_jp.tmi.twitch.tv "
			   "PRIVMSG #streamer :hi",
			   out);
	REQUIRE(out.messages.size() == 1);
	CHECK(out.messages[0].author == "\xE3\x81\x82\xE3\x81\x84"); // localized display name
	CHECK(out.messages[0].mention == "aiko_jp");
	CHECK(out.messages[0].replyTo.empty());
}

TEST_CASE("Incoming replies name their parent and drop the repeated leading @mention")
{
	IrcSession session("streamer", "", "");
	SessionOutput out;
	session.HandleLine(
		"@display-name=Viewer;id=m2;reply-parent-msg-id=m1;reply-parent-user-login=dezad;"
		"reply-parent-display-name=Dezad;reply-parent-msg-body=hello :viewer!viewer@viewer.tmi.twitch.tv "
		"PRIVMSG #streamer :@Dezad welcome back",
		out);
	session.HandleLine("@display-name=Other;id=m3;reply-parent-user-login=dezad :other!other@other.tmi.twitch.tv "
			   "PRIVMSG #streamer :@someoneelse hi",
			   out);
	REQUIRE(out.messages.size() == 2);
	CHECK(out.messages[0].replyTo == "Dezad");
	CHECK(out.messages[0].text == "welcome back");
	CHECK(out.messages[1].replyTo == "dezad"); // no display name: the login
	CHECK(out.messages[1].text == "@someoneelse hi");
}

TEST_CASE("BuildPrivmsg sends threaded replies and refuses unsafe parent ids")
{
	IrcSession session("streamer", "me", "token");
	CHECK(session.BuildPrivmsg("@dezad hi", "885196de-cb67-427a-baa8-82f9b0fcd05f") ==
	      "@reply-parent-msg-id=885196de-cb67-427a-baa8-82f9b0fcd05f PRIVMSG #streamer :@dezad hi");
	CHECK(session.BuildPrivmsg("hi", "bad id;x=y") == "PRIVMSG #streamer :hi");
	CHECK(session.BuildPrivmsg("hi", "") == "PRIVMSG #streamer :hi");

	auto echo = session.LocalEcho("@dezad hi", "dezad");
	CHECK(echo.replyTo == "dezad");
	CHECK(echo.text == "hi");
	CHECK(echo.mention == "me");
}

TEST_CASE("ParseValidateLogin reads the login")
{
	CHECK(ParseValidateLogin(R"({"client_id":"c","login":"mychannel","scopes":["chat:read"],"expires_in":5000})") ==
	      "mychannel");
	CHECK(ParseValidateLogin(R"({"status":401,"message":"invalid access token"})") == "");
	CHECK(ParseValidateLogin("") == "");
}
