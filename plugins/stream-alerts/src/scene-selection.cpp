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

#include "scene-selection.hpp"

#include <obs-frontend-api.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace stream_alerts {

bool IsTextSource(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	return id && std::strncmp(id, "text_", 5) == 0; // text_gdiplus, text_ft2_source
}

// Holds a reference to each item until it goes out of scope.
struct Items {
	std::vector<obs_sceneitem_t *> list;
	~Items()
	{
		for (obs_sceneitem_t *item : list)
			obs_sceneitem_release(item);
	}
};

static bool CollectItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	obs_sceneitem_addref(item);
	static_cast<Items *>(param)->list.push_back(item);
	return true;
}

// OBS enumerates from the bottom of the Sources list; this puts them in the order the list shows.
static void TopDown(Items &items)
{
	std::reverse(items.list.begin(), items.list.end());
}

// The first selected item from the top, looking inside groups after the group itself. Returns a new reference.
static obs_sceneitem_t *FindSelected(Items &items)
{
	TopDown(items);
	for (obs_sceneitem_t *item : items.list) {
		if (obs_sceneitem_selected(item)) {
			obs_sceneitem_addref(item);
			return item;
		}
		if (obs_sceneitem_is_group(item)) {
			Items inside;
			obs_sceneitem_group_enum_items(item, CollectItem, &inside);
			if (obs_sceneitem_t *found = FindSelected(inside))
				return found;
		}
	}
	return nullptr;
}

// The Text sources in a group or scene, groups inside it included. Stops counting past two.
static void FindTextSources(obs_scene_t *scene, std::vector<std::string> &names)
{
	Items items;
	obs_scene_enum_items(scene, CollectItem, &items);
	TopDown(items);
	for (obs_sceneitem_t *item : items.list) {
		if (names.size() > 1)
			return;
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (IsTextSource(source))
			names.emplace_back(obs_source_get_name(source));
		else if (obs_sceneitem_is_group(item))
			FindTextSources(obs_sceneitem_group_get_scene(item), names);
	}
}

std::optional<SelectedSource> GetSelectedSource()
{
	obs_source_t *sceneSource = obs_frontend_preview_program_mode_active()
					    ? obs_frontend_get_current_preview_scene()
					    : obs_frontend_get_current_scene();
	if (!sceneSource)
		return std::nullopt;
	Items items;
	obs_scene_enum_items(obs_scene_from_source(sceneSource), CollectItem, &items);
	obs_sceneitem_t *item = FindSelected(items);
	obs_source_release(sceneSource);
	if (!item)
		return std::nullopt;

	std::optional<SelectedSource> selected;
	obs_source_t *source = obs_sceneitem_get_source(item);
	const char *name = obs_source_get_name(source);
	if (name && *name) {
		selected = SelectedSource{name, IsTextSource(source), {}};
		if (obs_scene_t *inside = obs_group_or_scene_from_source(source)) {
			std::vector<std::string> texts;
			FindTextSources(inside, texts);
			if (texts.size() == 1)
				selected->textInside = texts.front();
		}
	}
	obs_sceneitem_release(item);
	return selected;
}

} // namespace stream_alerts
