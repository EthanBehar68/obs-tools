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

#include "core/chat-router.hpp"

#include <QButtonGroup>
#include <QWidget>

namespace unified_chat {

// Segmented three-way switch choosing where outgoing messages go: Twitch, YouTube or both.
class TargetSwitch : public QWidget {
	Q_OBJECT

public:
	explicit TargetSwitch(QWidget *parent = nullptr);

	SendTarget Target() const;
	void SetTarget(SendTarget target);

signals:
	void TargetChanged();

private:
	QButtonGroup *group_;
};

} // namespace unified_chat
