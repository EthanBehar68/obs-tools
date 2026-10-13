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

#include "alert-template.hpp"
#include "core/template-files.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <vector>

namespace stream_alerts {

static constexpr int kFontSize = 64;
static constexpr int kOutlineSize = 4;
static constexpr int kPictureTextGap = 16; // px between the picture and the text below it

static bool NameTaken(const std::string &name)
{
	obs_source_t *source = obs_get_source_by_name(name.c_str());
	obs_source_release(source);
	return source != nullptr;
}

// A Media Source for a video or sound: restarts each time the group is shown, plays once.
static obs_source_t *CreateMedia(const std::string &name, const std::string &file, bool monitor)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", file.c_str());
	obs_data_set_bool(settings, "restart_on_activate", true);
	obs_data_set_bool(settings, "looping", false);
	obs_source_t *source = obs_source_create("ffmpeg_source", name.c_str(), settings, nullptr);
	obs_data_release(settings);
	if (source && monitor)
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT); // you hear it too
	return source;
}

static obs_source_t *CreatePicture(const std::string &name, const std::string &file)
{
	if (!IsImageFile(file))
		return CreateMedia(name, file, true); // a video, maybe with its own sound
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", file.c_str());
	obs_source_t *source = obs_source_create("image_source", name.c_str(), settings, nullptr);
	obs_data_release(settings);
	return source;
}

static obs_source_t *CreateText(const std::string &name, const std::string &text)
{
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_string(font, "style", "Bold");
	obs_data_set_int(font, "size", kFontSize);
	obs_data_set_int(font, "flags", OBS_FONT_BOLD);
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_data_set_obj(settings, "font", font);
	obs_data_set_int(settings, "color", 0xFFFFFF);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_size", kOutlineSize);
	obs_data_set_int(settings, "outline_color", 0x000000);
	obs_data_set_string(settings, "align", "center");
	obs_data_set_string(settings, "valign", "center");
	obs_source_t *source = obs_source_create("text_gdiplus", name.c_str(), settings, nullptr);
	obs_data_release(settings);
	obs_data_release(font);
	return source;
}

// Adds a source at a position, its alignment point there. Returns the item (owned by the scene).
static obs_sceneitem_t *Place(obs_scene_t *scene, obs_source_t *source, uint32_t alignment, float x, float y)
{
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (!item)
		return nullptr;
	obs_sceneitem_set_alignment(item, alignment);
	const vec2 pos = {x, y};
	obs_sceneitem_set_pos(item, &pos);
	return item;
}

std::optional<CreatedAlert> CreateAlertGroup(const AlertTemplate &alert)
{
	obs_source_t *sceneSource = obs_frontend_preview_program_mode_active()
					    ? obs_frontend_get_current_preview_scene()
					    : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return std::nullopt;
	}

	CreatedAlert created;
	created.group = UniqueName(alert.name, NameTaken);
	created.text = UniqueName(created.group + " Text", NameTaken);

	obs_video_info video = {};
	obs_get_video_info(&video);
	const float centerX = video.base_width / 2.0f;
	float top = video.base_height / 3.0f; // the picture's centre, or the text's top without one

	// Placed in the scene, then grouped: the group keeps where they are on the canvas.
	std::vector<obs_sceneitem_t *> items;
	std::vector<obs_source_t *> sources;
	if (!alert.picture.empty()) {
		if (obs_source_t *picture =
			    CreatePicture(UniqueName(created.group + " Picture", NameTaken), alert.picture)) {
			sources.push_back(picture);
			if (obs_sceneitem_t *item = Place(scene, picture, OBS_ALIGN_CENTER, centerX, top)) {
				items.push_back(item);
				// An image knows its size now; a video only once it plays, so the text goes a little lower.
				const uint32_t height = obs_source_get_height(picture);
				top += (height > 0 ? height / 2.0f : video.base_height / 8.0f) + kPictureTextGap;
			}
		}
	}
	if (obs_source_t *text = CreateText(created.text, alert.sampleText)) {
		sources.push_back(text);
		if (obs_sceneitem_t *item = Place(scene, text, OBS_ALIGN_TOP, centerX, top))
			items.push_back(item);
	}
	if (!alert.sound.empty()) {
		if (obs_source_t *sound =
			    CreateMedia(UniqueName(created.group + " Sound", NameTaken), alert.sound, true)) {
			sources.push_back(sound);
			if (obs_sceneitem_t *item = Place(scene, sound, OBS_ALIGN_CENTER, centerX, top))
				items.push_back(item);
		}
	}

	obs_sceneitem_t *group =
		items.empty() ? nullptr
			      : obs_scene_insert_group2(scene, created.group.c_str(), items.data(), items.size(), true);
	for (obs_source_t *source : sources)
		obs_source_release(source); // the scene holds them now
	if (!group) {
		obs_log(LOG_WARNING, "couldn't create the alert group '%s'", created.group.c_str());
		obs_source_release(sceneSource);
		return std::nullopt;
	}
	obs_sceneitem_set_visible(group, false); // the plugin shows it for each alert
	created.scenes = 1;

	if (alert.everyScene) {
		// The same group in every other scene, where it was placed here, hidden: each copy shows on an alert.
		obs_transform_info transform = {};
		obs_sceneitem_get_info2(group, &transform);
		obs_source_t *groupSource = obs_sceneitem_get_source(group);
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; ++i) {
			obs_scene_t *other = obs_scene_from_source(scenes.sources.array[i]);
			if (!other || other == scene || obs_scene_get_group(other, created.group.c_str()))
				continue;
			if (obs_sceneitem_t *copy = obs_scene_add(other, groupSource)) {
				obs_sceneitem_set_info2(copy, &transform);
				obs_sceneitem_set_visible(copy, false);
				++created.scenes;
			}
		}
		obs_frontend_source_list_free(&scenes);
	}
	obs_source_release(sceneSource);
	obs_log(LOG_INFO, "created the alert group '%s' in %d scene(s)", created.group.c_str(), created.scenes);
	return created;
}

} // namespace stream_alerts
