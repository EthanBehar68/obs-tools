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

#pragma once

#include <functional>
#include <optional>
#include <string>

namespace unified_chat {

// Encrypts secrets at rest (on Windows: DPAPI, bound to the user's account). Both functions work on raw bytes;
// unprotect returns nullopt when the data can't be decrypted, e.g. a config copied from another account.
struct SecretCodec {
	std::function<std::optional<std::string>(const std::string &)> protect;
	std::function<std::optional<std::string>(const std::string &)> unprotect;
};

struct SecretReport {
	bool plaintextSecrets = false;  // at least one secret was stored unencrypted (an older file)
	bool unreadableSecrets = false; // at least one encrypted secret couldn't be decrypted (it's left empty)
};

// "enc:v1:<base64>" when a codec is given. If encryption fails the value is written as is, since losing a
// sign-in is worse than an unencrypted file.
std::string SealSecret(const std::string &value, const SecretCodec *codec);
// Plain text passes through (and is reported); an unreadable secret comes back empty.
std::string OpenSecret(const std::string &stored, const SecretCodec *codec, SecretReport *report);

} // namespace unified_chat
