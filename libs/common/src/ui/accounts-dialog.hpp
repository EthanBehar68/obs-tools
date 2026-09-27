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

class QWidget;

namespace unified_chat {

// The shared Twitch and Google sign-ins (Tools > OBS Tools > Accounts...). Modal; saves as it goes and, on
// closing, tells every plugin which accounts changed. Opening it again while it's open just raises it.
void OpenAccountsDialog(QWidget *parent);

} // namespace unified_chat
