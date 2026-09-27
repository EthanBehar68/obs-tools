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

#include "target-switch.hpp"
#include "platform-icons.hpp"

#include <obs-module.h>

#include <QHBoxLayout>
#include <QToolButton>

namespace unified_chat {

TargetSwitch::TargetSwitch(QWidget *parent) : QWidget(parent), group_(new QButtonGroup(this))
{
	auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	struct Option {
		SendTarget target;
		QIcon icon;
		const char *tooltip;
	};
	const Option options[] = {
		{SendTarget::Twitch, PlatformIcon(Platform::Twitch), "Target.Twitch"},
		{SendTarget::YouTube, PlatformIcon(Platform::YouTube), "Target.YouTube"},
		{SendTarget::Both, BothPlatformsIcon(), "Target.Both"},
	};

	for (const auto &option : options) {
		auto button = new QToolButton(this);
		button->setCheckable(true);
		button->setIcon(option.icon);
		button->setIconSize(option.target == SendTarget::Both ? QSize(34, 16) : QSize(16, 16));
		button->setToolTip(QString::fromUtf8(obs_module_text(option.tooltip)));
		button->setAccessibleName(button->toolTip());
		group_->addButton(button, (int)option.target);
		layout->addWidget(button);
	}
	group_->setExclusive(true);
	SetTarget(SendTarget::Both);

	connect(group_, &QButtonGroup::idClicked, this, [this](int) { emit TargetChanged(); });
}

SendTarget TargetSwitch::Target() const
{
	int id = group_->checkedId();
	return id < 0 ? SendTarget::Both : (SendTarget)id;
}

void TargetSwitch::SetTarget(SendTarget target)
{
	if (auto button = group_->button((int)target))
		button->setChecked(true);
}

} // namespace unified_chat
