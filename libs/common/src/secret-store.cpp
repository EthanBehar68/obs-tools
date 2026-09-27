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

#include "secret-store.hpp"

#include <string_view>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace unified_chat {

#ifdef _WIN32

// Ties the encrypted data to its file as well as to the user, so other DPAPI blobs can't be swapped in.
static constexpr std::string_view kAccountsEntropy = "obs-tools/accounts/v1";
static constexpr std::string_view kChatConfigEntropy = "obs-unified-chat/config/v1";

static DATA_BLOB Blob(std::string_view bytes)
{
	return {(DWORD)bytes.size(), reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data()))};
}

static std::optional<std::string> Protect(const std::string &plain, std::string_view entropyText)
{
	DATA_BLOB in = Blob(plain);
	DATA_BLOB entropy = Blob(entropyText);
	DATA_BLOB out = {};
	if (!CryptProtectData(&in, L"obs-tools", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return std::nullopt;
	std::string sealed(reinterpret_cast<const char *>(out.pbData), out.cbData);
	LocalFree(out.pbData);
	return sealed;
}

static std::optional<std::string> Unprotect(const std::string &sealed, std::string_view entropyText)
{
	DATA_BLOB in = Blob(sealed);
	DATA_BLOB entropy = Blob(entropyText);
	DATA_BLOB out = {};
	if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return std::nullopt;
	std::string plain(reinterpret_cast<const char *>(out.pbData), out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}

static SecretCodec MakeCodec(std::string_view entropy)
{
	return {[entropy](const std::string &plain) { return Protect(plain, entropy); },
		[entropy](const std::string &sealed) {
			return Unprotect(sealed, entropy);
		}};
}

const SecretCodec *PlatformSecretCodec(SecretPurpose purpose)
{
	static const SecretCodec accounts = MakeCodec(kAccountsEntropy);
	static const SecretCodec chatConfig = MakeCodec(kChatConfigEntropy);
	return purpose == SecretPurpose::Accounts ? &accounts : &chatConfig;
}

#else

const SecretCodec *PlatformSecretCodec(SecretPurpose)
{
	return nullptr;
}

#endif

} // namespace unified_chat
