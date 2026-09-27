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

#include "obs-tools-menu.hpp"
#include "accounts-dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QMainWindow>
#include <QMenu>

namespace unified_chat {

static constexpr const char *kMenuProc = "obs_tools_menu";
static constexpr const char *kMenuProcDecl = "void obs_tools_menu(out ptr menu)";
static constexpr const char *kChangedSignal = "obs_tools_accounts_changed";
static constexpr const char *kChangedSignalDecl = "void obs_tools_accounts_changed(bool twitch, bool google)";

static void PublishMenu(void *data, calldata_t *cd)
{
	calldata_set_ptr(cd, "menu", data);
}

static QMenu *FindPublishedMenu()
{
	calldata_t cd;
	calldata_init(&cd);
	QMenu *menu = nullptr;
	if (proc_handler_call(obs_get_proc_handler(), kMenuProc, &cd))
		menu = static_cast<QMenu *>(calldata_ptr(&cd, "menu"));
	calldata_free(&cd);
	return menu;
}

QMenu *ObsToolsMenu()
{
	static QMenu *menu = [] {
		if (QMenu *found = FindPublishedMenu())
			return found;

		auto mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto action =
			static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Tools.Menu")));
		auto created = new QMenu(mainWindow);
		action->setMenu(created);
		QAction *accounts = created->addAction(QString::fromUtf8(obs_module_text("Tools.Accounts")));
		QObject::connect(accounts, &QAction::triggered, created,
				 [mainWindow]() { OpenAccountsDialog(mainWindow); });
		created->addSeparator();

		proc_handler_add(obs_get_proc_handler(), kMenuProcDecl, PublishMenu, created);
		signal_handler_add(obs_get_signal_handler(), kChangedSignalDecl);
		return created;
	}();
	return menu;
}

void AddObsToolsMenuEntry(const QString &text, std::function<void()> onClick)
{
	QMenu *menu = ObsToolsMenu();
	QObject::connect(menu->addAction(text), &QAction::triggered, menu,
			 [onClick = std::move(onClick)]() { onClick(); });
}

static std::function<void(bool, bool)> &ChangedHandler()
{
	static std::function<void(bool, bool)> handler;
	return handler;
}

static void OnAccountsChanged(void *, calldata_t *cd)
{
	if (ChangedHandler())
		ChangedHandler()(calldata_bool(cd, "twitch"), calldata_bool(cd, "google"));
}

void SetAccountsChangedHandler(std::function<void(bool twitch, bool google)> handler)
{
	ObsToolsMenu(); // declares the signal if this plugin is the first
	ChangedHandler() = std::move(handler);
	signal_handler_connect(obs_get_signal_handler(), kChangedSignal, OnAccountsChanged, nullptr);
}

void ClearAccountsChangedHandler()
{
	signal_handler_disconnect(obs_get_signal_handler(), kChangedSignal, OnAccountsChanged, nullptr);
	ChangedHandler() = nullptr;
}

void NotifyAccountsChanged(bool twitch, bool google)
{
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_bool(&cd, "twitch", twitch);
	calldata_set_bool(&cd, "google", google);
	signal_handler_signal(obs_get_signal_handler(), kChangedSignal, &cd);
	calldata_free(&cd);
}

} // namespace unified_chat
