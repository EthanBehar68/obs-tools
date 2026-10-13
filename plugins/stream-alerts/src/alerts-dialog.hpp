/*
obs-stream-alerts
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

#include "core/alerts-config.hpp"

#include <QDialog>
#include <QStringList>

#include <map>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QWidget;

namespace stream_alerts {

class AlertsController;

// Tools > OBS Tools > Stream Alerts...: the alert kinds in a list, and the selected kind's settings beside it, with
// Test buttons that play one right away with the values as entered. Cancel puts the previous settings back.
class AlertsDialog : public QDialog {
public:
	AlertsDialog(AlertsController *controller, QWidget *parent);

	void accept() override;
	void reject() override;

private:
	struct Page {
		QCheckBox *enabled = nullptr;
		QComboBox *source = nullptr;
		QComboBox *textSource = nullptr;
		QPlainTextEdit *message = nullptr;
		QPlainTextEdit *overflow = nullptr; // Follow, Subscriber
		QPlainTextEdit *variant = nullptr;  // TwitchSub, Member
		QSpinBox *minimum = nullptr;        // Cheer: bits; Raid: viewers
		QSpinBox *interval = nullptr;       // Subscriber: seconds between checks
		QLabel *status = nullptr;
		QLabel *warning = nullptr; // placeholders this kind won't fill in
	};

	QWidget *MakePage(AlertKind kind, const QStringList &sources, const QStringList &textSources);
	AlertsConfig Current() const;
	// What's wrong with a kind's messages (placeholders it won't fill in), one line per message; empty when fine.
	QString Problems(AlertKind kind) const;
	// False, with the kind shown and a warning, when one (or, without only, any kind) has problems.
	bool CheckMessages(const AlertKind *only = nullptr);
	void UpdateStatus();

	AlertsController *controller_;
	AlertsConfig original_;
	std::map<AlertKind, Page> pages_;
	QListWidget *kinds_;
	QSpinBox *duration_;
};

} // namespace stream_alerts
