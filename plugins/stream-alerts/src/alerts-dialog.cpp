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

#include "alerts-dialog.hpp"
#include "alerts-controller.hpp"
#include "ui/accounts-dialog.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

namespace stream_alerts {

static QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static std::string ToStd(const QString &value)
{
	return value.trimmed().toStdString();
}

struct SourceNames {
	QStringList all;
	QStringList text;
};

static bool AddSource(void *param, obs_source_t *source)
{
	auto names = static_cast<SourceNames *>(param);
	const char *name = obs_source_get_name(source);
	if (!name || !*name)
		return true;
	names->all.append(QString::fromUtf8(name));
	const char *id = obs_source_get_unversioned_id(source);
	if (id && std::strncmp(id, "text_", 5) == 0)
		names->text.append(QString::fromUtf8(name));
	return true;
}

static SourceNames ListSources()
{
	SourceNames names;
	obs_enum_scenes(AddSource, &names); // scenes and groups
	obs_enum_sources(AddSource, &names);
	for (QStringList *list : {&names.all, &names.text}) {
		list->removeDuplicates();
		list->sort(Qt::CaseInsensitive);
	}
	return names;
}

// Editable, so a source added later (or renamed) can still be typed in.
static QComboBox *MakeSourceCombo(const QStringList &names, const std::string &current, QWidget *parent)
{
	auto combo = new QComboBox(parent);
	combo->setEditable(true);
	combo->addItem(QString());
	combo->addItems(names);
	combo->setCurrentText(QString::fromStdString(current));
	return combo;
}

AlertsDialog::AlertsDialog(AlertsController *controller, QWidget *parent)
	: QDialog(parent),
	  controller_(controller),
	  original_(controller->Config())
{
	setWindowTitle(Text("Alerts.Title"));
	setMinimumWidth(520);

	const SourceNames names = ListSources();
	auto layout = new QVBoxLayout(this);
	auto intro = new QLabel(Text("Alerts.Intro"), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	layout->addWidget(MakeGroup(AlertKind::Follow, "Alerts.Follows", names.all, names.text));
	layout->addWidget(MakeGroup(AlertKind::Subscriber, "Alerts.Subscribers", names.all, names.text));

	auto general = new QFormLayout();
	duration_ = new QSpinBox(this);
	duration_->setRange(1, 60);
	duration_->setSuffix(" s");
	duration_->setValue(original_.durationSeconds);
	general->addRow(Text("Alerts.Duration"), duration_);
	layout->addLayout(general);

	auto bottom = new QHBoxLayout();
	auto accounts = new QPushButton(Text("Alerts.Accounts"), this);
	connect(accounts, &QPushButton::clicked, this, [this]() { unified_chat::OpenAccountsDialog(this); });
	bottom->addWidget(accounts);
	bottom->addStretch();
	auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	bottom->addWidget(buttons);
	layout->addLayout(bottom);

	// Only while the window is open: the listeners' state changes as they connect.
	auto statusTimer = new QTimer(this);
	connect(statusTimer, &QTimer::timeout, this, [this]() { UpdateStatus(); });
	statusTimer->start(1000);
	UpdateStatus();
}

QGroupBox *AlertsDialog::MakeGroup(AlertKind kind, const char *titleKey, const QStringList &sources,
				   const QStringList &textSources)
{
	const AlertSettings &settings = original_.For(kind);
	Row &row = RowFor(kind);
	auto group = new QGroupBox(Text(titleKey), this);
	auto form = new QFormLayout(group);

	row.enabled = new QCheckBox(Text("Alerts.Enabled"), group);
	row.enabled->setChecked(settings.enabled);
	form->addRow(QString(), row.enabled);

	row.source = MakeSourceCombo(sources, settings.source, group);
	row.source->setToolTip(Text("Alerts.SourceHint"));
	form->addRow(Text("Alerts.Source"), row.source);

	row.textSource = MakeSourceCombo(textSources, settings.textSource, group);
	row.textSource->setToolTip(Text("Alerts.TextSourceHint"));
	form->addRow(Text("Alerts.TextSource"), row.textSource);

	row.message = new QLineEdit(QString::fromStdString(settings.message), group);
	row.message->setToolTip(Text("Alerts.MessageHint"));
	form->addRow(Text("Alerts.Message"), row.message);

	row.overflow = new QLineEdit(QString::fromStdString(settings.overflowMessage), group);
	row.overflow->setToolTip(Text("Alerts.OverflowHint"));
	form->addRow(Text("Alerts.Overflow"), row.overflow);

	row.status = new QLabel(group);
	row.status->setWordWrap(true);
	auto test = new QPushButton(Text("Alerts.Test"), group);
	auto statusRow = new QHBoxLayout();
	statusRow->addWidget(row.status, 1);
	statusRow->addWidget(test);
	form->addRow(Text("Alerts.Status"), statusRow);

	connect(test, &QPushButton::clicked, this, [this, kind]() {
		controller_->ApplyConfig(Current(), false); // try the values as entered
		controller_->Test(kind);
	});
	return group;
}

AlertsConfig AlertsDialog::Current() const
{
	AlertsConfig config = original_;
	for (AlertKind kind : {AlertKind::Follow, AlertKind::Subscriber}) {
		const Row &row = kind == AlertKind::Follow ? follow_ : subscriber_;
		AlertSettings &settings = config.For(kind);
		settings.enabled = row.enabled->isChecked();
		settings.source = ToStd(row.source->currentText());
		settings.textSource = ToStd(row.textSource->currentText());
		settings.message = ToStd(row.message->text());
		settings.overflowMessage = ToStd(row.overflow->text());
	}
	config.durationSeconds = duration_->value();
	return config;
}

void AlertsDialog::UpdateStatus()
{
	follow_.status->setText(controller_->StatusText(AlertKind::Follow));
	subscriber_.status->setText(controller_->StatusText(AlertKind::Subscriber));
}

void AlertsDialog::accept()
{
	controller_->ApplyConfig(Current(), true);
	QDialog::accept();
}

void AlertsDialog::reject()
{
	controller_->ApplyConfig(original_, false);
	QDialog::reject();
}

} // namespace stream_alerts
