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
#include <util/util.hpp>
#include <QMessageBox>
#include <QVariant>
#include "item-widget-helpers.hpp"
#include "window-basic-main.hpp"
#include "window-namedialog.hpp"
#include "qt-wrappers.hpp"

void OBSBasic::CloseDialogs()
{
	QList<QDialog*> childDialogs = this->findChildren<QDialog *>();
	if (!childDialogs.isEmpty()) {
		for (int i = 0; i < childDialogs.size(); ++i) {
			childDialogs.at(i)->close();
		}
	}

	for (QPointer<QWidget> &projector : projectors) {
		delete projector;
		projector.clear();
	}
}

void OBSBasic::ClearSceneData()
{
	disableSaving++;

	CloseDialogs();

	ClearVolumeControls();
	ClearListItems(ui->sources);
	ui->scenes->clear();

	obs_set_output_source(0, nullptr);
	obs_set_output_source(1, nullptr);
	obs_set_output_source(2, nullptr);
	obs_set_output_source(3, nullptr);
	obs_set_output_source(4, nullptr);
	obs_set_output_source(5, nullptr);

	auto cb = [](void *unused, obs_source_t *source)
	{
		obs_source_remove(source);
		UNUSED_PARAMETER(unused);
		return true;
	};

	obs_enum_sources(cb, nullptr);

	disableSaving--;
}

static void EnumSceneCollections(
		std::function<bool(const char *name, const char *path)> cb)
{
	char path[512];
	os_glob_t *glob;

	int ret = GetConfigPath(path, sizeof(path),
			"obs-studio/basic/scenes/*.json");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get config path for scene "
		                  "collections");
		return;
	}

	if (os_glob(path, 0, &glob) != 0) {
		blog(LOG_WARNING, "Failed to glob scene collections");
		return;
	}

	for (size_t i = 0; i < glob->gl_pathc; i++) {
		const char *file_path = glob->gl_pathv[i].path;

		if (glob->gl_pathv[i].directory)
			continue;

		BPtr<char> file_data = os_quick_read_utf8_file(file_path);
		if (!file_data)
			continue;

		obs_data_t *data = obs_data_create_from_json(file_data);
		std::string name = obs_data_get_string(data, "name");

		/* if no name found, use the file name as the name
		 * (this only happens when switching to the new version) */
		if (name.empty()) {
			name = strrchr(file_path, '/') + 1;
			name.resize(name.size() - 5);
		}

		obs_data_release(data);

		if (!cb(name.c_str(), file_path))
			break;
	}

	os_globfree(glob);
}

static bool SceneCollectionExists(const char *find_name)
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

	EnumSceneCollections(func);
	return found;
}

static bool GetSceneCollectionName(QWidget *parent, std::string &name,
		std::string &file, const char *old_name = nullptr)
{
	bool rename = old_name != nullptr;
	const char *title;
	const char *text;
	char path[512];
	size_t len;
	int ret;

	if (rename) {
		title = Str("Basic.Main.RenameSceneCollection.Title");
		text  = Str("Basic.Main.RenameSceneCollection.Text");
	} else {
		title = Str("Basic.Main.AddSceneCollection.Title");
		text  = Str("Basic.Main.AddSceneCollection.Text");
	}

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
		if (SceneCollectionExists(name.c_str())) {
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

	ret = GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get scene collection config path");
		return false;
	}

	len = file.size();
	file.insert(0, path);

	if (!GetClosestUnusedFileName(file, "json")) {
		blog(LOG_WARNING, "Failed to get closest file name for %s",
				file.c_str());
		return false;
	}

	file.erase(file.size() - 5, 5);
	file.erase(0, file.size() - len);
	return true;
}

void OBSBasic::AddSceneCollection(bool create_new)
{
	std::string name;
	std::string file;

	if (!GetSceneCollectionName(this, name, file))
		return;

	SaveProject();

	config_set_string(App()->GlobalConfig(), "Basic", "SceneCollection",
			name.c_str());
	config_set_string(App()->GlobalConfig(), "Basic", "SceneCollectionFile",
			file.c_str());
	if (create_new) {
		ClearSceneData();
		CreateDefaultScene();
	}
	SaveProject();
	RefreshSceneCollections();
}

void OBSBasic::RefreshSceneCollections()
{
	QList<QAction*> menuActions = ui->sceneCollectionMenu->actions();
	int count = 0;

	for (int i = 0; i < menuActions.count(); i++) {
		QVariant v = menuActions[i]->property("file_name");
		if (v.typeName() != nullptr)
			delete menuActions[i];
	}

	const char *cur_name = config_get_string(App()->GlobalConfig(),
			"Basic", "SceneCollection");

	auto addCollection = [&](const char *name, const char *path)
	{
		std::string file = strrchr(path, '/') + 1;
		file.erase(file.size() - 5, 5);

		QAction *action = new QAction(QT_UTF8(name), this);
		action->setProperty("file_name", QT_UTF8(path));
		connect(action, &QAction::triggered,
				this, &OBSBasic::ChangeSceneCollection);
		action->setCheckable(true);

		action->setChecked(strcmp(name, cur_name) == 0);

		ui->sceneCollectionMenu->addAction(action);
		count++;
		return true;
	};

	EnumSceneCollections(addCollection);

	ui->actionRemoveSceneCollection->setEnabled(count > 1);
}

void OBSBasic::on_actionNewSceneCollection_triggered()
{
	AddSceneCollection(true);
}

void OBSBasic::on_actionDupSceneCollection_triggered()
{
	AddSceneCollection(false);
}

void OBSBasic::on_actionRenameSceneCollection_triggered()
{
	std::string name;
	std::string file;

	std::string old_file = config_get_string(App()->GlobalConfig(),
			"Basic", "SceneCollectionFile");
	const char *old_name = config_get_string(App()->GlobalConfig(),
			"Basic", "SceneCollection");

	bool success = GetSceneCollectionName(this, name, file, old_name);
	if (!success)
		return;

	config_set_string(App()->GlobalConfig(), "Basic", "SceneCollection",
			name.c_str());
	config_set_string(App()->GlobalConfig(), "Basic", "SceneCollectionFile",
			file.c_str());
	SaveProject();

	char path[512];
	int ret = GetConfigPath(path, 512, "obs-studio/basic/scenes/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get scene collection config path");
		return;
	}

	old_file.insert(0, path);
	old_file += ".json";
	os_unlink(old_file.c_str());

	RefreshSceneCollections();
}

void OBSBasic::on_actionRemoveSceneCollection_triggered()
{
	std::string new_name;
	std::string new_path;

	std::string old_file = config_get_string(App()->GlobalConfig(),
			"Basic", "SceneCollectionFile");
	const char *old_name = config_get_string(App()->GlobalConfig(),
			"Basic", "SceneCollection");

	auto cb = [&](const char *name, const char *file_path)
	{
		if (strcmp(old_name, name) != 0) {
			new_name = name;
			new_path = file_path;
			return false;
		}

		return true;
	};

	EnumSceneCollections(cb);

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

	char path[512];
	int ret = GetConfigPath(path, 512, "obs-studio/basic/scenes/");
	if (ret <= 0) {
		blog(LOG_WARNING, "Failed to get scene collection config path");
		return;
	}

	old_file.insert(0, path);
	old_file += ".json";
	os_unlink(old_file.c_str());

	ClearSceneData();
	Load(new_path.c_str());
	RefreshSceneCollections();
}

void OBSBasic::ChangeSceneCollection()
{
	QAction *action = reinterpret_cast<QAction*>(sender());
	std::string file_name;

	if (!action)
		return;

	file_name = QT_TO_UTF8(action->property("file_name").value<QString>());
	if (file_name.empty())
		return;

	const char *old_name = config_get_string(App()->GlobalConfig(),
			"Basic", "SceneCollection");
	if (action->text().compare(QT_UTF8(old_name)) == 0) {
		action->setChecked(true);
		return;
	}

	SaveProject();

	ClearSceneData();
	Load(file_name.c_str());
	RefreshSceneCollections();
}
