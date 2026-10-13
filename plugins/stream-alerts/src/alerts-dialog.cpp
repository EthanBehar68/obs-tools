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
#include "scene-selection.hpp"
#include "ui/accounts-dialog.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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
	if (IsTextSource(source))
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

// The combo with a "Use selected" button after it, which takes the item selected in OBS's Sources list.
static QHBoxLayout *WithUseSelected(QComboBox *combo, const char *hintKey, QWidget *parent, QPushButton *&button)
{
	auto row = new QHBoxLayout();
	row->addWidget(combo, 1);
	button = new QPushButton(Text("Alerts.UseSelected"), parent);
	button->setToolTip(Text(hintKey));
	row->addWidget(button);
	return row;
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

	QPushButton *useSource = nullptr;
	QPushButton *useText = nullptr;
	page.source = MakeSourceCombo(sources, settings.source, widget);
	page.source->setToolTip(Text("Alerts.SourceHint"));
	form->addRow(Text("Alerts.Source"), WithUseSelected(page.source, "Alerts.UseSelectedHint", widget, useSource));
	connect(useSource, &QPushButton::clicked, this, [this, kind]() { FillFromSelection(kind, false); });

	page.textSource = MakeSourceCombo(textSources, settings.textSource, widget);
	page.textSource->setToolTip(Text("Alerts.TextSourceHint"));
	form->addRow(Text("Alerts.TextSource"),
		     WithUseSelected(page.textSource, "Alerts.UseSelectedTextHint", widget, useText));
	connect(useText, &QPushButton::clicked, this, [this, kind]() { FillFromSelection(kind, true); });

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
	auto placeholders = new QLabel(Text(("Placeholders." + key).c_str()), widget);
	placeholders->setWordWrap(true);
	form->addRow(QString(), placeholders);
	if (!IsPriority(kind)) {
		page.overflow = MakeMessageEdit(settings.overflowMessage, widget);
		form->addRow(Text("Alerts.Overflow"), page.overflow);
		auto overflowHelp = new QLabel(Text("Alerts.OverflowPlaceholders"), widget);
		overflowHelp->setWordWrap(true);
		form->addRow(QString(), overflowHelp);
	}

	page.warning = new QLabel(widget);
	page.warning->setWordWrap(true);
	page.warning->setProperty("class", "text-danger"); // OBS themes' error colour
	page.warning->hide();
	form->addRow(QString(), page.warning);
	auto showProblems = [this, kind]() {
		const QString problems = Problems(kind);
		pages_[kind].warning->setText(problems);
		pages_[kind].warning->setVisible(!problems.isEmpty());
	};
	for (QPlainTextEdit *edit : {page.message, page.variant, page.overflow}) {
		if (edit)
			connect(edit, &QPlainTextEdit::textChanged, this, showProblems);
	}
	showProblems(); // a message saved before this check existed

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
			if (!CheckMessages(&kind))
				return;
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

QString AlertsDialog::Problems(AlertKind kind) const
{
	const Page &page = pages_.at(kind);
	auto join = [](const std::vector<std::string> &items) {
		QStringList list;
		for (const auto &item : items)
			list.append(QString::fromStdString(item));
		return list.join(", ");
	};
	QStringList lines;
	auto check = [&](const QPlainTextEdit *edit, const QString &label, bool overflow) {
		if (!edit)
			return;
		const auto unusable = UnusablePlaceholders(ToStd(edit->toPlainText()), kind, overflow);
		if (!unusable.empty())
			lines.append(Text("Alerts.Unusable")
					     .arg(label, join(unusable), join(UsablePlaceholders(kind, overflow))));
	};
	check(page.message, Text("Alerts.Message"), false);
	check(page.variant,
	      Text(kind == AlertKind::TwitchSub ? "Alerts.Resub"
		   : kind == AlertKind::Member  ? "Alerts.Milestone"
						: "Alerts.Several"),
	      false);
	check(page.overflow, Text("Alerts.Overflow"), true);
	return lines.join("\n");
}

bool AlertsDialog::CheckMessages(const AlertKind *only)
{
	for (size_t row = 0; row < std::size(kAllKinds); ++row) {
		const AlertKind kind = kAllKinds[row];
		if (only && kind != *only)
			continue;
		const QString problems = Problems(kind);
		if (problems.isEmpty())
			continue;
		kinds_->setCurrentRow((int)row);
		QMessageBox::warning(this, Text("Alerts.UnusableTitle"),
				     Text(("Kind." + std::string(KindKey(kind))).c_str()) + "\n\n" + problems);
		return false;
	}
	return true;
}

bool AlertsDialog::FillFromSelection(AlertKind kind, bool text)
{
	const auto selected = GetSelectedSource();
	if (!selected) {
		QMessageBox::information(this, Text("Alerts.UseSelected"), Text("Alerts.NothingSelected"));
		return false;
	}
	Page &page = pages_[kind];
	const QString name = QString::fromStdString(selected->name);
	if (text) {
		if (!selected->isText) {
			QMessageBox::information(this, Text("Alerts.UseSelected"), Text("Alerts.NotText").arg(name));
			return false;
		}
		page.textSource->setCurrentText(name);
		return true;
	}
	page.source->setCurrentText(name);
	if (!selected->textInside.empty()) // a group with its own text: that's the one the alert writes into
		page.textSource->setCurrentText(QString::fromStdString(selected->textInside));
	return true;
}

void AlertsDialog::UseSelected(AlertKind kind)
{
	const auto row = std::find(std::begin(kAllKinds), std::end(kAllKinds), kind) - std::begin(kAllKinds);
	kinds_->setCurrentRow((int)row);
	if (FillFromSelection(kind, false))
		pages_[kind].enabled->setChecked(true);
}

void AlertsDialog::UpdateStatus()
{
	for (auto &[kind, page] : pages_)
		page.status->setText(controller_->StatusText(kind));
}

void AlertsDialog::accept()
{
	if (!CheckMessages())
		return;
	controller_->ApplyConfig(Current(), true);
	QDialog::accept();
}

void AlertsDialog::reject()
{
	controller_->ApplyConfig(original_, false);
	QDialog::reject();
}

} // namespace stream_alerts
