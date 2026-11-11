// Project Key's settings window for GNOME and other GTK desktops (libadwaita):
// the rows in Rows.h as GNOME preference rows, applied as they change, as on
// macOS and Windows. Like the Qt window (Qt.cpp), it reads and writes the
// addon's config through Fcitx5's controller on D-Bus. libadwaita follows the
// desktop's light or dark style by itself.
#include <adwaita.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Rows.h"

using namespace ProjectKey::SettingsRows;

namespace {

GDBusConnection* Bus() {
  static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  return bus;
}

// The addon's settings as text; empty when Fcitx5 is not running or has no Project Key.
// Fcitx5 not running yet, D-Bus starts it for this call, and it answers once Project Key
// has loaded its models: seconds after a login. So the wait is Qt's default, not 3 seconds.
std::map<std::string, std::string> ReadConfig() {
  std::map<std::string, std::string> config;
  GVariant* reply = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "GetConfig",
                                                g_variant_new("(s)", kConfig), nullptr, G_DBUS_CALL_FLAGS_NONE, 25000, nullptr,
                                                nullptr);
  if (!reply) return config;
  GVariant* boxed = g_variant_get_child_value(reply, 0);
  GVariant* values = g_variant_get_variant(boxed);
  if (g_variant_is_of_type(values, G_VARIANT_TYPE_VARDICT)) {
    GVariantIter iter;
    g_variant_iter_init(&iter, values);
    const char* key;
    GVariant* value;
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
      if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) config[key] = g_variant_get_string(value, nullptr);
      g_variant_unref(value);
    }
  }
  g_variant_unref(values);
  g_variant_unref(boxed);
  g_variant_unref(reply);
  return config;
}

// The addon merges what it is given, so one key at a time leaves the others as they are.
// Whether Fcitx5 took the value: it may have stopped since the window opened.
bool WriteConfig(const char* key, const char* value) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&builder, "{sv}", key, g_variant_new_string(value));
  GVariant* reply = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "SetConfig",
                                                g_variant_new("(sv)", kConfig, g_variant_builder_end(&builder)), nullptr,
                                                G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if (reply) g_variant_unref(reply);
  return reply != nullptr;
}

// The row has gone back to what is saved.
void Unsaved(GtkWidget* row) {
  AdwDialog* dialog = adw_alert_dialog_new("無法儲存", "Fcitx5 沒有回應，設定沒有改變。");
  adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "好");
  adw_dialog_present(dialog, row);
}

void ToggleChanged(AdwSwitchRow* toggle, GParamSpec*, gpointer key) {
  bool on = adw_switch_row_get_active(toggle);
  if (WriteConfig(static_cast<const char*>(key), on ? "True" : "False")) return;
  g_signal_handlers_block_by_func(toggle, reinterpret_cast<gpointer>(ToggleChanged), key);
  adw_switch_row_set_active(toggle, !on);
  g_signal_handlers_unblock_by_func(toggle, reinterpret_cast<gpointer>(ToggleChanged), key);
  Unsaved(GTK_WIDGET(toggle));
}

// The row keeps the index last saved as its "saved" data.
void ChoiceChanged(AdwComboRow* combo, GParamSpec*, gpointer data) {
  const Row* row = static_cast<const Row*>(data);
  guint index = adw_combo_row_get_selected(combo);
  if (index >= row->values.size()) return;
  if (WriteConfig(row->key, row->values[index])) {
    g_object_set_data(G_OBJECT(combo), "saved", GUINT_TO_POINTER(index));
    return;
  }
  g_signal_handlers_block_by_func(combo, reinterpret_cast<gpointer>(ChoiceChanged), data);
  adw_combo_row_set_selected(combo, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(combo), "saved")));
  g_signal_handlers_unblock_by_func(combo, reinterpret_cast<gpointer>(ChoiceChanged), data);
  Unsaved(GTK_WIDGET(combo));
}

std::vector<int> VersionParts(const std::string& version) {
  std::vector<int> parts;
  for (size_t start = 0; start <= version.size();) {
    size_t end = version.find('.', start);
    if (end == std::string::npos) end = version.size();
    parts.push_back(std::atoi(version.substr(start, end - start).c_str()));
    start = end + 1;
  }
  return parts;
}

// Runs f(label) once the main loop is back. From a label's activate-link: GTK still uses the
// clicked link after the handler returns, and new text frees it.
template <void (*f)(GtkLabel*)>
gboolean Later(GtkLabel* label, const char*, gpointer) {
  g_idle_add(+[](gpointer data) -> gboolean {
               f(GTK_LABEL(data));
               g_object_unref(data);
               return G_SOURCE_REMOVE;
             },
             g_object_ref(label));
  return true;
}

// Runs the update (UpdateCommand in Rows.h); once it is done, restarts Fcitx5, which then loads
// the new addon, and opens the new version's settings window in place of this one.
void StartUpdate(GtkLabel* label) {
  std::filesystem::path prefix = HomebrewPrefix();
  std::string command = UpdateCommand(prefix);
  const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
  GSubprocess* update = prefix.empty() ? nullptr : g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, nullptr);
  if (!update) return gtk_label_set_text(label, "無法更新");
  gtk_label_set_text(label, "更新中…（Homebrew 從原始碼建置，約需幾分鐘）");  // no link left to follow again
  g_object_set_data_full(G_OBJECT(label), "settings", g_strdup(UpdatedSettings(prefix).c_str()), g_free);
  g_object_ref(label);
  g_subprocess_wait_check_async(
      update, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer data) {
        auto* label = GTK_LABEL(data);
        bool done = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, nullptr);
        g_object_unref(source);
        if (done) {
          // Waited for: this window closes next, which would drop a call still being sent.
          GVariant* restarted = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "Restart",
                                                            nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
          if (restarted) g_variant_unref(restarted);
          const char* settings[] = {static_cast<const char*>(g_object_get_data(G_OBJECT(label), "settings")), nullptr};
          done = g_spawn_async(nullptr, const_cast<char**>(settings), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
          if (done) gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(label))));
          else gtk_label_set_text(label, "已更新，請重新開啟設定");
        } else {
          gtk_label_set_text(label, "無法更新（記錄在 ~/.cache/projectkey-update.log）");
        }
        g_object_unref(label);
      },
      label);
}

// The newest GitHub release, which the other platforms' package managers install from. Installed
// with Homebrew, a newer one is offered as an update; built from source, as its page.
void CheckForUpdate(GtkLabel* label) {
  SoupSession* session = soup_session_new_with_options("user-agent", "ProjectKey", nullptr);  // GitHub rejects requests without one
  SoupMessage* message = soup_message_new("GET", kReleases);
  g_object_ref(label);
  soup_session_send_and_read_async(
      session, message, G_PRIORITY_DEFAULT, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer data) {
        auto* label = GTK_LABEL(data);
        SoupSession* session = SOUP_SESSION(source);
        GBytes* body = soup_session_send_and_read_finish(session, result, nullptr);
        SoupMessage* message = soup_session_get_async_result_message(session, result);
        std::string latest, page;
        if (body && message && soup_message_get_status(message) == SOUP_STATUS_OK) {
          JsonParser* parser = json_parser_new();
          gsize size = 0;
          const char* data = static_cast<const char*>(g_bytes_get_data(body, &size));
          JsonNode* root = json_parser_load_from_data(parser, data, static_cast<gssize>(size), nullptr) ? json_parser_get_root(parser) : nullptr;
          if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject* release = json_node_get_object(root);
            if (json_object_has_member(release, "tag_name")) latest = json_object_get_string_member(release, "tag_name");
            if (json_object_has_member(release, "html_url")) page = json_object_get_string_member(release, "html_url");
          }
          g_object_unref(parser);
        }
        if (!latest.empty() && latest[0] == 'v') latest.erase(0, 1);
        if (latest.empty()) {
          gtk_label_set_text(label, "無法檢查");
        } else if (VersionParts(latest) > VersionParts(PROJECTKEY_VERSION) && !HomebrewPrefix().empty()) {
          char* markup = g_markup_printf_escaped("<a href=\"#\">更新到 %s</a>", latest.c_str());
          gtk_label_set_markup(label, markup);
          g_free(markup);
          g_signal_connect(label, "activate-link", G_CALLBACK(Later<StartUpdate>), nullptr);
        } else if (VersionParts(latest) > VersionParts(PROJECTKEY_VERSION)) {
          char* markup = g_markup_printf_escaped("<a href=\"%s\">下載 %s</a>", page.c_str(), latest.c_str());
          gtk_label_set_markup(label, markup);
          g_free(markup);
        } else {
          gtk_label_set_text(label, "已是最新版本");
        }
        if (body) g_bytes_unref(body);
        g_object_unref(label);
        g_object_unref(session);
      },
      label);
  g_object_unref(message);
}

// A models download in progress, one file after another.
struct ModelsDownload {
  GtkLabel* label;
  SoupSession* session;
  NewerModels models;
  std::filesystem::path partial;
  size_t index = 0;
  long long done = 0;
  std::ofstream out;
  GChecksum* hash = nullptr;
  GInputStream* in = nullptr;
  char buffer[1 << 16];
};

void EndModelsDownload(ModelsDownload* download, bool done) {
  if (!done) AbandonModels(download->models.date);
  gtk_label_set_text(download->label, done ? InstalledModelsText().c_str() : "無法下載");
  if (download->in) g_object_unref(download->in);
  if (download->hash) g_checksum_free(download->hash);
  g_object_unref(download->label);
  g_object_unref(download->session);
  delete download;
}

void NextModelsFile(ModelsDownload* download);

void ReadModelsFile(ModelsDownload* download) {
  g_input_stream_read_async(
      download->in, download->buffer, sizeof download->buffer, G_PRIORITY_DEFAULT, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer data) {
        auto* download = static_cast<ModelsDownload*>(data);
        gssize read = g_input_stream_read_finish(G_INPUT_STREAM(source), result, nullptr);
        if (read < 0) return EndModelsDownload(download, false);
        if (read > 0) {
          download->out.write(download->buffer, read);
          g_checksum_update(download->hash, reinterpret_cast<const guchar*>(download->buffer), read);
          long long before = download->done * 100 / download->models.bytes();
          download->done += read;
          long long percent = download->done * 100 / download->models.bytes();
          if (percent != before) gtk_label_set_text(download->label, ("下載中 " + std::to_string(percent) + "%").c_str());
          return ReadModelsFile(download);
        }
        download->out.close();
        std::string digest = std::string("sha256:") + g_checksum_get_string(download->hash);
        g_checksum_free(download->hash);
        download->hash = nullptr;
        g_object_unref(download->in);
        download->in = nullptr;
        if (!download->out || digest != download->models.files[download->index].digest) return EndModelsDownload(download, false);
        download->index++;
        NextModelsFile(download);
      },
      download);
}

void NextModelsFile(ModelsDownload* download) {
  if (download->index == download->models.files.size()) {
    bool done = FinishModels(download->models.date);
    if (done) {
      g_dbus_connection_call(Bus(), kFcitx, "/projectkey", "org.projectkey.Engine", "ReloadModels", nullptr, nullptr,
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
    }
    return EndModelsDownload(download, done);
  }
  const ModelsFile& file = download->models.files[download->index];
  download->out.open(download->partial / file.name, std::ios::binary);
  SoupMessage* message = soup_message_new("GET", file.url.c_str());
  if (!download->out || !message) return EndModelsDownload(download, false);
  download->hash = g_checksum_new(G_CHECKSUM_SHA256);
  soup_session_send_async(
      download->session, message, G_PRIORITY_DEFAULT, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer data) {
        auto* download = static_cast<ModelsDownload*>(data);
        GInputStream* in = soup_session_send_finish(SOUP_SESSION(source), result, nullptr);
        SoupMessage* message = soup_session_get_async_result_message(SOUP_SESSION(source), result);
        if (!in || !message || soup_message_get_status(message) != SOUP_STATUS_OK) {
          if (in) g_object_unref(in);
          return EndModelsDownload(download, false);
        }
        download->in = in;
        ReadModelsFile(download);
      },
      download);
  g_object_unref(message);
}

// The models CheckForModels offered on the label.
void StartModelsDownload(GtkLabel* label) {
  auto* newer = static_cast<NewerModels*>(g_object_get_data(G_OBJECT(label), "models"));
  if (!newer) return;  // already started
  std::filesystem::path partial = StartModels(newer->date);
  if (partial.empty()) return gtk_label_set_text(label, "無法下載");
  gtk_label_set_text(label, "下載中 0%");  // no link left to follow again
  g_object_ref(label);
  NextModelsFile(new ModelsDownload{label, soup_session_new_with_options("user-agent", "ProjectKey", nullptr), *newer, partial});
  g_object_set_data(G_OBJECT(label), "models", nullptr);
}

// Offers the newest models release (Rows.h) as a link that downloads it.
void CheckForModels(GtkLabel* label) {
  SoupSession* session = soup_session_new_with_options("user-agent", "ProjectKey", nullptr);
  SoupMessage* message = soup_message_new("GET", kAllReleases);
  g_object_ref(label);
  soup_session_send_and_read_async(
      session, message, G_PRIORITY_DEFAULT, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer data) {
        auto* label = GTK_LABEL(data);
        SoupSession* session = SOUP_SESSION(source);
        GBytes* body = soup_session_send_and_read_finish(session, result, nullptr);
        SoupMessage* message = soup_session_get_async_result_message(session, result);
        std::vector<ModelsRelease> releases;
        if (body && message && soup_message_get_status(message) == SOUP_STATUS_OK) {
          JsonParser* parser = json_parser_new();
          gsize size = 0;
          const char* text = static_cast<const char*>(g_bytes_get_data(body, &size));
          JsonNode* root = json_parser_load_from_data(parser, text, static_cast<gssize>(size), nullptr) ? json_parser_get_root(parser) : nullptr;
          JsonArray* array = root && JSON_NODE_HOLDS_ARRAY(root) ? json_node_get_array(root) : nullptr;
          for (guint i = 0; array && i < json_array_get_length(array); i++) {
            JsonObject* release = json_array_get_object_element(array, i);
            if (!release || !json_object_has_member(release, "tag_name") || !json_object_has_member(release, "assets")) continue;
            ModelsRelease parsed{json_object_get_string_member(release, "tag_name"),
                                 json_object_get_boolean_member_with_default(release, "draft", false), {}};
            JsonArray* assets = json_object_get_array_member(release, "assets");
            for (guint j = 0; assets && j < json_array_get_length(assets); j++) {
              JsonObject* file = json_array_get_object_element(assets, j);
              const char* digest = file ? json_object_get_string_member_with_default(file, "digest", nullptr) : nullptr;
              if (!digest) continue;
              parsed.files.push_back({json_object_get_string_member_with_default(file, "name", ""), digest,
                                      json_object_get_string_member_with_default(file, "browser_download_url", ""),
                                      json_object_get_int_member_with_default(file, "size", 0)});
            }
            releases.push_back(std::move(parsed));
          }
          g_object_unref(parser);
        }
        // Without a newer release, or without GitHub (the 更新 row says so), the row stays as it is.
        NewerModels newer = FindNewerModels(releases);
        if (!newer.date.empty()) {
          char* markup = g_markup_printf_escaped("<a href=\"#\">下載 %s（%lld MB）</a>", newer.date.c_str(), newer.bytes() / 1000000);
          gtk_label_set_markup(label, markup);
          g_free(markup);
          g_object_set_data_full(G_OBJECT(label), "models", new NewerModels(std::move(newer)),
                                 +[](gpointer data) { delete static_cast<NewerModels*>(data); });
          g_signal_connect(label, "activate-link", G_CALLBACK(Later<StartModelsDownload>), nullptr);
        }
        if (body) g_bytes_unref(body);
        g_object_unref(label);
        g_object_unref(session);
      },
      label);
  g_object_unref(message);
}

// A row's value on the right, as GNOME's settings show plain facts.
GtkWidget* InfoRow(const char* title, GtkWidget** value, const char* text) {
  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  *value = gtk_label_new(text);
  gtk_widget_add_css_class(*value, "dim-label");
  gtk_label_set_selectable(GTK_LABEL(*value), true);
  adw_action_row_add_suffix(ADW_ACTION_ROW(row), *value);
  return row;
}

void Activate(GtkApplication* app);

// 恢復原廠設定's answer: the addon's defaults, and with 一併刪除資料 the user's files and choices
// too; then the window again, showing the defaults.
void Restore(AdwAlertDialog*, const char* response, gpointer data) {
  if (std::strcmp(response, "cancel") == 0) return;
  GtkWidget* button = GTK_WIDGET(data);
  GVariant* reply = g_dbus_connection_call_sync(Bus(), kFcitx, "/projectkey", "org.projectkey.Engine", "ResetSettings", nullptr,
                                                nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if (!reply) return Unsaved(button);
  g_variant_unref(reply);
  bool removed = true;
  if (std::strcmp(response, "data") == 0) {
    removed = RemoveUserData();
    g_dbus_connection_call(Bus(), kFcitx, "/projectkey", "org.projectkey.Engine", "ClearChoices", nullptr, nullptr,
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  }
  // Once the dialog, which the old window holds, is gone.
  GtkWindow* old = GTK_WINDOW(g_object_ref(gtk_widget_get_root(button)));
  g_object_set_data(G_OBJECT(old), "removed", GINT_TO_POINTER(removed));
  g_idle_add(+[](gpointer data) -> gboolean {
    GtkWindow* old = GTK_WINDOW(data);
    GtkApplication* app = gtk_window_get_application(old);
    bool removed = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(old), "removed"));
    Activate(app);
    gtk_window_destroy(old);
    g_object_unref(old);
    if (!removed) {
      AdwDialog* dialog = adw_alert_dialog_new("無法刪除資料", "設定已恢復，自訂詞、排除詞或選字紀錄仍在資料夾中。");
      adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "好");
      adw_dialog_present(dialog, GTK_WIDGET(gtk_application_get_active_window(app)));
    }
    return G_SOURCE_REMOVE;
  }, old);
}

void Activate(GtkApplication* app) {
  const std::map<std::string, std::string> config = ReadConfig();
  const bool available = !config.empty();
  auto text = [&config](const char* key) {
    auto it = config.find(key);
    return it == config.end() ? std::string() : it->second;
  };

  GtkWidget* page = adw_preferences_page_new();
  if (!available) {
    GtkWidget* missing = adw_preferences_group_new();
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(missing), "讀不到設定：Fcitx5 沒有在執行，或沒有安裝 Project Key。");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(missing));
  }
  for (const Group& section : Groups()) {
    GtkWidget* group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), section.title);
    gtk_widget_set_sensitive(group, available);
    for (const Row& row : section.rows) {
      GtkWidget* widget = nullptr;
      if (row.kind == Row::kToggle) {
        widget = adw_switch_row_new();
        adw_switch_row_set_active(ADW_SWITCH_ROW(widget), text(row.key) == "True");
        g_signal_connect(widget, "notify::active", G_CALLBACK(ToggleChanged), const_cast<char*>(row.key));
      } else if (row.kind == Row::kChoice) {
        widget = adw_combo_row_new();
        std::vector<const char*> shown = row.shown.empty() ? row.values : row.shown;
        shown.push_back(nullptr);
        GtkStringList* model = gtk_string_list_new(shown.data());
        adw_combo_row_set_model(ADW_COMBO_ROW(widget), G_LIST_MODEL(model));
        g_object_unref(model);
        for (size_t i = 0; i < row.values.size(); i++) {
          if (text(row.key) == row.values[i]) adw_combo_row_set_selected(ADW_COMBO_ROW(widget), static_cast<guint>(i));
        }
        g_object_set_data(G_OBJECT(widget), "saved", GUINT_TO_POINTER(adw_combo_row_get_selected(ADW_COMBO_ROW(widget))));
        g_signal_connect(widget, "notify::selected", G_CALLBACK(ChoiceChanged), const_cast<Row*>(&row));
      } else if (row.kind == Row::kEditPhrases) {
        widget = adw_action_row_new();
        GtkWidget* edit = gtk_button_new_with_label("編輯…");
        gtk_widget_set_valign(edit, GTK_ALIGN_CENTER);
        g_signal_connect(edit, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer data) {
                           std::filesystem::path file = MadeUserFile(static_cast<const Row*>(data)->key);
                           GError* error = nullptr;
                           gchar* uri = file.empty() ? nullptr : g_filename_to_uri(file.c_str(), nullptr, &error);
                           if (uri && g_app_info_launch_default_for_uri(uri, nullptr, &error)) {
                             g_free(uri);
                             return;
                           }
                           if (error) g_warning("Project Key: cannot open %s: %s", file.c_str(), error->message);
                           g_clear_error(&error);
                           g_free(uri);
                           gtk_button_set_label(button, "無法開啟");
                         })),
                         const_cast<Row*>(&row));
        adw_action_row_add_suffix(ADW_ACTION_ROW(widget), edit);
      } else if (row.kind == Row::kShowFolder) {
        widget = adw_action_row_new();
        GtkWidget* show = gtk_button_new_with_label("顯示");
        gtk_widget_set_valign(show, GTK_ALIGN_CENTER);
        g_signal_connect(show, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer) {
                           std::filesystem::path folder = MadeUserFolder();
                           GError* error = nullptr;
                           gchar* uri = folder.empty() ? nullptr : g_filename_to_uri(folder.c_str(), nullptr, &error);
                           if (uri && g_app_info_launch_default_for_uri(uri, nullptr, &error)) {
                             g_free(uri);
                             return;
                           }
                           if (error) g_warning("Project Key: cannot open %s: %s", folder.c_str(), error->message);
                           g_clear_error(&error);
                           g_free(uri);
                           gtk_button_set_label(button, "無法開啟");
                         })),
                         nullptr);
        adw_action_row_add_suffix(ADW_ACTION_ROW(widget), show);
      } else if (row.kind == Row::kRestore) {
        widget = adw_action_row_new();
        GtkWidget* restore = gtk_button_new_with_label("恢復…");
        gtk_widget_set_valign(restore, GTK_ALIGN_CENTER);
        g_signal_connect(restore, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer) {
                           AdwDialog* dialog = adw_alert_dialog_new(kRestoreTitle, kRestoreBody);
                           adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "取消", "settings", "恢復設定", "data",
                                                          "一併刪除資料", nullptr);
                           adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "data", ADW_RESPONSE_DESTRUCTIVE);
                           adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
                           adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
                           g_signal_connect(dialog, "response", G_CALLBACK(Restore), button);
                           adw_dialog_present(dialog, GTK_WIDGET(button));
                         })),
                         nullptr);
        adw_action_row_add_suffix(ADW_ACTION_ROW(widget), restore);
      } else {
        widget = adw_action_row_new();
        GtkWidget* clear = gtk_button_new_with_label("清除");
        gtk_widget_set_valign(clear, GTK_ALIGN_CENTER);
        g_signal_connect(clear, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer) {
                           // The choices live only in the addon; when it is not loaded there are none to clear.
                           g_dbus_connection_call(Bus(), kFcitx, "/projectkey", "org.projectkey.Engine", "ClearChoices", nullptr, nullptr,
                                                  G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
                           gtk_widget_set_sensitive(GTK_WIDGET(button), false);
                         })),
                         nullptr);
        adw_action_row_add_suffix(ADW_ACTION_ROW(widget), clear);
      }
      adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widget), row.title);
      if (row.detail) adw_action_row_set_subtitle(ADW_ACTION_ROW(widget), row.detail);
      adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), widget);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
  }

  GtkWidget* about = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(about), "關於");
  GtkWidget* value = nullptr;
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(about), InfoRow("版本", &value, PROJECTKEY_VERSION));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(about), InfoRow("更新", &value, "檢查中…"));
  gtk_label_set_selectable(GTK_LABEL(value), false);  // it may become a link
  CheckForUpdate(GTK_LABEL(value));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(about), InfoRow("選字模型", &value, InstalledModelsText().c_str()));
  gtk_label_set_selectable(GTK_LABEL(value), false);  // it may become a link
  CheckForModels(GTK_LABEL(value));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(about), InfoRow("授權", &value, "MIT"));
  GtkWidget* engine = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(engine), "引擎與詞庫");
  GtkWidget* link = gtk_link_button_new_with_label(kEngineUrl, "McBopomofo");
  gtk_widget_set_valign(link, GTK_ALIGN_CENTER);
  adw_action_row_add_suffix(ADW_ACTION_ROW(engine), link);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(about), engine);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(about));

  GtkWidget* view = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
  GtkWidget* window = adw_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Project Key 設定");
  gtk_window_set_icon_name(GTK_WINDOW(window), "fcitx-projectkey");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 640);  // fits a 768-pixel screen; the page scrolls
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), view);
  gtk_window_present(GTK_WINDOW(window));
}

// Fcitx5's input method group in use, its default layout and its input methods.
struct InputMethodGroup {
  std::string name, layout;
  std::vector<std::pair<std::string, std::string>> inputMethods;  // name, layout
};

std::optional<InputMethodGroup> CurrentGroup() {
  GVariant* name = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "CurrentInputMethodGroup",
                                               nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  if (!name) return std::nullopt;
  InputMethodGroup group;
  const char* text = nullptr;
  g_variant_get(name, "(&s)", &text);
  group.name = text;
  g_variant_unref(name);
  GVariant* info = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "InputMethodGroupInfo",
                                               g_variant_new("(s)", group.name.c_str()), G_VARIANT_TYPE("(sa(ss))"),
                                               G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  if (!info) return std::nullopt;
  GVariantIter* items = nullptr;
  g_variant_get(info, "(&sa(ss))", &text, &items);
  group.layout = text;
  const char *inputMethod = nullptr, *layout = nullptr;
  while (g_variant_iter_next(items, "(&s&s)", &inputMethod, &layout)) group.inputMethods.emplace_back(inputMethod, layout);
  g_variant_iter_free(items);
  g_variant_unref(info);
  return group;
}

bool InputMethodAdded(const std::optional<InputMethodGroup>& group) {
  return group && std::any_of(group->inputMethods.begin(), group->inputMethods.end(), [](const auto& item) { return item.first == kInputMethod; });
}

// Appends Project Key to the group; Fcitx5 drops it when it has not loaded the addon.
void AddInputMethod(const InputMethodGroup& group) {
  GVariantBuilder items;
  g_variant_builder_init(&items, G_VARIANT_TYPE("a(ss)"));
  for (const auto& [inputMethod, layout] : group.inputMethods) g_variant_builder_add(&items, "(ss)", inputMethod.c_str(), layout.c_str());
  g_variant_builder_add(&items, "(ss)", kInputMethod, "");
  GVariant* reply = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "SetInputMethodGroupInfo",
                                                g_variant_new("(ssa(ss))", group.name.c_str(), group.layout.c_str(), &items), nullptr,
                                                G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  if (reply) g_variant_unref(reply);
}

bool InputMethodInUse() {
  GVariant* reply = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "CurrentInputMethod",
                                                nullptr, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  if (!reply) return false;
  const char* name = nullptr;
  g_variant_get(reply, "(&s)", &name);
  bool inUse = std::string(name) == kInputMethod;
  g_variant_unref(reply);
  return inUse;
}

struct Welcome {
  GtkWidget *add, *added, *shortcut, *inUse, *hints, *text, *done;
  GtkWidget* marks[3];
  int step = -1;
  bool addFailed = false;
};

// A step's mark: done, the step to do now, or not yet.
void SetMark(GtkWidget* mark, int step, int row) {
  gtk_label_set_text(GTK_LABEL(mark), step > row ? "✓" : step == row ? "●" : "○");
  for (const char* style : {"success", "accent", "dim-label"}) gtk_widget_remove_css_class(mark, style);
  gtk_widget_add_css_class(mark, step > row ? "success" : step == row ? "accent" : "dim-label");
}

void RefreshWelcome(Welcome* welcome) {
  std::optional<InputMethodGroup> group = CurrentGroup();
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(gtk_text_view_get_buffer(GTK_TEXT_VIEW(welcome->text)), &start, &end);
  bool added = InputMethodAdded(group), tried = !gtk_text_iter_equal(&start, &end);
  int step = WelcomeStep(added, added && InputMethodInUse(), tried);
  // Fcitx5 not running, or running without the addon it would add.
  gtk_label_set_text(GTK_LABEL(welcome->added), added ? "已加入" : !group ? "Fcitx5 沒有執行" : "請重新啟動 Fcitx5");
  gtk_widget_set_visible(welcome->added, step > 0 || !group || welcome->addFailed);
  gtk_widget_set_visible(welcome->add, step == 0 && group && !welcome->addFailed);
  if (step == welcome->step) return;
  welcome->step = step;
  for (int row = 0; row < 3; row++) SetMark(welcome->marks[row], step, row);
  gtk_widget_set_visible(welcome->shortcut, step <= 1);
  gtk_widget_set_visible(welcome->inUse, step > 1);
  gtk_widget_set_opacity(welcome->shortcut, step == 1 ? 1 : 0.55);
  gtk_widget_set_opacity(welcome->hints, step >= 2 ? 1 : 0.5);
  gtk_widget_set_opacity(welcome->text, step >= 2 ? 1 : 0.5);
  gtk_widget_set_sensitive(welcome->text, step >= 1);
  if (step == 3) {
    gtk_widget_add_css_class(welcome->done, "suggested-action");
  } else {
    gtk_widget_remove_css_class(welcome->done, "suggested-action");
  }
  if (step == 2) gtk_widget_grab_focus(welcome->text);  // the switch landed: type here next
}

GtkWidget* Mark() {
  GtkWidget* mark = gtk_label_new("○");
  gtk_widget_set_size_request(mark, 20, -1);
  return mark;
}

GtkWidget* ActionRow(const char* title, GtkWidget* mark) {
  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
  adw_action_row_add_prefix(ADW_ACTION_ROW(row), mark);
  return row;
}

GtkWidget* DimLabel(const char* text) {
  GtkWidget* label = gtk_label_new(text);
  gtk_widget_add_css_class(label, "dim-label");
  return label;
}

GtkWidget* Hint(const char* accelerator, const char* text) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(box), gtk_shortcut_label_new(accelerator));
  GtkWidget* label = DimLabel(text);
  gtk_widget_add_css_class(label, "caption");
  gtk_box_append(GTK_BOX(box), label);
  return box;
}

void ActivateWelcome(GtkApplication* app) {
  auto* welcome = new Welcome;
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  for (auto margin : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_bottom}) margin(content, 24);

  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget* icon = gtk_image_new_from_icon_name("fcitx-projectkey");  // the input method's own icon
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
  gtk_box_append(GTK_BOX(header), icon);
  GtkWidget* title = gtk_label_new("Project Key");
  gtk_widget_add_css_class(title, "title-2");
  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(content), header);

  GtkWidget* steps = adw_preferences_group_new();
  for (GtkWidget*& mark : welcome->marks) mark = Mark();
  GtkWidget* addRow = ActionRow("加入輸入法", welcome->marks[0]);
  welcome->add = gtk_button_new_with_label("加入");
  gtk_widget_add_css_class(welcome->add, "suggested-action");
  gtk_widget_set_valign(welcome->add, GTK_ALIGN_CENTER);
  welcome->added = DimLabel("已加入");
  adw_action_row_add_suffix(ADW_ACTION_ROW(addRow), welcome->add);
  adw_action_row_add_suffix(ADW_ACTION_ROW(addRow), welcome->added);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(steps), addRow);
  GtkWidget* switchRow = ActionRow("切換到 Project Key", welcome->marks[1]);
  welcome->shortcut = gtk_shortcut_label_new("<Control>space");  // Fcitx5's default trigger key
  gtk_widget_set_valign(welcome->shortcut, GTK_ALIGN_CENTER);
  welcome->inUse = DimLabel("使用中");
  adw_action_row_add_suffix(ADW_ACTION_ROW(switchRow), welcome->shortcut);
  adw_action_row_add_suffix(ADW_ACTION_ROW(switchRow), welcome->inUse);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(steps), switchRow);
  GtkWidget* tryRow = ActionRow("試打", welcome->marks[2]);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(steps), tryRow);
  gtk_box_append(GTK_BOX(content), steps);

  GtkWidget* tryBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  welcome->hints = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_box_append(GTK_BOX(welcome->hints), Hint("space", "選字"));
  gtk_box_append(GTK_BOX(welcome->hints), Hint("Return", "送出"));
  gtk_box_append(GTK_BOX(welcome->hints), Hint("Shift_L", "單按切換中英"));
  gtk_box_append(GTK_BOX(tryBox), welcome->hints);
  welcome->text = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(welcome->text), GTK_WRAP_WORD_CHAR);
  for (auto margin : {gtk_text_view_set_left_margin, gtk_text_view_set_right_margin, gtk_text_view_set_top_margin, gtk_text_view_set_bottom_margin}) {
    margin(GTK_TEXT_VIEW(welcome->text), 8);
  }
  GtkWidget* frame = gtk_frame_new(nullptr);
  gtk_frame_set_child(GTK_FRAME(frame), welcome->text);
  gtk_widget_set_size_request(frame, -1, 76);
  gtk_box_append(GTK_BOX(tryBox), frame);
  gtk_box_append(GTK_BOX(content), tryBox);

  GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget* settings = gtk_button_new_with_label("設定…");
  gtk_widget_add_css_class(settings, "flat");
  gtk_box_append(GTK_BOX(footer), settings);
  welcome->done = gtk_button_new_with_label("完成");
  gtk_widget_set_hexpand(welcome->done, true);
  gtk_widget_set_halign(welcome->done, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(footer), welcome->done);
  gtk_box_append(GTK_BOX(content), footer);

  GtkWidget* view = adw_toolbar_view_new();
  GtkWidget* bar = adw_header_bar_new();
  adw_header_bar_set_show_title(ADW_HEADER_BAR(bar), false);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), bar);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), content);
  GtkWidget* window = adw_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Project Key");
  gtk_window_set_icon_name(GTK_WINDOW(window), "fcitx-projectkey");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, -1);
  gtk_window_set_resizable(GTK_WINDOW(window), false);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), view);

  g_signal_connect(welcome->add, "clicked", G_CALLBACK((+[](GtkButton*, gpointer data) {
                     auto* welcome = static_cast<Welcome*>(data);
                     std::optional<InputMethodGroup> group = CurrentGroup();
                     if (group) AddInputMethod(*group);
                     welcome->addFailed = !InputMethodAdded(CurrentGroup());
                     RefreshWelcome(welcome);
                   })),
                   welcome);
  g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(welcome->text)), "changed",
                   G_CALLBACK((+[](GtkTextBuffer*, gpointer data) { RefreshWelcome(static_cast<Welcome*>(data)); })), welcome);
  g_signal_connect(settings, "clicked", G_CALLBACK((+[](GtkButton*, gpointer) {
                     // The settings window is this program without --welcome.
                     char* self = g_file_read_link("/proc/self/exe", nullptr);
                     const char* command[] = {self, nullptr};
                     if (self) g_spawn_async(nullptr, const_cast<char**>(command), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
                     g_free(self);
                   })),
                   nullptr);
  g_signal_connect_swapped(welcome->done, "clicked", G_CALLBACK(gtk_window_close), window);
  // Neither Fcitx5's input methods nor the one in use say when they change.
  guint poll = g_timeout_add(500, +[](gpointer data) -> gboolean {
    RefreshWelcome(static_cast<Welcome*>(data));
    return G_SOURCE_CONTINUE;
  }, welcome);
  g_object_set_data_full(G_OBJECT(window), "welcome", welcome, +[](gpointer data) { delete static_cast<Welcome*>(data); });
  g_signal_connect(window, "destroy", G_CALLBACK((+[](GtkWidget*, gpointer poll) { g_source_remove(GPOINTER_TO_UINT(poll)); })),
                   GUINT_TO_POINTER(poll));
  RefreshWelcome(welcome);
  gtk_window_present(GTK_WINDOW(window));
}

}  // namespace

int main(int argc, char** argv) {
  // No application ID, so GNOME matches the window to projectkey-settings.desktop by its program name.
  g_set_prgname("projectkey-settings");
  AdwApplication* app = adw_application_new(nullptr, G_APPLICATION_NON_UNIQUE);
  // --welcome opens the window after installing instead; GApplication would reject the option.
  bool welcome = argc > 1 && std::string(argv[1]) == "--welcome";
  g_signal_connect(app, "activate", welcome ? G_CALLBACK(ActivateWelcome) : G_CALLBACK(Activate), nullptr);
  int status = g_application_run(G_APPLICATION(app), welcome ? 1 : argc, argv);
  g_object_unref(app);
  return status;
}
