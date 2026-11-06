// Project Key's settings window on Linux: the groups and rows of the macOS and
// Windows settings, applied as they change, as there. The values go through
// Fcitx5's controller on D-Bus to the addon (Sources/ProjectKey.cpp), which
// saves them, so this window and Fcitx5's own configuration page edit the
// same settings.
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
#include <QVBoxLayout>
#include <QVersionNumber>

#include <algorithm>

namespace {

constexpr char kFcitx[] = "org.fcitx.Fcitx5";
constexpr char kConfig[] = "fcitx://config/addon/projectkey";

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

void WriteConfig(QDBusInterface& controller, const QString& key, const QString& value) {
  // The addon merges what it is given, so one key at a time leaves the others as they are.
  controller.call("SetConfig", QString(kConfig), QVariant::fromValue(QDBusVariant(QVariantMap{{key, value}})));
}

// The newest GitHub release, which the other platforms' package managers install from.
void CheckForUpdate(QNetworkAccessManager& network, QLabel* label) {
  QNetworkRequest request(QUrl("https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest"));
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

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("Project Key 設定");
  QApplication::setDesktopFileName("projectkey-settings");
  QApplication::setWindowIcon(QIcon::fromTheme("input-keyboard"));

  QDBusInterface controller(kFcitx, "/controller", "org.fcitx.Fcitx.Controller1");
  const QVariantMap config = ReadConfig(controller);
  const bool available = !config.isEmpty();

  QWidget window;
  window.setWindowTitle("Project Key 設定");
  auto* layout = new QVBoxLayout(&window);
  layout->setSizeConstraint(QLayout::SetFixedSize);  // as tall as the rows, like the other platforms' windows
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
  auto toggle = [&](QFormLayout* form, const QString& key, const QString& label, const QString& detail = {}) {
    auto* box = new QCheckBox(label);
    box->setChecked(Text(config.value(key)) == "True");
    box->setEnabled(available);
    QObject::connect(box, &QCheckBox::toggled, [&controller, key](bool on) { WriteConfig(controller, key, on ? "True" : "False"); });
    form->addRow(box);
    if (!detail.isEmpty()) form->addRow(note(detail));
  };
  // `values` are the option's names in the addon's config; `shown`, when given, what the list says for them.
  auto choice = [&](QFormLayout* form, const QString& key, const QString& label, const QStringList& values,
                    const QStringList& shown = {}) {
    auto* combo = new QComboBox;
    combo->addItems(shown.isEmpty() ? values : shown);
    combo->setCurrentIndex(std::max(0, static_cast<int>(values.indexOf(Text(config.value(key))))));
    combo->setEnabled(available);
    QObject::connect(combo, &QComboBox::currentIndexChanged, [&controller, key, values](int index) {
      if (index >= 0) WriteConfig(controller, key, values[index]);
    });
    form->addRow(label, combo);
  };
  const QStringList proofModes = {"永遠", "有可疑字時", "關閉"};

  QFormLayout* choosing = group("選字");
  choice(choosing, "CandidatePosition", "選字位置", {"游標後", "游標前"}, {"游標後（Windows）", "游標前（macOS）"});
  toggle(choosing, "NextDoubtful", "選完跳到下一個可疑處");
  toggle(choosing, "AdjacentKeys", "鄰鍵修正");
  toggle(choosing, "RememberChoices", "記住選過的字");
  auto* clear = new QPushButton("清除");
  clear->setEnabled(available);
  QObject::connect(clear, &QPushButton::clicked, [clear] {
    // The choices live only in the addon; when it is not loaded there are none to clear.
    QDBusInterface(kFcitx, "/projectkey", "org.projectkey.Engine").call("ClearChoices");
    clear->setEnabled(false);
  });
  choosing->addRow("選字紀錄", clear);

  QFormLayout* appearance = group("外觀");
  choice(appearance, "Appearance", "候選視窗", {"深色", "模糊"});
  appearance->addRow(note("模糊需要 KWin 的模糊特效；沒有時是深色。"));
  choice(appearance, "ProofPreview", "輸入時的整句預覽", proofModes);
  choice(appearance, "ChoosingProof", "選字時的整句預覽", proofModes);

  QFormLayout* keys = group("按鍵");
  toggle(keys, "CapsLockToggle", "Caps Lock 切換中英", "開啟時大寫用 Shift；關閉時 Caps Lock 鎖定大寫");
  toggle(keys, "ShiftToggle", "單按 Shift 切換中英");
  toggle(keys, "EnglishToZhuyin", "Tab 英文轉注音");
  toggle(keys, "ZhuyinToEnglish", "Tab 注音轉英文");

  QFormLayout* about = group("關於");
  auto* version = new QLabel(PROJECTKEY_VERSION);
  version->setTextInteractionFlags(Qt::TextSelectableByMouse);
  about->addRow("版本", version);
  auto* update = new QLabel("檢查中…");
  update->setOpenExternalLinks(true);
  about->addRow("更新", update);
  about->addRow("授權", new QLabel("MIT"));
  auto* engine = new QLabel("<a href=\"https://github.com/openvanilla/McBopomofo\">McBopomofo</a>");
  engine->setOpenExternalLinks(true);
  about->addRow("引擎與詞庫", engine);

  QNetworkAccessManager network;
  CheckForUpdate(network, update);
  window.setMinimumWidth(460);
  window.show();
  return QApplication::exec();
}
