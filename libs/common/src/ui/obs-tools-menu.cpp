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
#include "core/oauth-device.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

#include <QAction>
#include <QMainWindow>
#include <QMenu>

namespace obs_tools {

static constexpr const char *kMenuProc = "obs_tools_menu";
static constexpr const char *kMenuProcDecl = "void obs_tools_menu(out ptr menu)";
static constexpr const char *kChangedSignal = "obs_tools_accounts_changed";
static constexpr const char *kChangedSignalDecl = "void obs_tools_accounts_changed(bool twitch, bool google)";
static constexpr const char *kAlertSignal = "obs_tools_alert";
static constexpr const char *kAlertSignalDecl = "void obs_tools_alert(bool follow, string name, bool test)";
static constexpr const char *kYouTubeEventSignal = "obs_tools_youtube_event";
static constexpr const char *kYouTubeEventSignalDecl = "void obs_tools_youtube_event(string event)";
// Added after the signals above were published together: it has its own marker, so it's declared even when an older
// plugin declared the others first.
static constexpr const char *kTestAlertSignal = "obs_tools_test_alert";
static constexpr const char *kTestAlertSignalDecl = "void obs_tools_test_alert(string alert)";
static constexpr const char *kTestAlertProc = "obs_tools_test_alert_signal";
static constexpr const char *kTestAlertProcDecl = "void obs_tools_test_alert_signal()";

// libobs warns when a signal is declared twice and can't say whether one exists, so the first plugin to declare
// them also publishes this marker. (1.3.0 / 0.1.0 declared the first two without it: those warn once, harmlessly.)
static constexpr const char *kSignalsProc = "obs_tools_signals";
static constexpr const char *kSignalsProcDecl = "void obs_tools_signals()";

static void DeclareSignals()
{
	static bool declared = false;
	if (declared)
		return;
	declared = true;
	calldata_t cd;
	calldata_init(&cd);
	const bool exists = proc_handler_call(obs_get_proc_handler(), kSignalsProc, &cd);
	calldata_free(&cd);
	if (!exists) {
		proc_handler_add(obs_get_proc_handler(), kSignalsProcDecl, [](void *, calldata_t *) {}, nullptr);
		for (const char *decl : {kChangedSignalDecl, kAlertSignalDecl, kYouTubeEventSignalDecl})
			signal_handler_add(obs_get_signal_handler(), decl);
	}
	calldata_init(&cd);
	const bool testAlertExists = proc_handler_call(obs_get_proc_handler(), kTestAlertProc, &cd);
	calldata_free(&cd);
	if (!testAlertExists) {
		proc_handler_add(obs_get_proc_handler(), kTestAlertProcDecl, [](void *, calldata_t *) {}, nullptr);
		signal_handler_add(obs_get_signal_handler(), kTestAlertSignalDecl);
	}
}

struct KnownPluginEntry {
	std::string name;   // proc suffix, e.g. "unified_chat"
	std::string module; // DLL name, e.g. "obs-unified-chat"
};

// The plugins released before the handshake (Unified Chat 1.3.0, Stream Alerts 0.1.0) can't register. They're
// found by module name so an outdated install is flagged. Newer plugins register instead and don't go here.
struct KnownPlugin {
	const char *name;
	const char *module;
};
static constexpr KnownPlugin kPreHandshakePlugins[] = {{"unified_chat", "obs-unified-chat"},
						       {"stream_alerts", "obs-stream-alerts"}};

// The registry: every OBS Tools plugin adds itself, so one added later is found without changing the others. The
// first plugin to register owns the list; the others reach it through these procs.
static constexpr const char *kRegisterProc = "obs_tools_register";
static constexpr const char *kRegisterProcDecl = "void obs_tools_register(in string name, in string module)";
static constexpr const char *kPluginsProc = "obs_tools_plugins";
static constexpr const char *kPluginsProcDecl = "void obs_tools_plugins(out string plugins)";

static std::vector<KnownPluginEntry> &Registry()
{
	static std::vector<KnownPluginEntry> registry;
	return registry;
}

static void RegisterProc(void *, calldata_t *cd)
{
	const char *name = calldata_string(cd, "name");
	const char *module = calldata_string(cd, "module");
	if (name && module)
		Registry().push_back({name, module});
}

static void PluginsProc(void *, calldata_t *cd)
{
	std::string list; // "name module;name module"
	for (const auto &entry : Registry())
		list += (list.empty() ? "" : ";") + entry.name + " " + entry.module;
	calldata_set_string(cd, "plugins", list.c_str());
}

static std::vector<KnownPluginEntry> RegisteredPlugins()
{
	std::vector<KnownPluginEntry> plugins;
	calldata_t cd;
	calldata_init(&cd);
	if (proc_handler_call(obs_get_proc_handler(), kPluginsProc, &cd)) {
		const char *text = calldata_string(&cd, "plugins");
		std::string list = text ? text : "";
		size_t at = 0;
		while (at < list.size()) {
			size_t end = list.find(';', at);
			if (end == std::string::npos)
				end = list.size();
			const std::string entry = list.substr(at, end - at);
			at = end + 1;
			const size_t space = entry.find(' ');
			if (space != std::string::npos)
				plugins.push_back({entry.substr(0, space), entry.substr(space + 1)});
		}
	}
	calldata_free(&cd);
	return plugins;
}

struct PluginInfo {
	std::string name;
	std::string version;
	long long protocol = 0;
	long long minProtocol = 0; // the oldest protocol it works with
	std::string twitchScopes;
};
static PluginInfo &OwnInfo()
{
	static PluginInfo info;
	return info;
}

static void PublishInfo(void *, calldata_t *cd)
{
	const PluginInfo &info = OwnInfo();
	calldata_set_string(cd, "version", info.version.c_str());
	calldata_set_int(cd, "protocol", info.protocol);
	calldata_set_int(cd, "min_protocol", info.minProtocol);
	calldata_set_string(cd, "twitch_scopes", info.twitchScopes.c_str());
}

void RegisterObsToolsPlugin(const char *name, const char *module, const char *version)
{
	PluginInfo &info = OwnInfo();
	info = {name, version, kObsToolsProtocol, kObsToolsMinProtocol, oauth::TwitchProvider({}).scope};
	const std::string decl =
		std::string("void obs_tools_plugin_") + name +
		"(out string version, out int protocol, out int min_protocol, out string twitch_scopes)";
	proc_handler_add(obs_get_proc_handler(), decl.c_str(), PublishInfo, nullptr);

	calldata_t cd;
	calldata_init(&cd);
	if (!proc_handler_call(obs_get_proc_handler(), kPluginsProc, &cd)) {
		proc_handler_add(obs_get_proc_handler(), kRegisterProcDecl, RegisterProc, nullptr);
		proc_handler_add(obs_get_proc_handler(), kPluginsProcDecl, PluginsProc, nullptr);
	}
	calldata_free(&cd);
	calldata_init(&cd);
	calldata_set_string(&cd, "name", name);
	calldata_set_string(&cd, "module", module);
	proc_handler_call(obs_get_proc_handler(), kRegisterProc, &cd);
	calldata_free(&cd);
	DeclareSignals();
}

struct Peer {
	std::string module;
	bool answered = false; // false: loaded, but a release from before the handshake
	PluginInfo info;
};
static std::vector<Peer> Peers()
{
	std::vector<KnownPluginEntry> candidates = RegisteredPlugins();
	for (const KnownPlugin &known : kPreHandshakePlugins) {
		auto same = [&](const KnownPluginEntry &entry) {
			return entry.name == known.name;
		};
		if (std::none_of(candidates.begin(), candidates.end(), same) && obs_get_module(known.module))
			candidates.push_back({known.name, known.module});
	}
	std::vector<Peer> peers;
	for (const auto &candidate : candidates) {
		if (candidate.name == OwnInfo().name)
			continue;
		Peer peer;
		peer.module = candidate.module;
		calldata_t cd;
		calldata_init(&cd);
		if (proc_handler_call(obs_get_proc_handler(), ("obs_tools_plugin_" + candidate.name).c_str(), &cd)) {
			peer.answered = true;
			const char *version = calldata_string(&cd, "version");
			const char *scopes = calldata_string(&cd, "twitch_scopes");
			const long long protocol = calldata_int(&cd, "protocol");
			long long minProtocol = 0;
			if (!calldata_get_int(&cd, "min_protocol", &minProtocol))
				minProtocol = protocol; // a release from before min_protocol: only its own protocol
			peer.info = {candidate.name, version ? version : "", protocol, minProtocol,
				     scopes ? scopes : ""};
		}
		calldata_free(&cd);
		peers.push_back(std::move(peer));
	}
	return peers;
}

// Each side must speak at least the other's oldest supported protocol.
static bool PeerTooOld(const Peer &peer)
{
	return !peer.answered || peer.info.protocol < kObsToolsMinProtocol;
}
static bool PeerTooNew(const Peer &peer)
{
	return peer.answered && peer.info.minProtocol > kObsToolsProtocol;
}

void CheckObsToolsPlugins()
{
	for (const Peer &peer : Peers()) {
		if (!peer.answered)
			obs_log(LOG_WARNING, "OBS Tools: %s is from an older release; update it to match this one",
				peer.module.c_str());
		else if (PeerTooOld(peer) || PeerTooNew(peer))
			obs_log(LOG_WARNING, "OBS Tools: %s %s uses protocol %lld, this plugin %lld: update both",
				peer.module.c_str(), peer.info.version.c_str(), peer.info.protocol, kObsToolsProtocol);
		else
			obs_log(LOG_INFO, "OBS Tools: %s %s loaded too", peer.module.c_str(),
				peer.info.version.c_str());
	}
}

std::string ObsToolsVersionWarning()
{
	for (const Peer &peer : Peers()) {
		if (PeerTooOld(peer))
			return peer.module +
			       " is older than this plugin. Update it too, or a sign-in may miss permissions "
			       "and some events won't arrive.";
		if (PeerTooNew(peer))
			return std::string("Another OBS Tools plugin (") + peer.module +
			       ") is newer than this one. Update every OBS Tools plugin to the same release.";
	}
	return {};
}

std::string AllTwitchScopes(const std::string &own)
{
	std::string scopes = own;
	for (const Peer &peer : Peers())
		scopes = oauth::MergeScopes(scopes, peer.info.twitchScopes);
	return scopes;
}

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
		DeclareSignals();
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
	DeclareSignals();
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
	DeclareSignals();
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_bool(&cd, "twitch", twitch);
	calldata_set_bool(&cd, "google", google);
	signal_handler_signal(obs_get_signal_handler(), kChangedSignal, &cd);
	calldata_free(&cd);
}

static std::function<void(bool, const std::string &, bool)> &AlertHandler()
{
	static std::function<void(bool, const std::string &, bool)> handler;
	return handler;
}

static void OnAlert(void *, calldata_t *cd)
{
	const char *name = calldata_string(cd, "name");
	if (AlertHandler())
		AlertHandler()(calldata_bool(cd, "follow"), name ? name : "", calldata_bool(cd, "test"));
}

void SetAlertHandler(std::function<void(bool follow, const std::string &name, bool test)> handler)
{
	DeclareSignals();
	AlertHandler() = std::move(handler);
	signal_handler_connect(obs_get_signal_handler(), kAlertSignal, OnAlert, nullptr);
}

void ClearAlertHandler()
{
	signal_handler_disconnect(obs_get_signal_handler(), kAlertSignal, OnAlert, nullptr);
	AlertHandler() = nullptr;
}

void NotifyAlert(bool follow, const std::string &name, bool test)
{
	DeclareSignals();
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_bool(&cd, "follow", follow);
	calldata_set_string(&cd, "name", name.c_str());
	calldata_set_bool(&cd, "test", test);
	signal_handler_signal(obs_get_signal_handler(), kAlertSignal, &cd);
	calldata_free(&cd);
}

static std::function<void(const std::string &)> &YouTubeEventHandler()
{
	static std::function<void(const std::string &)> handler;
	return handler;
}

static void OnYouTubeEvent(void *, calldata_t *cd)
{
	const char *event = calldata_string(cd, "event");
	if (YouTubeEventHandler() && event)
		YouTubeEventHandler()(event);
}

void SetYouTubeEventHandler(std::function<void(const std::string &eventJson)> handler)
{
	DeclareSignals();
	YouTubeEventHandler() = std::move(handler);
	signal_handler_connect(obs_get_signal_handler(), kYouTubeEventSignal, OnYouTubeEvent, nullptr);
}

void ClearYouTubeEventHandler()
{
	signal_handler_disconnect(obs_get_signal_handler(), kYouTubeEventSignal, OnYouTubeEvent, nullptr);
	YouTubeEventHandler() = nullptr;
}

void NotifyYouTubeEvent(const std::string &eventJson)
{
	DeclareSignals();
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "event", eventJson.c_str());
	signal_handler_signal(obs_get_signal_handler(), kYouTubeEventSignal, &cd);
	calldata_free(&cd);
}

static std::function<void(const std::string &)> &TestAlertHandler()
{
	static std::function<void(const std::string &)> handler;
	return handler;
}

static void OnTestAlert(void *, calldata_t *cd)
{
	const char *alert = calldata_string(cd, "alert");
	if (TestAlertHandler() && alert)
		TestAlertHandler()(alert);
}

void SetTestAlertHandler(std::function<void(const std::string &alertJson)> handler)
{
	DeclareSignals();
	TestAlertHandler() = std::move(handler);
	signal_handler_connect(obs_get_signal_handler(), kTestAlertSignal, OnTestAlert, nullptr);
}

void ClearTestAlertHandler()
{
	signal_handler_disconnect(obs_get_signal_handler(), kTestAlertSignal, OnTestAlert, nullptr);
	TestAlertHandler() = nullptr;
}

void NotifyTestAlert(const std::string &alertJson)
{
	DeclareSignals();
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "alert", alertJson.c_str());
	signal_handler_signal(obs_get_signal_handler(), kTestAlertSignal, &cd);
	calldata_free(&cd);
}

} // namespace obs_tools
