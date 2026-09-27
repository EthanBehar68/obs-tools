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

#pragma once

#include "core/secret-codec.hpp"

namespace unified_chat {

enum class SecretPurpose {
	Accounts,         // plugin_config/obs-tools/accounts.json
	LegacyChatConfig, // sign-ins in obs-unified-chat's config.json up to 1.2.0 (read once, to move them)
};

// Encrypts secrets for the current Windows user (DPAPI), so a copied file can't be read on another account or PC.
// Each purpose has its own entropy, so blobs can't be swapped between files. Returns nullptr where no platform
// store is available; secrets are then stored as is.
const SecretCodec *PlatformSecretCodec(SecretPurpose purpose);

} // namespace unified_chat
