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

#include "core/chat-message.hpp"

#include <QIcon>
#include <QImage>

namespace unified_chat {

QImage PlatformImage(Platform platform, int size, qreal devicePixelRatio);
// Marks the streamer's own chat lines.
QImage SelfBadgeImage(int size, qreal devicePixelRatio);
QIcon PlatformIcon(Platform platform, int size = 16);
QIcon BothPlatformsIcon(int height = 16);

} // namespace unified_chat
