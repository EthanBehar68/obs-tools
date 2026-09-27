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

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace stream_alerts {

class AlertsController;

// Tools > OBS Tools > Stream Alerts...: what each alert shows, plus Test buttons that play one right away with the
// values as entered. Cancel puts the previous settings back.
class AlertsDialog : public QDialog {
public:
	AlertsDialog(AlertsController *controller, QWidget *parent);

	void accept() override;
	void reject() override;

private:
	struct Row {
		QCheckBox *enabled = nullptr;
		QComboBox *source = nullptr;
		QComboBox *textSource = nullptr;
		QLineEdit *message = nullptr;
		QLineEdit *overflow = nullptr;
		QLabel *status = nullptr;
	};

	QGroupBox *MakeGroup(AlertKind kind, const char *titleKey, const QStringList &sources,
			     const QStringList &textSources);
	Row &RowFor(AlertKind kind) { return kind == AlertKind::Follow ? follow_ : subscriber_; }
	AlertsConfig Current() const;
	void UpdateStatus();

	AlertsController *controller_;
	AlertsConfig original_;
	Row follow_;
	Row subscriber_;
	QSpinBox *duration_;
};

} // namespace stream_alerts
