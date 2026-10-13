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
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
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

// A few lines tall: Enter starts a new line of the alert text, Tab moves on.
static QPlainTextEdit *MakeMessageEdit(const std::string &text, QWidget *parent)
{
	auto edit = new QPlainTextEdit(QString::fromStdString(text), parent);
	edit->setTabChangesFocus(true);
	edit->setFixedHeight(edit->fontMetrics().lineSpacing() * 3 + 12);
	return edit;
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
	setMinimumWidth(720);

	const SourceNames names = ListSources();
	auto layout = new QVBoxLayout(this);
	auto intro = new QLabel(Text("Alerts.Intro"), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	// The kinds on the left, the selected one's settings on the right: nine stacked groups would be far too tall.
	auto middle = new QHBoxLayout();
	kinds_ = new QListWidget(this);
	auto pages = new QStackedWidget(this);
	for (AlertKind kind : kAllKinds) {
		kinds_->addItem(Text((std::string("Kind.") + KindKey(kind)).c_str()));
		pages->addWidget(MakePage(kind, names.all, names.text));
	}
	kinds_->setFixedWidth(kinds_->sizeHintForColumn(0) + 24);
	connect(kinds_, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
	kinds_->setCurrentRow(0);
	middle->addWidget(kinds_);
	middle->addWidget(pages, 1);
	layout->addLayout(middle);

	auto general = new QFormLayout();
	duration_ = new QSpinBox(this);
	duration_->setRange(1, 60);
	duration_->setSuffix(" s");
	duration_->setValue(original_.durationSeconds);
	general->addRow(Text("Alerts.Duration"), duration_);
	layout->addLayout(general);

	auto bottom = new QHBoxLayout();
	auto accounts = new QPushButton(Text("Alerts.Accounts"), this);
	connect(accounts, &QPushButton::clicked, this, [this]() { obs_tools::OpenAccountsDialog(this); });
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

// The minimum setting on a kind's page: bits, raid viewers or Jewels.
static int &Minimum(AlertsConfig &config, AlertKind kind)
{
	return kind == AlertKind::Cheer  ? config.minBits
	       : kind == AlertKind::Raid ? config.minRaidViewers
					 : config.minJewels;
}
static int Minimum(const AlertsConfig &config, AlertKind kind)
{
	return Minimum(const_cast<AlertsConfig &>(config), kind);
}

QWidget *AlertsDialog::MakePage(AlertKind kind, const QStringList &sources, const QStringList &textSources)
{
	const AlertSettings &settings = original_.For(kind);
	const std::string key = KindKey(kind);
	Page &page = pages_[kind];
	auto widget = new QWidget(this);
	auto form = new QFormLayout(widget);
	form->setContentsMargins(0, 0, 0, 0);

	auto about = new QLabel(Text(("About." + key).c_str()), widget);
	about->setWordWrap(true);
	form->addRow(about);

	page.enabled = new QCheckBox(Text("Alerts.Enabled"), widget);
	page.enabled->setChecked(settings.enabled);
	form->addRow(QString(), page.enabled);

	page.source = MakeSourceCombo(sources, settings.source, widget);
	page.source->setToolTip(Text("Alerts.SourceHint"));
	form->addRow(Text("Alerts.Source"), page.source);

	page.textSource = MakeSourceCombo(textSources, settings.textSource, widget);
	page.textSource->setToolTip(Text("Alerts.TextSourceHint"));
	form->addRow(Text("Alerts.TextSource"), page.textSource);

	page.message = MakeMessageEdit(settings.message, widget);
	form->addRow(Text("Alerts.Message"), page.message);

	const bool hasVariant = kind == AlertKind::TwitchSub || kind == AlertKind::Member || kind == AlertKind::Jewels;
	const char *variantName = kind == AlertKind::TwitchSub ? "Resub"
				  : kind == AlertKind::Member  ? "Milestone"
							       : "Several";
	if (hasVariant) {
		page.variant = MakeMessageEdit(settings.variantMessage, widget);
		form->addRow(Text((std::string("Alerts.") + variantName).c_str()), page.variant);
	}
	if (!IsPriority(kind)) {
		page.overflow = MakeMessageEdit(settings.overflowMessage, widget);
		page.overflow->setToolTip(Text("Alerts.OverflowHint"));
		form->addRow(Text("Alerts.Overflow"), page.overflow);
	}
	auto placeholders = new QLabel(Text(("Placeholders." + key).c_str()), widget);
	placeholders->setWordWrap(true);
	form->addRow(QString(), placeholders);

	if (kind == AlertKind::Cheer || kind == AlertKind::Raid || kind == AlertKind::Jewels) {
		page.minimum = new QSpinBox(widget);
		page.minimum->setRange(1, 1000000);
		page.minimum->setValue(Minimum(original_, kind));
		page.minimum->setSuffix(Text(kind == AlertKind::Cheer  ? "Alerts.BitsSuffix"
					     : kind == AlertKind::Raid ? "Alerts.ViewersSuffix"
								       : "Alerts.JewelsSuffix"));
		form->addRow(Text("Alerts.Minimum"), page.minimum);
	}

	if (kind == AlertKind::Subscriber) {
		page.interval = new QSpinBox(widget);
		page.interval->setRange(kMinSubscriberCheckSeconds, kMaxSubscriberCheckSeconds);
		page.interval->setSingleStep(30);
		page.interval->setSuffix(" s");
		page.interval->setValue(original_.subscriberCheckSeconds);
		auto cost = new QLabel(widget);
		auto showCost = [cost](int seconds) {
			cost->setText(Text("Alerts.CheckCost").arg((3600 + seconds - 1) / seconds)); // 1 unit per check
		};
		showCost(page.interval->value());
		connect(page.interval, &QSpinBox::valueChanged, cost, showCost);
		auto row = new QHBoxLayout();
		row->addWidget(page.interval);
		row->addWidget(cost, 1);
		form->addRow(Text("Alerts.CheckEvery"), row);
	}

	page.status = new QLabel(widget);
	page.status->setWordWrap(true);
	form->addRow(Text("Alerts.Status"), page.status);

	auto tests = new QHBoxLayout();
	auto addTest = [&](const char *labelKey, bool variant) {
		auto test = new QPushButton(Text(labelKey), widget);
		connect(test, &QPushButton::clicked, this, [this, kind, variant]() {
			controller_->ApplyConfig(Current(), false); // try the values as entered
			controller_->Test(kind, variant);
		});
		tests->addWidget(test);
	};
	addTest("Alerts.Test", false);
	if (hasVariant)
		addTest((std::string("Alerts.Test") + variantName).c_str(), true);
	tests->addStretch();
	form->addRow(QString(), tests);
	return widget;
}

AlertsConfig AlertsDialog::Current() const
{
	AlertsConfig config = original_;
	for (const auto &[kind, page] : pages_) {
		AlertSettings &settings = config.For(kind);
		settings.enabled = page.enabled->isChecked();
		settings.source = ToStd(page.source->currentText());
		settings.textSource = ToStd(page.textSource->currentText());
		settings.message = ToStd(page.message->toPlainText());
		if (page.overflow)
			settings.overflowMessage = ToStd(page.overflow->toPlainText());
		if (page.variant)
			settings.variantMessage = ToStd(page.variant->toPlainText());
		if (page.minimum)
			Minimum(config, kind) = page.minimum->value();
		if (page.interval)
			config.subscriberCheckSeconds = page.interval->value();
	}
	config.durationSeconds = duration_->value();
	return config;
}

void AlertsDialog::UpdateStatus()
{
	for (auto &[kind, page] : pages_)
		page.status->setText(controller_->StatusText(kind));
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
