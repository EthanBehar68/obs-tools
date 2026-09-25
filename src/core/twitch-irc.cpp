/*
obs-unified-chat
Copyright (C) 2026 ebehar

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "twitch-irc.hpp"
#include "text-util.hpp"

#include <json.hpp>

namespace unified_chat::twitch {

static constexpr std::string_view kActionPrefix = "\x01"
						  "ACTION ";

std::string_view IrcMessage::Nick() const
{
	return std::string_view(prefix).substr(0, prefix.find('!'));
}

const std::string &IrcMessage::Tag(std::string_view key) const
{
	static const std::string empty;
	auto it = tags.find(key);
	return it == tags.end() ? empty : it->second;
}

std::string UnescapeTagValue(std::string_view value)
{
	std::string out;
	out.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		char c = value[i];
		if (c != '\\') {
			out.push_back(c);
			continue;
		}
		if (++i >= value.size())
			break;
		switch (value[i]) {
		case ':':
			out.push_back(';');
			break;
		case 's':
			out.push_back(' ');
			break;
		case 'r':
			out.push_back('\r');
			break;
		case 'n':
			out.push_back('\n');
			break;
		default:
			out.push_back(value[i]);
		}
	}
	return out;
}

static std::string_view NextToken(std::string_view &rest)
{
	auto space = rest.find(' ');
	std::string_view token = rest.substr(0, space);
	rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
	while (!rest.empty() && rest.front() == ' ')
		rest.remove_prefix(1);
	return token;
}

std::optional<IrcMessage> ParseIrcLine(std::string_view line)
{
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		line.remove_suffix(1);
	if (line.empty())
		return std::nullopt;

	IrcMessage msg;
	std::string_view rest = line;

	if (rest.front() == '@') {
		std::string_view tags = NextToken(rest).substr(1);
		while (!tags.empty()) {
			auto semi = tags.find(';');
			std::string_view pair = tags.substr(0, semi);
			tags = semi == std::string_view::npos ? std::string_view() : tags.substr(semi + 1);
			if (pair.empty())
				continue;
			auto eq = pair.find('=');
			std::string key(pair.substr(0, eq));
			msg.tags[key] = eq == std::string_view::npos ? std::string()
								     : UnescapeTagValue(pair.substr(eq + 1));
		}
	}

	if (!rest.empty() && rest.front() == ':')
		msg.prefix = std::string(NextToken(rest).substr(1));

	msg.command = std::string(NextToken(rest));
	if (msg.command.empty())
		return std::nullopt;

	while (!rest.empty()) {
		if (rest.front() == ':') {
			msg.params.emplace_back(rest.substr(1));
			break;
		}
		msg.params.emplace_back(NextToken(rest));
	}
	return msg;
}

static bool IsLoginChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

std::string NormalizeChannel(std::string_view input)
{
	std::string value = ToLower(Trim(input));

	auto host = value.find("twitch.tv/");
	if (host != std::string::npos)
		value = value.substr(host + 10);
	auto end = value.find_first_of("/?#", value.empty() || value[0] != '#' ? 0 : 1);
	if (end != std::string::npos)
		value = value.substr(0, end);
	if (!value.empty() && value[0] == '#')
		value.erase(0, 1);

	if (value.empty() || value.size() > 25)
		return {};
	for (char c : value) {
		if (!IsLoginChar(c))
			return {};
	}
	return value;
}

std::string ParseValidateLogin(const std::string &body)
{
	auto obj = nlohmann::json::parse(body, nullptr, false);
	if (!obj.is_object())
		return {};
	auto login = obj.find("login");
	return login != obj.end() && login->is_string() ? login->get<std::string>() : std::string();
}

std::vector<std::string> LineBuffer::Append(std::string_view data)
{
	pending_.append(data);
	std::vector<std::string> lines;
	size_t start = 0;
	for (;;) {
		auto nl = pending_.find('\n', start);
		if (nl == std::string::npos)
			break;
		size_t len = nl - start;
		if (len > 0 && pending_[nl - 1] == '\r')
			--len;
		if (len > 0)
			lines.emplace_back(pending_.substr(start, len));
		start = nl + 1;
	}
	pending_.erase(0, start);
	return lines;
}

IrcSession::IrcSession(std::string channel, std::string login, std::string token, unsigned anonymousSuffix)
	: channel_(NormalizeChannel(channel)),
	  login_(ToLower(Trim(login))),
	  token_(Trim(token))
{
	if (token_.rfind("oauth:", 0) == 0)
		token_.erase(0, 6);
	if (token_.empty() || login_.empty()) {
		token_.clear();
		nick_ = "justinfan" + std::to_string(anonymousSuffix);
	} else {
		nick_ = login_;
	}
}

std::vector<std::string> IrcSession::Start()
{
	joined_ = false;
	std::vector<std::string> lines;
	lines.emplace_back("CAP REQ :twitch.tv/tags twitch.tv/commands");
	if (IsAuthenticated())
		lines.emplace_back("PASS oauth:" + token_);
	lines.emplace_back("NICK " + nick_);
	if (!channel_.empty())
		lines.emplace_back("JOIN #" + channel_);
	return lines;
}

void IrcSession::HandleLine(std::string_view line, SessionOutput &out)
{
	auto parsed = ParseIrcLine(line);
	if (!parsed)
		return;
	const IrcMessage &msg = *parsed;

	if (msg.command == "PING") {
		out.outgoing.emplace_back("PONG :" +
					  (msg.params.empty() ? std::string("tmi.twitch.tv") : msg.params.back()));
	} else if (msg.command == "PRIVMSG" && msg.params.size() >= 2) {
		ChatMessage chat;
		chat.platform = Platform::Twitch;
		chat.id = msg.Tag("id");
		chat.author = msg.Tag("display-name");
		if (chat.author.empty())
			chat.author = std::string(msg.Nick());
		chat.color = SanitizeColor(msg.Tag("color"));
		chat.text = msg.params[1];
		if (chat.text.rfind(kActionPrefix, 0) == 0) {
			chat.isAction = true;
			chat.text.erase(0, kActionPrefix.size());
			if (!chat.text.empty() && chat.text.back() == '\x01')
				chat.text.pop_back();
		}
		chat.isSelf = msg.Nick() == nick_;
		out.messages.push_back(std::move(chat));
	} else if (msg.command == "USERNOTICE") {
		std::string notice = msg.Tag("system-msg");
		if (msg.params.size() >= 2 && !msg.params[1].empty())
			notice += notice.empty() ? msg.params[1] : " - " + msg.params[1];
		if (!notice.empty())
			out.notices.push_back(std::move(notice));
	} else if (msg.command == "NOTICE") {
		std::string text = msg.params.empty() ? std::string() : msg.params.back();
		if (text.find("Login authentication failed") != std::string::npos ||
		    text.find("Improperly formatted auth") != std::string::npos) {
			out.authFailed = true;
		}
		if (!text.empty())
			out.notices.push_back(std::move(text));
	} else if (msg.command == "GLOBALSTATE" || msg.command == "USERSTATE") {
		if (const auto &name = msg.Tag("display-name"); !name.empty())
			displayName_ = name;
		color_ = SanitizeColor(msg.Tag("color"));
		if (msg.command == "USERSTATE")
			joined_ = true;
	} else if (msg.command == "JOIN") {
		if (msg.Nick() == nick_)
			joined_ = true;
	} else if (msg.command == "RECONNECT") {
		out.reconnect = true;
	}
}

std::optional<std::string> IrcSession::BuildPrivmsg(std::string_view text) const
{
	std::string body = Trim(StripLineBreaks(text));
	if (body.empty() || channel_.empty())
		return std::nullopt;
	if (body.rfind("/me ", 0) == 0)
		body = std::string(kActionPrefix) + body.substr(4) + "\x01";
	return "PRIVMSG #" + channel_ + " :" + body;
}

ChatMessage IrcSession::LocalEcho(std::string_view text) const
{
	ChatMessage chat;
	chat.platform = Platform::Twitch;
	chat.author = displayName_.empty() ? login_ : displayName_;
	chat.color = color_;
	chat.isSelf = true;
	chat.text = Trim(StripLineBreaks(text));
	if (chat.text.rfind("/me ", 0) == 0) {
		chat.isAction = true;
		chat.text.erase(0, 4);
	}
	return chat;
}

} // namespace unified_chat::twitch
