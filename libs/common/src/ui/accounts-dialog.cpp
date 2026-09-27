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

#include "accounts-dialog.hpp"
#include "obs-tools-menu.hpp"
#include "core/text-util.hpp"
#include "net/account-store.hpp"
#include "net/device-login.hpp"

#include <obs-module.h>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace unified_chat {

static QString Text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QString FromStd(const std::string &value)
{
	return QString::fromStdString(value);
}

static std::string ToStd(const QString &value)
{
	return Trim(value.toStdString());
}

namespace {

class AccountsDialog : public QDialog {
public:
	explicit AccountsDialog(QWidget *parent);
	~AccountsDialog() override { login_.reset(); }

	void done(int result) override;

private:
	struct Row {
		QLineEdit *clientId = nullptr;
		QLineEdit *clientSecret = nullptr; // Google only
		QLabel *account = nullptr;
		QPushButton *signIn = nullptr;
		QPushButton *signOut = nullptr;
		bool changed = false;
	};

	QGroupBox *MakeGroup(Service service, const char *titleKey);
	Row &RowFor(Service service) { return service == Service::Twitch ? twitch_ : google_; }
	void StartLogin(Service service);
	void FinishLogin(Service service, const oauth::Token &token, const QString &login, const QString &error);
	void SignOut(Service service);
	void SaveClients();
	void UpdateLabels();

	Accounts accounts_;
	Row twitch_;
	Row google_;
	std::unique_ptr<DeviceLogin> login_;
	Service loginService_ = Service::Twitch;
};

AccountsDialog::AccountsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(Text("Accounts.Title"));
	setMinimumWidth(460);

	SecretReport report;
	accounts_ = SharedAccounts().Load(&report);

	auto layout = new QVBoxLayout(this);
	auto intro = new QLabel(Text("Accounts.Intro"), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	if (report.unreadableSecrets) {
		auto warning = new QLabel(Text("Accounts.Unreadable"), this);
		warning->setWordWrap(true);
		layout->addWidget(warning);
	}
	layout->addWidget(MakeGroup(Service::Twitch, "Accounts.Twitch"));
	layout->addWidget(MakeGroup(Service::Google, "Accounts.Google"));

	auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	UpdateLabels();
}

QGroupBox *AccountsDialog::MakeGroup(Service service, const char *titleKey)
{
	const Account &account = accounts_.Get(service);
	Row &row = RowFor(service);
	auto group = new QGroupBox(Text(titleKey), this);
	auto form = new QFormLayout(group);

	row.clientId = new QLineEdit(FromStd(account.clientId), group);
	row.clientId->setPlaceholderText(Text("Accounts.ClientIdHint"));
	form->addRow(Text("Accounts.ClientId"), row.clientId);
	if (service == Service::Google) {
		row.clientSecret = new QLineEdit(FromStd(account.clientSecret), group);
		row.clientSecret->setEchoMode(QLineEdit::Password);
		form->addRow(Text("Accounts.ClientSecret"), row.clientSecret);
	}

	row.account = new QLabel(group);
	row.account->setWordWrap(true);
	row.account->setTextInteractionFlags(Qt::TextBrowserInteraction);
	row.account->setOpenExternalLinks(true);
	form->addRow(Text("Accounts.Account"), row.account);

	row.signIn = new QPushButton(Text("Accounts.SignIn"), group);
	row.signOut = new QPushButton(Text("Accounts.SignOut"), group);
	auto buttons = new QHBoxLayout();
	buttons->addWidget(row.signIn);
	buttons->addWidget(row.signOut);
	buttons->addStretch();
	form->addRow(QString(), buttons);

	connect(row.signIn, &QPushButton::clicked, this, [this, service]() { StartLogin(service); });
	connect(row.signOut, &QPushButton::clicked, this, [this, service]() { SignOut(service); });
	return group;
}

void AccountsDialog::done(int result)
{
	login_.reset();
	SaveClients();
	if (twitch_.changed || google_.changed)
		NotifyAccountsChanged(twitch_.changed, google_.changed);
	QDialog::done(result);
}

void AccountsDialog::SaveClients()
{
	const std::string twitchId = ToStd(twitch_.clientId->text());
	const std::string googleId = ToStd(google_.clientId->text());
	const std::string googleSecret = ToStd(google_.clientSecret->text());
	const bool twitchEdited = twitchId != accounts_.twitch.clientId;
	const bool googleEdited = googleId != accounts_.google.clientId ||
				  googleSecret != accounts_.google.clientSecret;
	if (!twitchEdited && !googleEdited)
		return;
	SharedAccounts().Update([&](Accounts &stored) {
		stored.twitch.clientId = twitchId;
		stored.google.clientId = googleId;
		stored.google.clientSecret = googleSecret;
	});
	accounts_.twitch.clientId = twitchId;
	accounts_.google.clientId = googleId;
	accounts_.google.clientSecret = googleSecret;
	twitch_.changed |= twitchEdited;
	google_.changed |= googleEdited;
}

void AccountsDialog::UpdateLabels()
{
	const bool busy = login_ != nullptr;
	for (Service service : {Service::Twitch, Service::Google}) {
		const Account &account = accounts_.Get(service);
		Row &row = RowFor(service);
		const bool signedIn = account.token.IsValid();
		if (signedIn)
			row.account->setText(
				service == Service::Twitch && !account.login.empty()
					? Text("Accounts.SignedInAs").arg(FromStd(account.login).toHtmlEscaped())
					: Text("Accounts.SignedIn"));
		else if (!busy || loginService_ != service)
			row.account->setText(Text("Accounts.NotSignedIn"));
		row.signIn->setEnabled(!busy);
		row.signOut->setEnabled(!busy && signedIn);
	}
}

void AccountsDialog::StartLogin(Service service)
{
	SaveClients(); // the new sign-in belongs to the client ID as typed
	oauth::Provider provider;
	if (service == Service::Twitch) {
		provider = oauth::TwitchProvider(accounts_.twitch.clientId);
	} else {
		provider = oauth::GoogleProvider(accounts_.google.clientId, accounts_.google.clientSecret);
		if (provider.clientSecret.empty()) {
			QMessageBox::warning(this, windowTitle(), Text("Accounts.MissingClientSecret"));
			return;
		}
	}
	if (provider.clientId.empty()) {
		QMessageBox::warning(this, windowTitle(), Text("Accounts.MissingClientId"));
		return;
	}

	QLabel *label = RowFor(service).account;
	label->setText(Text("Accounts.Starting"));
	loginService_ = service;

	// The login thread is joined before the dialog goes away, and Qt drops queued calls to a destroyed object.
	DeviceLogin::Callbacks callbacks;
	callbacks.onCode = [this, label](const oauth::DeviceCode &code) {
		QString uri = FromStd(code.verificationUri);
		QString userCode = FromStd(code.userCode);
		QMetaObject::invokeMethod(
			this,
			[label, uri, userCode]() {
				label->setText(Text("Accounts.EnterCode")
						       .arg(uri.toHtmlEscaped(), uri.toHtmlEscaped(),
							    userCode.toHtmlEscaped()));
				QApplication::clipboard()->setText(userCode);
				QDesktopServices::openUrl(QUrl(uri));
			},
			Qt::QueuedConnection);
	};
	callbacks.onFinished = [this, service](const oauth::Token &token, const std::string &login,
					       const std::string &error) {
		QString qlogin = FromStd(login);
		QString qerror = FromStd(error);
		QMetaObject::invokeMethod(
			this, [this, service, token, qlogin, qerror]() { FinishLogin(service, token, qlogin, qerror); },
			Qt::QueuedConnection);
	};

	login_ = std::make_unique<DeviceLogin>(std::move(provider), std::move(callbacks));
	UpdateLabels();
}

void AccountsDialog::FinishLogin(Service service, const oauth::Token &token, const QString &login, const QString &error)
{
	login_.reset();
	if (!error.isEmpty()) {
		UpdateLabels();
		RowFor(service).account->setText(Text("Accounts.LoginFailed").arg(error.toHtmlEscaped()));
		return;
	}

	Account &account = accounts_.Get(service);
	account.token = token;
	if (service == Service::Twitch)
		account.login = login.toStdString();
	SharedAccounts().Update([&](Accounts &stored) {
		Account &target = stored.Get(service);
		target.token = account.token;
		target.login = account.login;
	});
	RowFor(service).changed = true;
	UpdateLabels();
}

void AccountsDialog::SignOut(Service service)
{
	Account &account = accounts_.Get(service);
	account.token = {};
	account.login.clear();
	SharedAccounts().Update([&](Accounts &stored) {
		stored.Get(service).token = {};
		stored.Get(service).login.clear();
	});
	RowFor(service).changed = true;
	UpdateLabels();
}

} // namespace

void OpenAccountsDialog(QWidget *parent)
{
	static QPointer<AccountsDialog> open;
	if (open) {
		open->raise();
		open->activateWindow();
		return;
	}
	AccountsDialog dialog(parent);
	open = &dialog;
	dialog.exec();
}

} // namespace unified_chat
