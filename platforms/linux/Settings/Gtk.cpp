// Project Key's settings window for GNOME and other GTK desktops (libadwaita):
// the rows in Rows.h as GNOME preference rows, applied as they change, as on
// macOS and Windows. Like the Qt window (Qt.cpp), it reads and writes the
// addon's config through Fcitx5's controller on D-Bus. libadwaita follows the
// desktop's light or dark style by itself.
#include <adwaita.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <cstdlib>
#include <map>
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
std::map<std::string, std::string> ReadConfig() {
  std::map<std::string, std::string> config;
  GVariant* reply = g_dbus_connection_call_sync(Bus(), kFcitx, "/controller", "org.fcitx.Fcitx.Controller1", "GetConfig",
                                                g_variant_new("(s)", kConfig), nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
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

// The newest GitHub release, which the other platforms' package managers install from.
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

}  // namespace

int main(int argc, char** argv) {
  // No application ID, so GNOME matches the window to projectkey-settings.desktop by its program name.
  g_set_prgname("projectkey-settings");
  AdwApplication* app = adw_application_new(nullptr, G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(Activate), nullptr);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
