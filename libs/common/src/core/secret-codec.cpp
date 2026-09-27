/*
obs-tools
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

#include "secret-codec.hpp"
#include "text-util.hpp"

#include <string_view>

namespace unified_chat {

static constexpr std::string_view kSecretPrefix = "enc:v1:";

std::string SealSecret(const std::string &value, const SecretCodec *codec)
{
	if (value.empty() || !codec || !codec->protect)
		return value;
	auto sealed = codec->protect(value);
	return sealed ? std::string(kSecretPrefix) + Base64Encode(*sealed) : value;
}

std::string OpenSecret(const std::string &stored, const SecretCodec *codec, SecretReport *report)
{
	if (stored.rfind(kSecretPrefix, 0) != 0) {
		if (!stored.empty() && report)
			report->plaintextSecrets = true;
		return stored;
	}
	std::optional<std::string> opened;
	if (codec && codec->unprotect) {
		if (auto bytes = Base64Decode(std::string_view(stored).substr(kSecretPrefix.size())))
			opened = codec->unprotect(*bytes);
	}
	if (!opened && report)
		report->unreadableSecrets = true;
	return opened.value_or(std::string()); // unreadable: sign in again rather than fail
}

} // namespace unified_chat
