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

} // namespace unified_chat
