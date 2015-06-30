/******************************************************************************
    Copyright (C) 2015 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>
#include <QMessageBox>
#include <QVariant>
#include "window-basic-main.hpp"
#include "window-namedialog.hpp"
#include "qt-wrappers.hpp"

static void EnumProfiles(
		std::function<bool(const char *name, const char *path)> cb)
{
	char path[512];
	os_glob_t *glob;

	int ret = GetConfigPath(path, sizeof(path),
			"obs-studio/basic/profiles/*");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return;
	}

	if (os_glob(path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob profiles");
		return;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *file_path = glob->gl_pathv[i].path;
		const char *dir_name = strrchr(file_path, '/') + 1;

		if (!glob->gl_pathv[i].directory)
			continue;

		if (strcmp(dir_name,  ".") == 0 ||
		    strcmp(dir_name, "..") == 0)
			continue;

		std::string file = file_path;
		file += "/basic.ini";

		ConfigFile config;
		int ret = config.Open(file.c_str(), CONFIG_OPEN_EXISTING);
		if (ret != CONFIG_SUCCESS)
			continue;

		const char *name = config_get_string(config, "General", "Name");
		if (!name)
			name = strrchr(file_path, '/') + 1;

		if (!cb(name, file_path))
			break;
	}

	os_globfree(glob);
}

static bool ProfileExists(const char *find_name)
{
	bool found = false;
	auto func = [&](const char *name, const char *path)
	{
		if (strcmp(name, find_name) == 0) {
			found = true;
			return false;
		}

		UNUSED_PARAMETER(path);
		return true;
	};

	EnumProfiles(func);
	return found;
}

static bool GetProfileName(QWidget *parent, std::string &name,
		std::string &file, const char *title, const char *text,
		const char *old_name = nullptr)
{
	char path[512];
	int ret;

	for (;;) {
		bool success = NameDialog::AskForName(parent, title, text,
				name, QT_UTF8(old_name));
		if (!success) {
			return false;
		}
		if (name.empty()) {
			QMessageBox::information(parent,
					QTStr("NoNameEntered.Title"),
					QTStr("NoNameEntered.Text"));
			continue;
		}
		if (ProfileExists(name.c_str())) {
			QMessageBox::information(parent,
					QTStr("NameExists.Title"),
					QTStr("NameExists.Text"));
			continue;
		}
		break;
	}

	if (!GetFileSafeName(name.c_str(), file)) {
		blog(LOG_WARNING, "Failed to create safe file name for '%s'",
				name.c_str());
		return false;
	}

	ret = GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return false;
	}

	file.insert(0, path);

	if (!GetClosestUnusedFileName(file, nullptr)) {
		blog(LOG_WARNING, "Failed to get closest file name for %s",
				file.c_str());
		return false;
	}

	file.erase(0, ret);
	return true;
}

static bool CopyProfile(const char *from_partial, const char *to)
{
	os_glob_t *glob;
	char path[512];
	int ret;

	ret = GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return false;
	}

	strcat(path, from_partial);
	strcat(path, "/*");

	if (os_glob(path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob profile '%s'", from_partial);
		return false;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *file_path = glob->gl_pathv[i].path;
		if (glob->gl_pathv[i].directory)
			continue;

		ret = snprintf(path, sizeof(path), "%s/%s",
				to, strrchr(file_path, '/') + 1);
		if (ret > 0) {
			if (os_copyfile(file_path, path) != 0) {
				blog(LOG_WARNING, "CopyProfile: Failed to "
				                  "copy file %s to %s",
				                  file_path, path);
			}
		}
	}

	os_globfree(glob);

	return true;
}

bool OBSBasic::AddProfile(bool create_new, const char *title, const char *text,
		const char *init_text)
{
	std::string new_name;
	std::string new_dir;
	ConfigFile config;

	if (!GetProfileName(this, new_name, new_dir, title, text, init_text))
		return false;

	std::string cur_dir = config_get_string(App()->GlobalConfig(),
			"Basic", "ProfileDir");

	char new_path[512];
	int ret = GetConfigPath(new_path, 512, "obs-studio/basic/profiles/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return false;
	}

	strcat(new_path, new_dir.c_str());

	if (os_mkdir(new_path) < 0) {
		blog(LOG_WARNING, "Failed to create profile directory '%s'",
				new_dir.c_str());
		return false;
	}

	if (!create_new)
		CopyProfile(cur_dir.c_str(), new_path);

	strcat(new_path, "/basic.ini");

	if (config.Open(new_path, CONFIG_OPEN_ALWAYS) != 0) {
		blog(LOG_ERROR, "Failed to open new config file '%s'",
				new_dir.c_str());
		return false;
	}

	config_set_string(App()->GlobalConfig(), "Basic", "Profile",
			new_name.c_str());
	config_set_string(App()->GlobalConfig(), "Basic", "ProfileDir",
			new_dir.c_str());

	config_set_string(config, "General", "Name", new_name.c_str());
	config.Save();
	config.Swap(basicConfig);
	InitBasicConfigDefaults();
	RefreshProfiles();

	if (create_new)
		ResetProfileData();

	blog(LOG_INFO, "------------------------------------------------");
	blog(LOG_INFO, "Created profile '%s' (%s)", new_name.c_str(),
			create_new ? "clean" : "duplicate");

	config_save(App()->GlobalConfig());
	return true;
}

void OBSBasic::DeleteProfile(const char *profile_dir)
{
	char profile_path[512];
	char base_path[512];

	int ret = GetConfigPath(base_path, 512, "obs-studio/basic/profiles");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get profiles config path");
		return;
	}

	ret = snprintf(profile_path, 512, "%s/%s/*", base_path, profile_dir);
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get path for profile dir '%s'",
				profile_dir);
		return;
	}

	os_glob_t *glob;
	if (os_glob(profile_path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob profile dir '%s'",
				profile_dir);
		return;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *file_path = glob->gl_pathv[i].path;

		if (glob->gl_pathv[i].directory)
			continue;

		os_unlink(file_path);
	}

	os_globfree(glob);

	ret = snprintf(profile_path, 512, "%s/%s", base_path, profile_dir);
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get path for profile dir '%s'",
				profile_dir);
		return;
	}

	os_rmdir(profile_path);
}

void OBSBasic::RefreshProfiles()
{
	QList<QAction*> menuActions = ui->profileMenu->actions();
	int count = 0;

	for (int i = 0; i < menuActions.count(); i++) {
		QVariant v = menuActions[i]->property("file_name");
		if (v.typeName() != nullptr)
			delete menuActions[i];
	}

	const char *cur_name = config_get_string(App()->GlobalConfig(),
			"Basic", "Profile");

	auto addProfile = [&](const char *name, const char *path)
	{
		std::string file = strrchr(path, '/') + 1;

		QAction *action = new QAction(QT_UTF8(name), this);
		action->setProperty("file_name", QT_UTF8(path));
		connect(action, &QAction::triggered,
				this, &OBSBasic::ChangeProfile);
		action->setCheckable(true);

		action->setChecked(strcmp(name, cur_name) == 0);

		ui->profileMenu->addAction(action);
		count++;
		return true;
	};

	EnumProfiles(addProfile);

	ui->actionRemoveProfile->setEnabled(count > 1);
}

void OBSBasic::ResetProfileData()
{
	ResetVideo();
	service = nullptr;
	InitService();
	ResetOutputs();
	ClearHotkeys();
	CreateHotkeys();
}

void OBSBasic::on_actionNewProfile_triggered()
{
	AddProfile(true, Str("AddProfile.Title"), Str("AddProfile.Text"));
}

void OBSBasic::on_actionDupProfile_triggered()
{
	AddProfile(false, Str("AddProfile.Title"), Str("AddProfile.Text"));
}

void OBSBasic::on_actionRenameProfile_triggered()
{
	std::string cur_dir = config_get_string(App()->GlobalConfig(),
			"Basic", "ProfileDir");
	const char *cur_name = config_get_string(App()->GlobalConfig(),
			"Basic", "Profile");

	/* Duplicate and delete in case there are any issues in the process */
	bool success = AddProfile(false, Str("RenameProfile.Title"),
			Str("RenameProfile.Text"), cur_name);
	if (success) {
		DeleteProfile(cur_dir.c_str());
		RefreshProfiles();
	}
}

void OBSBasic::on_actionRemoveProfile_triggered()
{
	std::string new_name;
	std::string new_path;
	ConfigFile config;

	std::string old_dir = config_get_string(App()->GlobalConfig(),
			"Basic", "ProfileDir");
	const char *old_name = config_get_string(App()->GlobalConfig(),
			"Basic", "Profile");

	auto cb = [&](const char *name, const char *file_path)
	{
		if (strcmp(old_name, name) != 0) {
			new_name = name;
			new_path = file_path;
			return false;
		}

		return true;
	};

	EnumProfiles(cb);

	if (new_path.empty()) {
		QMessageBox::information(this,
				QTStr("CannotDeleteLastItem.Title"),
				QTStr("CannotDeleteLastItem.Text"));
		return;
	}

	QString text = QTStr("ConfirmRemove.Text");
	text.replace("$1", QT_UTF8(old_name));

	QMessageBox::StandardButton button = QMessageBox::question(this,
			QTStr("ConfirmRemove.Title"), text);
	if (button == QMessageBox::No)
		return;

	size_t new_path_len = new_path.size();
	new_path += "/basic.ini";

	if (config.Open(new_path.c_str(), CONFIG_OPEN_ALWAYS) != 0) {
		blog(LOG_ERROR, "ChangeProfile: Failed to load file '%s'",
				new_path.c_str());
		return;
	}

	new_path.resize(new_path_len);

	config_set_string(App()->GlobalConfig(), "Basic", "Profile",
			new_name.c_str());
	config_set_string(App()->GlobalConfig(), "Basic", "ProfileDir",
			strrchr(new_path.c_str(), '/') + 1);

	config.Swap(basicConfig);
	InitBasicConfigDefaults();
	ResetProfileData();
	DeleteProfile(old_dir.c_str());
	RefreshProfiles();
	config_save(App()->GlobalConfig());
}

void OBSBasic::ChangeProfile()
{
	QAction *action = reinterpret_cast<QAction*>(sender());
	ConfigFile config;
	std::string path;

	if (!action)
		return;

	path = QT_TO_UTF8(action->property("file_name").value<QString>());
	if (path.empty())
		return;

	const char *old_name = config_get_string(App()->GlobalConfig(),
			"Basic", "Profile");
	if (action->text().compare(QT_UTF8(old_name)) == 0) {
		action->setChecked(true);
		return;
	}

	size_t path_len = path.size();
	path += "/basic.ini";

	if (config.Open(path.c_str(), CONFIG_OPEN_ALWAYS) != 0) {
		blog(LOG_ERROR, "ChangeProfile: Failed to load file '%s'",
				path.c_str());
		return;
	}

	path.resize(path_len);

	const char *new_name = config_get_string(config, "General", "Name");
	const char *new_dir = strrchr(path.c_str(), '/') + 1;

	config_set_string(App()->GlobalConfig(), "Basic", "Profile", new_name);
	config_set_string(App()->GlobalConfig(), "Basic", "ProfileDir",
			new_dir);

	blog(LOG_INFO, "------------------------------------------------");
	blog(LOG_INFO, "Switched to profile '%s'", QT_TO_UTF8(action->text()));

	config.Swap(basicConfig);
	InitBasicConfigDefaults();
	ResetProfileData();
	RefreshProfiles();
	config_save(App()->GlobalConfig());
}
