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

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace unified_chat {

#ifdef _WIN32

// Ties the encrypted data to this plugin as well as to the user, so other DPAPI blobs can't be swapped in.
static constexpr char kEntropy[] = "obs-unified-chat/config/v1";

static DATA_BLOB Blob(const std::string &bytes)
{
	return {(DWORD)bytes.size(), reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data()))};
}

static DATA_BLOB EntropyBlob()
{
	return {(DWORD)(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
}

static std::optional<std::string> Protect(const std::string &plain)
{
	DATA_BLOB in = Blob(plain);
	DATA_BLOB entropy = EntropyBlob();
	DATA_BLOB out = {};
	if (!CryptProtectData(&in, L"obs-unified-chat", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return std::nullopt;
	std::string sealed(reinterpret_cast<const char *>(out.pbData), out.cbData);
	LocalFree(out.pbData);
	return sealed;
}

static std::optional<std::string> Unprotect(const std::string &sealed)
{
	DATA_BLOB in = Blob(sealed);
	DATA_BLOB entropy = EntropyBlob();
	DATA_BLOB out = {};
	if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return std::nullopt;
	std::string plain(reinterpret_cast<const char *>(out.pbData), out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}

const SecretCodec *PlatformSecretCodec()
{
	static const SecretCodec codec{Protect, Unprotect};
	return &codec;
}

#else

const SecretCodec *PlatformSecretCodec()
{
	return nullptr;
}

#endif

} // namespace unified_chat
