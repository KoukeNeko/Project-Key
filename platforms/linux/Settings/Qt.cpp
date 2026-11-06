// Project Key's settings window for KDE (Qt): the rows in Rows.h, applied as they
// change, as on macOS and Windows. The values go through Fcitx5's controller on
// D-Bus to the addon (Sources/ProjectKey.cpp), which saves them, so this window,
// the GTK one (Gtk.cpp) and Fcitx5's own configuration page edit the same settings.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFormLayout>
#include <QGroupBox>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <algorithm>

#include "Rows.h"

using namespace ProjectKey::SettingsRows;

namespace {

// A config value as text; Fcitx5 sends each as a string inside a variant.
QString Text(const QVariant& value) {
  return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant().toString() : value.toString();
}

// The addon's settings, keyed by their names in Config (Sources/ProjectKey.cpp);
// empty when Fcitx5 is not running or has no Project Key.
QVariantMap ReadConfig(QDBusInterface& controller) {
  QDBusMessage reply = controller.call("GetConfig", QString(kConfig));
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return {};
  QVariant value = reply.arguments().first().value<QDBusVariant>().variant();
  return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
}

// Whether Fcitx5 took the value: it may have stopped since the window opened.
bool WriteConfig(QDBusInterface& controller, const QString& key, const QString& value) {
  // The addon merges what it is given, so one key at a time leaves the others as they are.
  QDBusMessage reply = controller.call("SetConfig", QString(kConfig), QVariant::fromValue(QDBusVariant(QVariantMap{{key, value}})));
  return reply.type() == QDBusMessage::ReplyMessage;
}

// The control has gone back to what is saved.
void Unsaved(QWidget* control) {
  QMessageBox::warning(control->window(), "Project Key 設定", "無法儲存：Fcitx5 沒有回應，設定沒有改變。");
}

// The newest GitHub release, which the other platforms' package managers install from.
void CheckForUpdate(QNetworkAccessManager& network, QLabel* label) {
  QNetworkRequest request{QUrl(kReleases)};
  request.setHeader(QNetworkRequest::UserAgentHeader, "ProjectKey");  // GitHub rejects requests without one
  QNetworkReply* reply = network.get(request);
  QObject::connect(reply, &QNetworkReply::finished, label, [reply, label] {
    reply->deleteLater();
    QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    QString latest = release.value("tag_name").toString();
    if (latest.startsWith('v')) latest.remove(0, 1);
    if (reply->error() != QNetworkReply::NoError || latest.isEmpty()) {
      label->setText("無法檢查");
    } else if (QVersionNumber::fromString(latest) > QVersionNumber::fromString(PROJECTKEY_VERSION)) {
      label->setText(QString("<a href=\"%1\">下載 %2</a>").arg(release.value("html_url").toString(), latest));
    } else {
      label->setText("已是最新版本");
    }
  });
}

// Whether the desktop asks apps for dark colors (the freedesktop portal's
// color-scheme: 1 is dark), which KDE and GNOME both set.
bool PrefersDark() {
  QDBusMessage reply = QDBusInterface("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                      "org.freedesktop.portal.Settings")
                           .call("Read", QString("org.freedesktop.appearance"), QString("color-scheme"));
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return false;
  QVariant value = reply.arguments().first();
  while (value.canConvert<QDBusVariant>()) value = value.value<QDBusVariant>().variant();  // a variant in a variant
  return value.toUInt() == 1;
}

// Dark colors when the desktop is dark. Qt 6 follows the desktop through its
// platform theme, but Plasma 5 (Ubuntu 24.04) has that theme only for Qt 5 and
// GNOME has none, so here the window takes KDE's colors from kdeglobals on KDE,
// or GNOME's (Adwaita's) dark colors elsewhere. Read at startup only.
void ApplyDarkPalette() {
  if (!PrefersDark()) return;
  // kdeglobals can be left over from KDE on a machine now running GNOME.
  bool kdeDesktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':').contains("KDE");
  QSettings kde(QStandardPaths::locate(QStandardPaths::GenericConfigLocation, "kdeglobals"), QSettings::IniFormat);
  // A KDE color is "r,g,b", which QSettings reads as a list.
  auto color = [&kde, kdeDesktop](const QString& key, QColor fallback) {
    QStringList rgb = kdeDesktop ? kde.value(key).toStringList() : QStringList();
    return rgb.size() == 3 ? QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt()) : fallback;
  };
  QColor window = color("Colors:Window/BackgroundNormal", QColor(0x242424));
  QColor button = color("Colors:Button/BackgroundNormal", QColor(0x353535));
  QPalette palette(button, window);  // derives the bevel shades
  palette.setColor(QPalette::WindowText, color("Colors:Window/ForegroundNormal", QColor(0xffffff)));
  palette.setColor(QPalette::Base, color("Colors:View/BackgroundNormal", QColor(0x1e1e1e)));
  palette.setColor(QPalette::AlternateBase, color("Colors:View/BackgroundAlternate", QColor(0x2a2a2a)));
  palette.setColor(QPalette::Text, color("Colors:View/ForegroundNormal", QColor(0xffffff)));
  palette.setColor(QPalette::ButtonText, color("Colors:Button/ForegroundNormal", QColor(0xffffff)));
  palette.setColor(QPalette::Highlight, color("Colors:Selection/BackgroundNormal", QColor(0x3584e4)));
  palette.setColor(QPalette::HighlightedText, color("Colors:Selection/ForegroundNormal", QColor(0xffffff)));
  palette.setColor(QPalette::Link, color("Colors:View/ForegroundLink", QColor(0x78aeed)));
  QColor inactive = color("Colors:View/ForegroundInactive", QColor(0x9a9996));
  palette.setColor(QPalette::PlaceholderText, inactive);
  for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(QPalette::Disabled, role, inactive);
  }
  QApplication::setPalette(palette);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  ApplyDarkPalette();
  QApplication::setApplicationName("Project Key 設定");
  QApplication::setDesktopFileName("projectkey-settings");
  QApplication::setWindowIcon(QIcon::fromTheme("fcitx-projectkey"));

  QDBusInterface controller(kFcitx, "/controller", "org.fcitx.Fcitx.Controller1");
  const QVariantMap config = ReadConfig(controller);
  const bool available = !config.isEmpty();

  // As tall as the rows, like the other platforms' windows, but no taller than
  // the screen (a 768-pixel laptop with GNOME's larger font): then it scrolls.
  QScrollArea window;
  window.setWindowTitle("Project Key 設定");
  window.setFrameShape(QFrame::NoFrame);
  window.setWidgetResizable(true);
  window.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* content = new QWidget;
  auto* layout = new QVBoxLayout(content);
  if (!available) {
    auto* message = new QLabel("讀不到設定：Fcitx5 沒有在執行，或沒有安裝 Project Key。");
    message->setWordWrap(true);
    layout->addWidget(message);
  }

  auto group = [layout](const QString& title) {
    auto* box = new QGroupBox(title);
    auto* form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    layout->addWidget(box);
    return form;
  };
  auto note = [](const QString& text) {
    auto* label = new QLabel(text);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
  };
  for (const Group& section : Groups()) {
    QFormLayout* form = group(section.title);
    for (const Row& row : section.rows) {
      QString key = row.key;
      if (row.kind == Row::kToggle) {
        auto* box = new QCheckBox(row.title);
        box->setChecked(Text(config.value(key)) == "True");
        box->setEnabled(available);
        QObject::connect(box, &QCheckBox::toggled, [&controller, key, box](bool on) {
          if (WriteConfig(controller, key, on ? "True" : "False")) return;
          QSignalBlocker quiet(box);
          box->setChecked(!on);
          Unsaved(box);
        });
        form->addRow(box);
      } else if (row.kind == Row::kChoice) {
        QStringList values, shown;
        for (const char* value : row.values) values << value;
        for (const char* text : row.shown.empty() ? row.values : row.shown) shown << text;
        auto* combo = new QComboBox;
        combo->addItems(shown);
        combo->setCurrentIndex(std::max(0, static_cast<int>(values.indexOf(Text(config.value(key))))));
        combo->setEnabled(available);
        QObject::connect(combo, &QComboBox::currentIndexChanged, [&controller, key, values, combo, saved = combo->currentIndex()](int index) mutable {
          if (index < 0) return;
          if (WriteConfig(controller, key, values[index])) {
            saved = index;
            return;
          }
          QSignalBlocker quiet(combo);
          combo->setCurrentIndex(saved);
          Unsaved(combo);
        });
        form->addRow(row.title, combo);
      } else {
        auto* clear = new QPushButton("清除");
        clear->setEnabled(available);
        QObject::connect(clear, &QPushButton::clicked, [clear] {
          // The choices live only in the addon; when it is not loaded there are none to clear.
          QDBusInterface(kFcitx, "/projectkey", "org.projectkey.Engine").call("ClearChoices");
          clear->setEnabled(false);
        });
        form->addRow(row.title, clear);
      }
      if (row.detail) form->addRow(note(row.detail));
    }
  }
  QFormLayout* about = group("關於");
  auto* version = new QLabel(PROJECTKEY_VERSION);
  version->setTextInteractionFlags(Qt::TextSelectableByMouse);
  about->addRow("版本", version);
  auto* update = new QLabel("檢查中…");
  update->setOpenExternalLinks(true);
  about->addRow("更新", update);
  about->addRow("授權", new QLabel("MIT"));
  auto* engine = new QLabel(QString("<a href=\"%1\">McBopomofo</a>").arg(kEngineUrl));
  engine->setOpenExternalLinks(true);
  about->addRow("引擎與詞庫", engine);

  QNetworkAccessManager network;
  CheckForUpdate(network, update);
  window.setWidget(content);
  content->setMinimumWidth(460);
  QSize size = content->sizeHint();
  int room = window.screen()->availableGeometry().height() - 60;  // the title bar
  if (size.height() > room) size = QSize(size.width() + window.verticalScrollBar()->sizeHint().width(), room);
  window.resize(size);
  window.show();
  return QApplication::exec();
}
