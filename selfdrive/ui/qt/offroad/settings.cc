#include <cassert>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>
#include <thread> //차선캘리

#include <QDebug>
#include <QProcess>

#include "common/watchdog.h"
#include "common/util.h"
#include "selfdrive/ui/qt/network/networking.h"
#include "selfdrive/ui/qt/offroad/settings.h"
#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/widgets/prime.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/offroad/developer_panel.h"
#include "selfdrive/ui/qt/offroad/firehose.h"

TogglesPanel::TogglesPanel(SettingsWindow *parent) : ListWidget(parent) {
  // param, title, desc, icon
  std::vector<std::tuple<QString, QString, QString, QString>> toggle_defs{
    {
      "OpenpilotEnabledToggle",
      tr("启用openpilot"),
      tr("使用openpilot系统进行自适应巡航控制和车道保持驾驶辅助。使用此功能时需要您始终保持注意力。更改此设置在车辆关闭电源时生效。"),
      "../assets/img_chffr_wheel.png",
    },
    {
      "ExperimentalMode",
      tr("实验模式"),
      "",
      "../assets/img_experimental_white.svg",
    },
    {
      "DisengageOnAccelerator",
      tr("踩油门踏板时解除接合"),
      tr("启用后，踩下油门踏板将解除openpilot。"),
      "../assets/offroad/icon_disengage_on_accelerator.svg",
    },
    {
      "IsLdwEnabled",
      tr("启用车道偏离警告"),
      tr("当您的车辆在超过31英里/小时（50公里/小时）行驶时，在没有转向信号的情况下偏离检测到的车道线时，接收转向回车道的警报。"),
      "../assets/offroad/icon_warning.png",
    },
    {
      "AlwaysOnDM",
      tr("始终开启驾驶员监控"),
      tr("即使openpilot未接合时也启用驾驶员监控。"),
      "../assets/offroad/icon_monitoring.png",
    },
    {
      "RecordFront",
      tr("录制并上传驾驶员摄像头"),
      tr("上传驾驶员摄像头的数据，帮助改进驾驶员监控算法。"),
      "../assets/offroad/icon_monitoring.png",
    },
    {
      "RecordAudio",
      tr("录制并上传麦克风音频"),
      tr("驾驶时录制并存储麦克风音频。音频将包含在comma connect的行车记录仪视频中。"),
      "../assets/offroad/microphone.png",
    },
    {
      "IsMetric",
      tr("使用公制系统"),
      tr("以公里/小时而不是英里/小时显示速度。"),
      "../assets/offroad/icon_metric.png",
    },
  };


  std::vector<QString> longi_button_texts{tr("激进"), tr("标准"), tr("轻松") , tr("更轻松") };
  long_personality_setting = new ButtonParamControl("LongitudinalPersonality", tr("驾驶个性"),
                                          tr("推荐使用标准模式。在激进模式下，openpilot将更紧密地跟随前车，对油门和刹车更加激进。"
                                             "在轻松模式下，openpilot将与前车保持更远的距离。在支持的车辆上，您可以使用方向盘距离按钮在这些个性之间切换。"),
                                          "../assets/offroad/icon_speed_limit.png",
                                          longi_button_texts);

  // set up uiState update for personality setting
  QObject::connect(uiState(), &UIState::uiUpdate, this, &TogglesPanel::updateState);

  for (auto &[param, title, desc, icon] : toggle_defs) {
    auto toggle = new ParamControl(param, title, desc, icon, this);

    bool locked = params.getBool((param + "Lock").toStdString());
    toggle->setEnabled(!locked);

    addItem(toggle);
    toggles[param.toStdString()] = toggle;

    // insert longitudinal personality after NDOG toggle
    if (param == "DisengageOnAccelerator") {
      addItem(long_personality_setting);
    }
  }

  // Toggles with confirmation dialogs
  toggles["ExperimentalMode"]->setActiveIcon("../assets/img_experimental.svg");
  toggles["ExperimentalMode"]->setConfirmation(true, true);
}

void TogglesPanel::updateState(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  if (sm.updated("selfdriveState")) {
    auto personality = sm["selfdriveState"].getSelfdriveState().getPersonality();
    if (personality != s.scene.personality && s.scene.started && isVisible()) {
      long_personality_setting->setCheckedButton(static_cast<int>(personality));
    }
    uiState()->scene.personality = personality;
  }
}

void TogglesPanel::expandToggleDescription(const QString &param) {
  toggles[param.toStdString()]->showDescription();
}

void TogglesPanel::showEvent(QShowEvent *event) {
  updateToggles();
}

void TogglesPanel::updateToggles() {
  auto experimental_mode_toggle = toggles["ExperimentalMode"];
  const QString e2e_description = QString("%1<br>"
                                          "<h4>%2</h4><br>"
                                          "%3<br>"
                                          "<h4>%4</h4><br>"
                                          "%5<br>")
                                  .arg(tr("openpilot默认以<b>轻松模式</b>驾驶。实验模式启用<b>alpha级功能</b>，这些功能尚未准备好用于轻松模式。实验功能如下："))
                                  .arg(tr("端到端纵向控制"))
                                  .arg(tr("让驾驶模型控制油门和刹车。openpilot将按照它认为人类会做的方式驾驶，包括在红灯和停车标志处停车。"
                                          "由于驾驶模型决定驾驶速度，设定速度仅作为上限。这是一个alpha质量功能；"
                                          "应该预期会出现错误。"))
                                  .arg(tr("新的驾驶可视化"))
                                  .arg(tr("驾驶可视化将在低速时切换到面向道路的广角摄像头，以更好地显示某些转弯。实验模式标志也将显示在右上角。"));

  const bool is_release = params.getBool("IsReleaseBranch");
  auto cp_bytes = params.get("CarParamsPersistent");
  if (!cp_bytes.empty()) {
    AlignedBuffer aligned_buf;
    capnp::FlatArrayMessageReader cmsg(aligned_buf.align(cp_bytes.data(), cp_bytes.size()));
    cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();

    if (hasLongitudinalControl(CP)) {
      // normal description and toggle
      experimental_mode_toggle->setEnabled(true);
      experimental_mode_toggle->setDescription(e2e_description);
      long_personality_setting->setEnabled(true);
    } else {
      // no long for now
      experimental_mode_toggle->setEnabled(false);
      long_personality_setting->setEnabled(false);
      params.remove("ExperimentalMode");

      const QString unavailable = tr("由于此车使用原厂ACC进行纵向控制，实验模式目前在此车上不可用。");

      QString long_desc = unavailable + " " + \
                          tr("openpilot纵向控制可能在未来的更新中出现。");
      if (CP.getAlphaLongitudinalAvailable()) {
        if (is_release) {
          long_desc = unavailable + " " + tr("可以在非发布分支上测试openpilot纵向控制的alpha版本，以及实验模式。");
        } else {
          long_desc = tr("启用openpilot纵向控制（alpha）开关以允许实验模式。");
        }
      }
      experimental_mode_toggle->setDescription("<b>" + long_desc + "</b><br><br>" + e2e_description);
    }

    experimental_mode_toggle->refresh();
  } else {
    experimental_mode_toggle->setDescription(e2e_description);
  }
}

DevicePanel::DevicePanel(SettingsWindow *parent) : ListWidget(parent) {
  setSpacing(50);
  addItem(new LabelControl(tr("设备ID"), getDongleId().value_or(tr("无"))));
  addItem(new LabelControl(tr("序列号"), params.get("HardwareSerial").c_str()));

  // power buttons
  QHBoxLayout* power_layout = new QHBoxLayout();
  power_layout->setSpacing(30);

  QPushButton* reboot_btn = new QPushButton(tr("重启"));
  reboot_btn->setObjectName("reboot_btn");
  power_layout->addWidget(reboot_btn);
  QObject::connect(reboot_btn, &QPushButton::clicked, this, &DevicePanel::reboot);
  //车道校准
  QPushButton *reset_CalibBtn = new QPushButton(tr("重新校准"));
  reset_CalibBtn->setObjectName("reset_CalibBtn");
  power_layout->addWidget(reset_CalibBtn);
  QObject::connect(reset_CalibBtn, &QPushButton::clicked, this, &DevicePanel::calibration);

  QPushButton* poweroff_btn = new QPushButton(tr("关机"));
  poweroff_btn->setObjectName("poweroff_btn");
  power_layout->addWidget(poweroff_btn);
  QObject::connect(poweroff_btn, &QPushButton::clicked, this, &DevicePanel::poweroff);

  if (false && !Hardware::PC()) {
      connect(uiState(), &UIState::offroadTransition, poweroff_btn, &QPushButton::setVisible);
  }

  addItem(power_layout);

  QHBoxLayout* init_layout = new QHBoxLayout();
  init_layout->setSpacing(30);

  QPushButton* init_btn = new QPushButton(tr("Git拉取并重启"));
  init_btn->setObjectName("init_btn");
  init_layout->addWidget(init_btn);
  //QObject::connect(init_btn, &QPushButton::clicked, this, &DevicePanel::reboot);
  QObject::connect(init_btn, &QPushButton::clicked, [&]() {
    if (ConfirmationDialog::confirm(tr("Git拉取并重启？"), tr("是"), this)) {
      QString cmd =
        "bash -c 'cd/home/my/openpilot && "
        "git fetch && "
        "if git status -uno | grep -q \"Your branch is behind\"; then "
        "git pull && reboot; "
        "else "
        "echo \"Already up to date.\"; "
        "fi'";

      if (!QProcess::startDetached(cmd)) {
        ConfirmationDialog::alert(tr("启动更新进程失败。"), this);
      }
      else {
        ConfirmationDialog::alert(tr("更新进程已启动。如果应用了更新，设备将重启。"), this);
      }
    }
    });

  QPushButton* default_btn = new QPushButton(tr("设为默认"));
  default_btn->setObjectName("default_btn");
  init_layout->addWidget(default_btn);
  //QObject::connect(default_btn, &QPushButton::clicked, this, &DevicePanel::poweroff);
  QObject::connect(default_btn, &QPushButton::clicked, [&]() {
    if (ConfirmationDialog::confirm(tr("设为默认？"), tr("是"), this)) {
      //emit parent->closeSettings();
      QTimer::singleShot(1000, []() {
        printf("Set to default\n");
        Params().putInt("SoftRestartTriggered", 2);
        printf("Set to default2\n");
        });
    }
    });

  QPushButton* remove_mapbox_key_btn = new QPushButton(tr("移除Mapbox密钥"));
  remove_mapbox_key_btn->setObjectName("remove_mapbox_key_btn");
  init_layout->addWidget(remove_mapbox_key_btn);
  QObject::connect(remove_mapbox_key_btn, &QPushButton::clicked, [&]() {
    if (ConfirmationDialog::confirm(tr("移除Mapbox密钥？"), tr("是"), this)) {
      QTimer::singleShot(1000, []() {
        Params().put("MapboxPublicKey", "");
        Params().put("MapboxSecretKey", "");
        });
    }
    });

  setStyleSheet(R"(
    #reboot_btn { height: 120px; border-radius: 15px; background-color: #2CE22C; }
    #reboot_btn:pressed { background-color: #24FF24; }
    #reset_CalibBtn { height: 120px; border-radius: 15px; background-color: #FFBB00; }
    #reset_CalibBtn:pressed { background-color: #FF2424; }
    #poweroff_btn { height: 120px; border-radius: 15px; background-color: #E22C2C; }
    #poweroff_btn:pressed { background-color: #FF2424; }
    #init_btn { height: 120px; border-radius: 15px; background-color: #2C2CE2; }
    #init_btn:pressed { background-color: #2424FF; }
    #default_btn { height: 120px; border-radius: 15px; background-color: #BDBDBD; }
    #default_btn:pressed { background-color: #A9A9A9; }
    #remove_mapbox_key_btn { height: 120px; border-radius: 15px; background-color: #BDBDBD; }
    #remove_mapbox_key_btn:pressed { background-color: #A9A9A9; }
  )");
  addItem(init_layout);

  pair_device = new ButtonControl(tr("配对设备"), tr("配对"),
                                  tr("将您的设备与comma connect (connect.comma.ai)配对并领取您的comma prime优惠。"));
  connect(pair_device, &ButtonControl::clicked, [=]() {
    PairingPopup popup(this);
    popup.exec();
  });
  addItem(pair_device);

  // offroad-only buttons

  auto dcamBtn = new ButtonControl(tr("驾驶员摄像头"), tr("预览"),
                                   tr("预览驾驶员摄像头以确保驾驶员监控具有良好的可见性。（车辆必须关闭）"));
  connect(dcamBtn, &ButtonControl::clicked, [=]() { emit showDriverView(); });
  addItem(dcamBtn);

  auto retrainingBtn = new ButtonControl(tr("复习培训指南"), tr("复习"), tr("复习openpilot的规则、功能和限制"));
  connect(retrainingBtn, &ButtonControl::clicked, [=]() {
    if (ConfirmationDialog::confirm(tr("确定要复习培训指南吗？"), tr("复习"), this)) {
      emit reviewTrainingGuide();
    }
  });
  addItem(retrainingBtn);

  auto statusCalibBtn = new ButtonControl(tr("校准状态"), tr("显示"), "");
  connect(statusCalibBtn, &ButtonControl::showDescriptionEvent, this, &DevicePanel::updateCalibDescription);
  addItem(statusCalibBtn);

  std::string calib_bytes = params.get("CalibrationParams");
  if (!calib_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(calib_bytes.data(), calib_bytes.size()));
      auto calib = cmsg.getRoot<cereal::Event>().getLiveCalibration();
      if (calib.getCalStatus() != cereal::LiveCalibrationData::Status::UNCALIBRATED) {
        double pitch = calib.getRpyCalib()[1] * (180 / M_PI);
        double yaw = calib.getRpyCalib()[2] * (180 / M_PI);
        QString position = QString("%2 %1° %4 %3°")
                           .arg(QString::number(std::abs(pitch), 'g', 1), pitch > 0 ? "↓" : "↑",
                                QString::number(std::abs(yaw), 'g', 1), yaw > 0 ? "←" : "→");
        params.put("DevicePosition", position.toStdString());
      }
    } catch (kj::Exception) {
      qInfo() << "invalid CalibrationParams";
    }
  }

  if (Hardware::TICI()) {
    auto regulatoryBtn = new ButtonControl(tr("监管信息"), tr("查看"), "");
    connect(regulatoryBtn, &ButtonControl::clicked, [=]() {
      const std::string txt = util::read_file("../assets/offroad/fcc.html");
      ConfirmationDialog::rich(QString::fromStdString(txt), this);
    });
    addItem(regulatoryBtn);
  }

  auto translateBtn = new ButtonControl(tr("更改语言"), tr("更改"), "");
  connect(translateBtn, &ButtonControl::clicked, [=]() {
    QMap<QString, QString> langs = getSupportedLanguages();
    QString selection = MultiOptionDialog::getSelection(tr("Select a language"), langs.keys(), langs.key(uiState()->language), this);
    if (!selection.isEmpty()) {
      // put language setting, exit Qt UI, and trigger fast restart
      params.put("LanguageSetting", langs[selection].toStdString());
      qApp->exit(18);
      watchdog_kick(0);
    }
  });
  addItem(translateBtn);

  QObject::connect(uiState()->prime_state, &PrimeState::changed, [this] (PrimeState::Type type) {
    pair_device->setVisible(type == PrimeState::PRIME_TYPE_UNPAIRED);
  });
  QObject::connect(uiState(), &UIState::offroadTransition, [=](bool offroad) {
    for (auto btn : findChildren<ButtonControl *>()) {
      if (btn != pair_device) {
        btn->setEnabled(offroad);
      }
    }
    translateBtn->setEnabled(true);
    statusCalibBtn->setEnabled(true);
  });

}

void DevicePanel::updateCalibDescription() {
  QString desc =
      tr("openpilot要求设备安装在左右4°以内和上下5°以内。openpilot持续校准，很少需要重置。");
  std::string calib_bytes = params.get("CalibrationParams");
  if (!calib_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(calib_bytes.data(), calib_bytes.size()));
      auto calib = cmsg.getRoot<cereal::Event>().getLiveCalibration();
      if (calib.getCalStatus() != cereal::LiveCalibrationData::Status::UNCALIBRATED) {
        double pitch = calib.getRpyCalib()[1] * (180 / M_PI);
        double yaw = calib.getRpyCalib()[2] * (180 / M_PI);
        desc += tr(" 您的设备指向 %1° %2 和 %3° %4。")
                    .arg(QString::number(std::abs(pitch), 'g', 1), pitch > 0 ? tr("下") : tr("上"),
                         QString::number(std::abs(yaw), 'g', 1), yaw > 0 ? tr("左") : tr("右"));
      }
    } catch (kj::Exception) {
      qInfo() << "invalid CalibrationParams";
    }
  }
  qobject_cast<ButtonControl *>(sender())->setDescription(desc);
}

void DevicePanel::reboot() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm(tr("确定要重启吗？"), tr("重启"), this)) {
      // 再次检查接合状态，以防对话框打开时发生变化
      if (!uiState()->engaged()) {
        params.putBool("DoReboot", true);
      }
    }
  } else {
    ConfirmationDialog::alert(tr("解除接合以重启"), this);
  }
}

//차선캘리
void execAndReboot(const std::string& cmd) {
    system(cmd.c_str());
    Params().putBool("DoReboot", true);
}

void DevicePanel::calibration() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm(tr("确定要重置校准吗？"), tr("重新校准"), this)) {
      if (!uiState()->engaged()) {
        std::thread worker(execAndReboot, "rm -f /home/my/.comma/params/d/CalibrationParams");
        worker.detach();
      }
    }
  } else {
    ConfirmationDialog::alert(tr("重启并解除接合以进行校准"), this);
  }
}

void DevicePanel::poweroff() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm(tr("确定要关机吗？"), tr("关机"), this)) {
      // 再次检查接合状态，以防对话框打开时发生变化
      if (!uiState()->engaged()) {
        params.putBool("DoShutdown", true);
      }
    }
  } else {
    ConfirmationDialog::alert(tr("解除接合以关机"), this);
  }
}

void SettingsWindow::showEvent(QShowEvent *event) {
  setCurrentPanel(0);
}

void SettingsWindow::setCurrentPanel(int index, const QString &param) {
  if (!param.isEmpty()) {
    // Check if param ends with "Panel" to determine if it's a panel name
    if (param.endsWith("Panel")) {
      QString panelName = param;
      panelName.chop(5); // Remove "Panel" suffix

      // Find the panel by name
      for (int i = 0; i < nav_btns->buttons().size(); i++) {
        if (nav_btns->buttons()[i]->text() == tr(panelName.toStdString().c_str())) {
          index = i;
          break;
        }
      }
    } else {
      emit expandToggleDescription(param);
    }
  }

  panel_widget->setCurrentIndex(index);
  nav_btns->buttons()[index]->setChecked(true);
}

SettingsWindow::SettingsWindow(QWidget *parent) : QFrame(parent) {

  // setup two main layouts
  sidebar_widget = new QWidget;
  QVBoxLayout *sidebar_layout = new QVBoxLayout(sidebar_widget);
  panel_widget = new QStackedWidget();

  // close button
  QPushButton *close_btn = new QPushButton(tr("×"));
  close_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 140px;
      padding-bottom: 20px;
      border-radius: 100px;
      background-color: #292929;
      font-weight: 400;
    }
    QPushButton:pressed {
      background-color: #3B3B3B;
    }
  )");
  close_btn->setFixedSize(200, 200);
  sidebar_layout->addSpacing(45);
  sidebar_layout->addWidget(close_btn, 0, Qt::AlignCenter);
  QObject::connect(close_btn, &QPushButton::clicked, this, &SettingsWindow::closeSettings);

  // setup panels
  DevicePanel *device = new DevicePanel(this);
  QObject::connect(device, &DevicePanel::reviewTrainingGuide, this, &SettingsWindow::reviewTrainingGuide);
  QObject::connect(device, &DevicePanel::showDriverView, this, &SettingsWindow::showDriverView);

  TogglesPanel *toggles = new TogglesPanel(this);
  QObject::connect(this, &SettingsWindow::expandToggleDescription, toggles, &TogglesPanel::expandToggleDescription);

  auto networking = new Networking(this);
  QObject::connect(uiState()->prime_state, &PrimeState::changed, networking, &Networking::setPrimeType);

  QList<QPair<QString, QWidget *>> panels = {
    {tr("设备"), device},
    {tr("网络"), networking},
    {tr("开关"), toggles},
  };
  if(Params().getBool("SoftwareMenu")) {
    panels.append({tr("软件"), new SoftwarePanel(this)});
  }
  if(false) {
    panels.append({tr("Firehose"), new FirehosePanel(this)});
  }
  panels.append({ tr("Carrot"), new CarrotPanel(this) });
  panels.append({ tr("开发者"), new DeveloperPanel(this) });

  nav_btns = new QButtonGroup(this);
  for (auto &[name, panel] : panels) {
    QPushButton *btn = new QPushButton(name);
    btn->setCheckable(true);
    btn->setChecked(nav_btns->buttons().size() == 0);
    btn->setStyleSheet(R"(
      QPushButton {
        color: grey;
        border: none;
        background: none;
        font-size: 65px;
        font-weight: 500;
      }
      QPushButton:checked {
        color: white;
      }
      QPushButton:pressed {
        color: #ADADAD;
      }
    )");
    btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    nav_btns->addButton(btn);
    sidebar_layout->addWidget(btn, 0, Qt::AlignRight);

    const int lr_margin = name != tr("网络") ? 50 : 0;  // 网络面板处理自己的边距
    panel->setContentsMargins(lr_margin, 25, lr_margin, 25);

    ScrollView *panel_frame = new ScrollView(panel, this);
    panel_widget->addWidget(panel_frame);

    QObject::connect(btn, &QPushButton::clicked, [=, w = panel_frame]() {
      btn->setChecked(true);
      panel_widget->setCurrentWidget(w);
    });
  }
  sidebar_layout->setContentsMargins(50, 50, 100, 50);

  // main settings layout, sidebar + main panel
  QHBoxLayout *main_layout = new QHBoxLayout(this);

  sidebar_widget->setFixedWidth(500);
  main_layout->addWidget(sidebar_widget);
  main_layout->addWidget(panel_widget);

  setStyleSheet(R"(
    * {
      color: white;
      font-size: 50px;
    }
    SettingsWindow {
      background-color: black;
    }
    QStackedWidget, ScrollView {
      background-color: #292929;
      border-radius: 30px;
    }
  )");
}


#include <QScroller>
#include <QListWidget>

static QStringList get_list(const char* path) {
  QStringList stringList;
  QFile textFile(path);
  if (textFile.open(QIODevice::ReadOnly)) {
    QTextStream textStream(&textFile);
    while (true) {
      QString line = textStream.readLine();
      if (line.isNull()) {
        break;
      } else {
        stringList.append(line);
      }
    }
  }
  return stringList;
}

CarrotPanel::CarrotPanel(QWidget* parent) : QWidget(parent) {
  main_layout = new QStackedLayout(this);
  homeScreen = new QWidget(this);
  carrotLayout = new QVBoxLayout(homeScreen);
  carrotLayout->setMargin(1);

  QHBoxLayout* select_layout = new QHBoxLayout();
  select_layout->setSpacing(1);


  QPushButton* start_btn = new QPushButton(tr("启动"));
  start_btn->setObjectName("start_btn");
  QObject::connect(start_btn, &QPushButton::clicked, this, [this]() {
    this->currentCarrotIndex = 0;
    this->togglesCarrot(0);
    updateButtonStyles();
  });

  QPushButton* cruise_btn = new QPushButton(tr("巡航"));
  cruise_btn->setObjectName("cruise_btn");
  QObject::connect(cruise_btn, &QPushButton::clicked, this, [this]() {
    this->currentCarrotIndex = 1;
    this->togglesCarrot(1);
    updateButtonStyles();
  });

  QPushButton* speed_btn = new QPushButton(tr("速度"));
  speed_btn->setObjectName("speed_btn");
  QObject::connect(speed_btn, &QPushButton::clicked, this, [this]() {
    this->currentCarrotIndex = 2;
    this->togglesCarrot(2);
    updateButtonStyles();
  });

  QPushButton* latLong_btn = new QPushButton(tr("调优"));
  latLong_btn->setObjectName("latLong_btn");
  QObject::connect(latLong_btn, &QPushButton::clicked, this, [this]() {
    this->currentCarrotIndex = 3;
    this->togglesCarrot(3);
    updateButtonStyles();
  });

  QPushButton* disp_btn = new QPushButton(tr("显示"));
  disp_btn->setObjectName("disp_btn");
  QObject::connect(disp_btn, &QPushButton::clicked, this, [this]() {
    this->currentCarrotIndex = 4;
    this->togglesCarrot(4);
    updateButtonStyles();
  });

  QPushButton* path_btn = new QPushButton(tr("路径"));
  path_btn->setObjectName("path_btn");
  QObject::connect(path_btn, &QPushButton::clicked, this, [this]() {
    this->currentCarrotIndex = 5;
    this->togglesCarrot(5);
    updateButtonStyles();
  });


  updateButtonStyles();

  select_layout->addWidget(start_btn);
  select_layout->addWidget(cruise_btn);
  select_layout->addWidget(speed_btn);
  select_layout->addWidget(latLong_btn);
  select_layout->addWidget(disp_btn);
  select_layout->addWidget(path_btn);
  carrotLayout->addLayout(select_layout, 0);

  QWidget* toggles = new QWidget();
  QVBoxLayout* toggles_layout = new QVBoxLayout(toggles);

  cruiseToggles = new ListWidget(this);
  cruiseToggles->addItem(new CValueControl("CruiseButtonMode", "按钮: 巡航按钮模式", "0:正常,1:用户1,2:用户2", "../assets/offroad/icon_road.png", 0, 2, 1));
  cruiseToggles->addItem(new CValueControl("LfaButtonMode", "按钮: LFA按钮模式", "0:正常,1:减速&停止&前车就绪", "../assets/offroad/icon_road.png", 0, 1, 1));
  cruiseToggles->addItem(new CValueControl("CruiseSpeedUnit", "按钮: 巡航速度单位", "", "../assets/offroad/icon_road.png", 1, 20, 1));
  cruiseToggles->addItem(new CValueControl("CruiseEcoControl", "巡航: 节能控制(4km/h)", "临时提高设定速度以提高燃油效率。", "../assets/offroad/icon_road.png", 0, 10, 1));
  //cruiseToggles->addItem(new CValueControl("CruiseSpeedMin", "巡航: 最低速度限制(10)", "巡航控制最低速度", "../assets/offroad/icon_road.png", 5, 50, 1));
  cruiseToggles->addItem(new CValueControl("AutoSpeedUptoRoadSpeedLimit", "巡航: 自动加速 (0%)", "基于前车自动加速至道路限速。", "../assets/offroad/icon_road.png", 0, 200, 10));
  //cruiseToggles->addItem(new CValueControl("AutoResumeFromGas", "油门巡航开启: 使用", "油门踏板释放时自动巡航开启，60%油门巡航自动开启", "../assets/offroad/icon_road.png", 0, 3, 1));
  //cruiseToggles->addItem(new CValueControl("AutoResumeFromGasSpeed", "油门巡航开启: 速度(30)", "行驶速度超过设定值时巡航开启", "../assets/offroad/icon_road.png", 20, 140, 5));
  //cruiseToggles->addItem(new CValueControl("TFollowSpeedAddM", "间距: 额外TFs 40km/h(0)x0.01s", "速度相关额外最大(100km/h) TFs", "../assets/offroad/icon_road.png", -100, 200, 5));
  //cruiseToggles->addItem(new CValueControl("TFollowSpeedAdd", "间距: 额外TFs 100Km/h(0)x0.01s", "速度相关额外最大(100km/h) TFs", "../assets/offroad/icon_road.png", -100, 200, 5));
  cruiseToggles->addItem(new CValueControl("TFollowGap1", "间距1: 应用TFollow (110)x0.01s", "", "../assets/offroad/icon_road.png", 70, 300, 5));
  cruiseToggles->addItem(new CValueControl("TFollowGap2", "间距2: 应用TFollow (120)x0.01s", "", "../assets/offroad/icon_road.png", 70, 300, 5));
  cruiseToggles->addItem(new CValueControl("TFollowGap3", "间距3: 应用TFollow (160)x0.01s", "", "../assets/offroad/icon_road.png", 70, 300, 5));
  cruiseToggles->addItem(new CValueControl("TFollowGap4", "间距4: 应用TFollow (180)x0.01s", "", "../assets/offroad/icon_road.png", 70, 300, 5));
  cruiseToggles->addItem(new CValueControl("DynamicTFollow", "动态间距控制", "", "../assets/offroad/icon_road.png", 0, 100, 5));
  cruiseToggles->addItem(new CValueControl("DynamicTFollowLC", "动态间距控制 (变道)", "", "../assets/offroad/icon_road.png", 0, 100, 5));
  cruiseToggles->addItem(new CValueControl("MyDrivingMode", "驾驶模式: 选择", "1:经济,2:安全,3:标准,4:运动", "../assets/offroad/icon_road.png", 1, 4, 1));
  cruiseToggles->addItem(new CValueControl("MyDrivingModeAuto", "驾驶模式: 自动", "仅正常模式", "../assets/offroad/icon_road.png", 0, 1, 1));
  cruiseToggles->addItem(new CValueControl("TrafficLightDetectMode", "交通灯检测模式", "0:无, 1:仅停车, 2: 停车&通行", "../assets/offroad/icon_road.png", 0, 2, 1));
  //cruiseToggles->addItem(new CValueControl("MyEcoModeFactor", "驾驶模式: 节能加速比例(80%)", "节能模式下的加速比例", "../assets/offroad/icon_road.png", 10, 95, 5));
  //cruiseToggles->addItem(new CValueControl("MySafeModeFactor", "驾驶模式: 安全比例(60%)", "加速/停止距离/减速比例/间距控制比例", "../assets/offroad/icon_road.png", 10, 90, 10));
  //cruiseToggles->addItem(new CValueControl("MyHighModeFactor", "驾驶模式: 高速比例(100%)", "加速比例控制比例", "../assets/offroad/icon_road.png", 100, 300, 10));

  latLongToggles = new ListWidget(this);
  //latLongToggles->addItem(new CValueControl("AutoLaneChangeSpeed", "变道速度(20)", "", "../assets/offroad/icon_road.png", 1, 100, 5));
  latLongToggles->addItem(new CValueControl("UseLaneLineSpeed", "车道线模式速度(0)", "车道线模式，使用lat_mpc控制", "../assets/offroad/icon_logic.png", 0, 200, 5));
  latLongToggles->addItem(new CValueControl("UseLaneLineCurveSpeed", "车道线模式弯道速度(0)", "车道线模式，仅高速", "../assets/offroad/icon_logic.png", 0, 200, 5));
  latLongToggles->addItem(new CValueControl("AdjustLaneOffset", "调整车道偏移(0)cm", "", "../assets/offroad/icon_logic.png", 0, 500, 5));
  latLongToggles->addItem(new CValueControl("CustomSR", "横向: 转向比x0.1(0)", "自定义转向比", "../assets/offroad/icon_logic.png", 0, 300, 1));
  latLongToggles->addItem(new CValueControl("SteerRatioRate", "横向: 转向比应用率x0.01(100)", "转向比应用率", "../assets/offroad/icon_logic.png", 30, 170, 1));
  latLongToggles->addItem(new CValueControl("PathOffset", "横向: 路径偏移", "(-)左, (+)右", "../assets/offroad/icon_logic.png", -150, 150, 1));
  //latLongToggles->addItem(horizontal_line());
  //latLongToggles->addItem(new CValueControl("JerkStartLimit", "纵向: 急动起始(10)x0.1", "起始急动。", "../assets/offroad/icon_road.png", 1, 50, 1));
  //latLongToggles->addItem(new CValueControl("LongitudinalTuningApi", "纵向: 控制类型", "0:速度pid, 1:加速度pid, 2:加速度pid(comma)", "../assets/offroad/icon_road.png", 0, 2, 1));
  latLongToggles->addItem(new CValueControl("LongTuningKpV", "纵向: P增益(100)", "", "../assets/offroad/icon_logic.png", 0, 150, 5));
  latLongToggles->addItem(new CValueControl("LongTuningKiV", "纵向: I增益(0)", "", "../assets/offroad/icon_logic.png", 0, 2000, 5));
  latLongToggles->addItem(new CValueControl("LongTuningKf", "纵向: FF增益(100)", "", "../assets/offroad/icon_logic.png", 0, 200, 5));
  latLongToggles->addItem(new CValueControl("LongActuatorDelay", "纵向: 执行器延迟(20)", "", "../assets/offroad/icon_logic.png", 0, 200, 5));
  latLongToggles->addItem(new CValueControl("VEgoStopping", "纵向: VEgo停止(50)", "停止因子", "../assets/offroad/icon_logic.png", 1, 100, 5));
  latLongToggles->addItem(new CValueControl("RadarReactionFactor", "纵向: 雷达反应因子(100)", "", "../assets/offroad/icon_logic.png", 0, 200, 10));
  //latLongToggles->addItem(new CValueControl("StartAccelApply", "LONG: StartingAccel 2.0x(0)%", "停止->起步时指定加速度的加速率 0: 不使用.", "../assets/offroad/icon_road.png", 0, 100, 10));
  //latLongToggles->addItem(new CValueControl("StopAccelApply", "LONG: StoppingAccel -2.0x(0)%", "停止维持时调整刹车压力. 0: 不使用. ", "../assets/offroad/icon_road.png", 0, 100, 10));
  latLongToggles->addItem(new CValueControl("LaneChangeNeedTorque", "变道需要扭矩", "-1:禁用变道, 0: 不需要扭矩, 1:需要扭矩", "../assets/offroad/icon_logic.png", -1, 1, 1));
  latLongToggles->addItem(new CValueControl("LaneChangeDelay", "变道延迟", "x0.1秒", "../assets/offroad/icon_logic.png", 0, 100, 5));
  latLongToggles->addItem(new CValueControl("LaneChangeBsd", "变道盲点检测", "-1:忽略盲点, 0:盲点检测, 1: 阻止转向扭矩", "../assets/offroad/icon_logic.png", -1, 1, 1));
  latLongToggles->addItem(new CValueControl("StoppingAccel", "纵向: 停止起始加速度x0.01(-40)", "", "../assets/offroad/icon_logic.png", -100, 0, 5));
  latLongToggles->addItem(new CValueControl("StopDistanceCarrot", "纵向: 停止距离 (600)cm", "", "../assets/offroad/icon_logic.png", 300, 1000, 10));
  //latLongToggles->addItem(new CValueControl("TraffStopDistanceAdjust", "纵向: 交通停止距离调整(150)cm", "", "../assets/offroad/icon_road.png", -1000, 1000, 10));
  latLongToggles->addItem(new CValueControl("JLeadFactor3", "纵向: 急动前导因子 (0)", "x0.01", "../assets/offroad/icon_logic.png", 0, 100, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals0", "加速:0km/h(160)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals1", "加速:10km/h(160)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals2", "加速:40km/h(120)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals3", "加速:60km/h(120)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals4", "加速:80km/h(80)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals5", "加速:110km/h(70)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  latLongToggles->addItem(new CValueControl("CruiseMaxVals6", "加速:140km/h(60)", "指定速度所需的加速度。(x0.01m/s^2)", "../assets/offroad/icon_logic.png", 1, 250, 5));
  //latLongToggles->addItem(new CValueControl("CruiseMinVals", "减速:(120)", "设置减速率。(x0.01m/s^2)", "../assets/offroad/icon_road.png", 50, 250, 5));
  latLongToggles->addItem(new CValueControl("MaxAngleFrames", "最大角度帧数(89)", "89:基本, 转向仪表板错误85~87", "../assets/offroad/icon_logic.png", 80, 100, 1));
  latLongToggles->addItem(new CValueControl("SteerActuatorDelay", "横向:转向执行器延迟(30)", "x0.01, 0:实时延迟", "../assets/offroad/icon_logic.png", 0, 100, 1));
  latLongToggles->addItem(new CValueControl("LateralTorqueCustom", "横向: 扭矩自定义(0)", "", "../assets/offroad/icon_logic.png", 0, 2, 1));
  latLongToggles->addItem(new CValueControl("LateralTorqueAccelFactor", "横向: 扭矩加速度因子(2500)", "", "../assets/offroad/icon_logic.png", 1000, 6000, 100));
  latLongToggles->addItem(new CValueControl("LateralTorqueFriction", "横向: 扭矩摩擦(100)", "", "../assets/offroad/icon_logic.png", 0, 1000, 10));
  latLongToggles->addItem(new CValueControl("CustomSteerMax", "横向: 自定义转向最大值(0)", "", "../assets/offroad/icon_logic.png", 0, 30000, 5));
  latLongToggles->addItem(new CValueControl("CustomSteerDeltaUp", "横向: 自定义转向增量上升(0)", "", "../assets/offroad/icon_logic.png", 0, 50, 1));
  latLongToggles->addItem(new CValueControl("CustomSteerDeltaDown", "横向: 自定义转向增量下降(0)", "", "../assets/offroad/icon_logic.png", 0, 50, 1));

  dispToggles = new ListWidget(this);
  //dispToggles->addItem(new CValueControl("ShowHudMode", "显示:显示模式", "0:青蛙,1:APilot,2:底部,3:顶部,4:左侧,5:左底部", "../assets/offroad/icon_shell.png", 0, 5, 1));
  dispToggles->addItem(new CValueControl("ShowDebugUI", "显示:调试信息", "", "../assets/offroad/icon_shell.png", 0, 2, 1));
  dispToggles->addItem(new CValueControl("ShowTpms", "显示:胎压信息", "", "../assets/offroad/icon_shell.png", 0, 3, 1));
  dispToggles->addItem(new CValueControl("ShowDateTime", "显示:时间信息", "0:无,1:时间/日期,2:时间,3:日期", "../assets/offroad/icon_calendar.png", 0, 3, 1));
  //dispToggles->addItem(new CValueControl("ShowSteerRotate", "显示:方向盘旋转", "0:无,1:旋转", "../assets/offroad/icon_shell.png", 0, 1, 1));
  dispToggles->addItem(new CValueControl("ShowPathEnd", "显示:路径终点", "0:无,1:显示", "../assets/offroad/icon_shell.png", 0, 1, 1));
  //dispToggles->addItem(new CValueControl("ShowAccelRpm", "显示:加速计", "0:无,1:显示,1:加速+RPM", "../assets/offroad/icon_shell.png", 0, 2, 1));
  //dispToggles->addItem(new CValueControl("ShowTpms", "显示:胎压监测", "0:无,1:显示", "../assets/offroad/icon_shell.png", 0, 1, 1));
  //dispToggles->addItem(new CValueControl("ShowSteerMode", "显示:方向盘显示模式", "0:黑色,1:彩色,2:无", "../assets/offroad/icon_shell.png", 0, 2, 1));
  dispToggles->addItem(new CValueControl("ShowDeviceState", "显示:设备状态", "0:无,1:显示", "../assets/offroad/icon_shell.png", 0, 1, 1));
  //dispToggles->addItem(new CValueControl("ShowConnInfo", "显示:APM连接", "0:无,1:显示", "../assets/offroad/icon_shell.png", 0, 1, 1));
  dispToggles->addItem(new CValueControl("ShowLaneInfo", "显示:车道信息", "-1:无, 0:路径, 1:路径+车道, 2: 路径+车道+路边", "../assets/offroad/icon_shell.png", -1, 2, 1));
  //dispToggles->addItem(new CValueControl("ShowBlindSpot", "显示:盲点信息", "0:无,1:显示", "../assets/offroad/icon_shell.png", 0, 1, 1));
  //dispToggles->addItem(new CValueControl("ShowGapInfo", "显示:间距信息", "0:无,1:显示", "../assets/offroad/icon_shell.png", -1, 1, 1));
  //dispToggles->addItem(new CValueControl("ShowDmInfo", "显示:驾驶员监控信息", "0:无,1:显示,-1:禁用(重启)", "../assets/offroad/icon_shell.png", -1, 1, 1));
  dispToggles->addItem(new CValueControl("ShowRadarInfo", "显示:雷达信息", "0:无,1:显示,2:相对位置,3:停车", "../assets/offroad/icon_shell.png", 0, 3, 1));
  dispToggles->addItem(new CValueControl("ShowRouteInfo", "显示:路线信息", "0:无,1:显示", "../assets/offroad/icon_shell.png", 0, 1, 1));
  dispToggles->addItem(new CValueControl("ShowPlotMode", "显示:调试绘图", "", "../assets/offroad/icon_shell.png", 0, 10, 1));
  dispToggles->addItem(new CValueControl("ShowCustomBrightness", "亮度比例", "", "../assets/offroad/icon_brightness.png", 0, 100, 10));

  pathToggles = new ListWidget(this);
  pathToggles->addItem(new CValueControl("ShowPathModeCruiseOff", "显示: 路径模式: 巡航关闭", "0:正常,1,2:录制,3,4:^^,5,6:录制,7,8:^^,9,10,11,12:平滑^^", "../assets/offroad/icon_shell.png", 0, 15, 1));
  pathToggles->addItem(new CValueControl("ShowPathColorCruiseOff", "显示: 路径颜色: 巡航关闭", "(+10:描边)0:红色,1:橙色,2:黄色,3:绿色,4:蓝色,5:靛蓝,6:紫色,7:棕色,8:白色,9:黑色", "../assets/offroad/icon_shell.png", 0, 19, 1));
  pathToggles->addItem(new CValueControl("ShowPathMode", "显示:路径模式: 无车道", "0:正常,1,2:录制,3,4:^^,5,6:录制,7,8:^^,9,10,11,12:平滑^^", "../assets/offroad/icon_shell.png", 0, 15, 1));
  pathToggles->addItem(new CValueControl("ShowPathColor", "显示:路径颜色: 无车道", "(+10:描边)0:红色,1:橙色,2:黄色,3:绿色,4:蓝色,5:靛蓝,6:紫色,7:棕色,8:白色,9:黑色", "../assets/offroad/icon_shell.png", 0, 19, 1));
  pathToggles->addItem(new CValueControl("ShowPathModeLane", "显示:路径模式: 车道模式", "0:正常,1,2:录制,3,4:^^,5,6:录制,7,8:^^,9,10,11,12:平滑^^", "../assets/offroad/icon_shell.png", 0, 15, 1));
  pathToggles->addItem(new CValueControl("ShowPathColorLane", "显示:路径颜色: 车道模式", "(+10:描边)0:红色,1:橙色,2:黄色,3:绿色,4:蓝色,5:靛蓝,6:紫色,7:棕色,8:白色,9:黑色", "../assets/offroad/icon_shell.png", 0, 19, 1));
  pathToggles->addItem(new CValueControl("ShowPathWidth", "显示:路径宽度比例(100%)", "", "../assets/offroad/icon_shell.png", 10, 200, 10));

  startToggles = new ListWidget(this);
  QString selected = QString::fromStdString(Params().get("CarSelected3"));
  QPushButton* selectCarBtn = new QPushButton(selected.length() > 1 ? selected : tr("选择您的车辆"));
  selectCarBtn->setObjectName("selectCarBtn");
  selectCarBtn->setStyleSheet(R"(
    QPushButton {
      margin-top: 20px; margin-bottom: 20px; padding: 10px; height: 120px; border-radius: 15px;
      color: #FFFFFF; background-color: #2C2CE2;
    }
    QPushButton:pressed {
      background-color: #2424FF;
    }
  )");
  //selectCarBtn->setFixedSize(350, 100);
  connect(selectCarBtn, &QPushButton::clicked, [=]() {
    QString selected = QString::fromStdString(Params().get("CarSelected3"));

    QStringList all_items = get_list((QString::fromStdString(Params().getParamPath()) + "/SupportedCars").toStdString().c_str());
    all_items.append(get_list((QString::fromStdString(Params().getParamPath()) + "/SupportedCars_gm").toStdString().c_str()));
    all_items.append(get_list((QString::fromStdString(Params().getParamPath()) + "/SupportedCars_toyota").toStdString().c_str()));
    all_items.append(get_list((QString::fromStdString(Params().getParamPath()) + "/SupportedCars_mazda").toStdString().c_str()));

    QMap<QString, QStringList> car_groups;
    for (const QString& car : all_items) {
      QStringList parts = car.split(" ", QString::SkipEmptyParts);
      if (!parts.isEmpty()) {
        QString manufacturer = parts.first();
        car_groups[manufacturer].append(car);
      }
    }

    QStringList manufacturers = car_groups.keys();
    QString selectedManufacturer = MultiOptionDialog::getSelection("选择制造商", manufacturers, manufacturers.isEmpty() ? "" : manufacturers.first(), this);

    if (!selectedManufacturer.isEmpty()) {
      QStringList cars = car_groups[selectedManufacturer];
      QString selectedCar = MultiOptionDialog::getSelection("选择您的车辆", cars, selected, this);

      if (!selectedCar.isEmpty()) {
        if (selectedCar == "[ Not Selected ]") {
          Params().remove("CarSelected3");
        } else {
          printf("Selected Car: %s\n", selectedCar.toStdString().c_str());
          Params().put("CarSelected3", selectedCar.toStdString());
          QTimer::singleShot(1000, []() {
            Params().putInt("SoftRestartTriggered", 1);
          });
          ConfirmationDialog::alert(selectedCar, this);
        }
        selected = QString::fromStdString(Params().get("CarSelected3"));
        selectCarBtn->setText((selected.isEmpty() || selected == "[ Not Selected ]") ? tr("选择您的车辆") : selected);
      }
    }
  });

  startToggles->addItem(selectCarBtn);
  startToggles->addItem(new CValueControl("HyundaiCameraSCC", "现代: 摄像头SCC", "1:连接SCC的CAN线到CAM, 2:同步巡航状态, 3:原厂长距离", "../assets/offroad/icon_shell.png", 0, 3, 1));
  startToggles->addItem(new ParamControl("IsLdwsCar", "车道偏离警告车辆", "", "../assets/offroad/icon_road.png", this));
  startToggles->addItem(new CValueControl("EnableRadarTracks", "启用雷达跟踪", "1:启用雷达跟踪, -1,2:始终禁用使用HKG SCC雷达", "../assets/offroad/icon_shell.png", -1, 2, 1));
  startToggles->addItem(new CValueControl("CanfdHDA2", "CANFD: HDA2模式", "1:HDA2,2:HDA2+BSM", "../assets/offroad/icon_shell.png", 0, 2, 1));
  startToggles->addItem(new CValueControl("AutoCruiseControl", "自动巡航控制", "软保持, 自动巡航开/关控制", "../assets/offroad/icon_road.png", 0, 3, 1));
  startToggles->addItem(new CValueControl("CruiseOnDist", "巡航: 自动开启距离(0cm)", "当油门/刹车关闭时，前车靠近时巡航开启。", "../assets/offroad/icon_road.png", 0, 2500, 50));
  startToggles->addItem(new CValueControl("AutoEngage", "启动时自动接合控制", "1:转向启用, 2:转向/巡航接合", "../assets/offroad/icon_road.png", 0, 2, 1));
  startToggles->addItem(new ParamControl("DisableMinSteerSpeed", "禁用最小转向速度 (例如 SMDPS", "", "../assets/offroad/icon_road.png", this));
  startToggles->addItem(new CValueControl("AutoGasTokSpeed", "自动油门Tok速度", "油门(加速)Tok启用速度", "../assets/offroad/icon_road.png", 0, 200, 5));
  startToggles->addItem(new ParamControl("AutoGasSyncSpeed", "自动更新巡航速度", "", "../assets/offroad/icon_road.png", this));
  startToggles->addItem(new CValueControl("SpeedFromPCM", "从PCM读取巡航速度", "丰田必须设为1，本田3", "../assets/offroad/icon_road.png", 0, 3, 1));
  startToggles->addItem(new CValueControl("SoundVolumeAdjust", "音量(100%)", "", "../assets/offroad/icon_sound.png", 5, 200, 5));
  startToggles->addItem(new CValueControl("SoundVolumeAdjustEngage", "接合时音量(10%)", "", "../assets/offroad/icon_sound.png", 5, 200, 5));
  startToggles->addItem(new CValueControl("MaxTimeOffroadMin", "关机时间 (分钟)", "", "../assets/offroad/icon_sandtimer.png", 1, 600, 10));
  startToggles->addItem(new ParamControl("DisableDM", "禁用驾驶员监控", "", "../assets/img_driver_face_static_x.png", this));
  startToggles->addItem(new CValueControl("EnableConnect", "启用连接", "您的设备可能被Comma封禁", "../assets/offroad/icon_sandtimer.png", 0, 1, 1));
  //startToggles->addItem(new CValueControl("CarrotCountDownSpeed", "NaviCountDown Speed(10)", "", "../assets/offroad/icon_shell.png", 0, 200, 5));
  startToggles->addItem(new CValueControl("MapboxStyle", "Mapbox样式(0)", "", "../assets/offroad/icon_shell.png", 0, 2, 1));
  startToggles->addItem(new CValueControl("RecordRoadCam", "录制道路摄像头(0)", "1:道路摄像头, 2:道路摄像头+广角道路摄像头", "../assets/offroad/icon_shell.png", 0, 2, 1));
  startToggles->addItem(new CValueControl("HDPuse", "使用HDP(CCNC)(0)", "1:使用APN时, 2:始终", "../assets/offroad/icon_shell.png", 0, 2, 1));
  startToggles->addItem(new ParamControl("HotspotOnBoot", "启动时启用热点", "", "../assets/offroad/icon_shell.png", this));
  startToggles->addItem(new ParamControl("SoftwareMenu", "启用软件菜单", "", "../assets/offroad/icon_shell.png", this));
  //startToggles->addItem(new ParamControl("NoLogging", "禁用日志记录", "", "../assets/offroad/icon_shell.png", this));
  //startToggles->addItem(new ParamControl("LaneChangeNeedTorque", "变道: 需要扭矩", "", "../assets/offroad/icon_shell.png", this));
  //startToggles->addItem(new CValueControl("LaneChangeLaneCheck", "变道: 检查车道存在", "(0:否,1:车道,2:+边缘)", "../assets/offroad/icon_shell.png", 0, 2, 1));
  startToggles->addItem(new CValueControl("NNFF", "NNFF", "Twilsonco's NNFF(Reboot required)", "../assets/offroad/icon_road.png", 0, 1, 1));
  startToggles->addItem(new CValueControl("NNFFLite", "NNFFLite", "Twilsonco's NNFF-Lite(Reboot required)", "../assets/offroad/icon_road.png", 0, 1, 1));

  speedToggles = new ListWidget(this);
  speedToggles->addItem(new CValueControl("AutoCurveSpeedLowerLimit", "弯道: 最低限速(30)", "接近弯道时降低速度。最低速度", "../assets/offroad/icon_road.png", 10, 200, 5));
  speedToggles->addItem(new CValueControl("AutoCurveSpeedFactor", "弯道: 自动控制比例(100%)", "", "../assets/offroad/icon_road.png", 50, 300, 5));
  speedToggles->addItem(new CValueControl("AutoCurveSpeedAggressiveness", "弯道: 激进程度 (100%)", "", "../assets/offroad/icon_road.png", 50, 300, 5));
  speedToggles->addItem(new CValueControl("AutoNaviSpeedCtrlEnd", "测速摄像头减速结束(6秒)", "设置减速完成点。数值越大，距离摄像头越远完成减速。", "../assets/offroad/icon_road.png", 3, 20, 1));
  speedToggles->addItem(new CValueControl("AutoNaviSpeedCtrlMode", "导航速度控制模式(2)", "0:不减速, 1:测速摄像头, 2: + 防事故减速带, 3: + 移动摄像头", "../assets/offroad/icon_road.png", 0, 3, 1));
  speedToggles->addItem(new CValueControl("AutoNaviSpeedDecelRate", "测速摄像头减速率x0.01m/s^2(80)", "数值越小，从更远距离开始减速", "../assets/offroad/icon_road.png", 10, 200, 10));
  speedToggles->addItem(new CValueControl("AutoNaviSpeedSafetyFactor", "测速摄像头安全因子(105%)", "", "../assets/offroad/icon_road.png", 80, 120, 1));
  speedToggles->addItem(new CValueControl("AutoNaviSpeedBumpTime", "减速带时间距离(1秒)", "", "../assets/offroad/icon_road.png", 1, 50, 1));
  speedToggles->addItem(new CValueControl("AutoNaviSpeedBumpSpeed", "减速带速度(35公里/小时)", "", "../assets/offroad/icon_road.png", 10, 100, 5));
  speedToggles->addItem(new CValueControl("AutoRoadSpeedLimitOffset", "道路限速偏移(-1)", "-1:不使用,道路限速+偏移", "../assets/offroad/icon_road.png", -1, 100, 1));
  speedToggles->addItem(new CValueControl("AutoNaviCountDownMode", "导航倒计时模式(2)", "0: 关闭, 1:转向+摄像头, 2:转向+摄像头+减速带", "../assets/offroad/icon_road.png", 0, 2, 1));
  speedToggles->addItem(new CValueControl("TurnSpeedControlMode", "转弯速度控制模式(1)", "0: 关闭, 1:视觉, 2:视觉+路线, 3: 路线", "../assets/offroad/icon_road.png", 0, 3, 1));
  speedToggles->addItem(new CValueControl("MapTurnSpeedFactor", "地图转弯速度因子(100)", "", "../assets/offroad/icon_map.png", 50, 300, 5));
  speedToggles->addItem(new CValueControl("AutoTurnControl", "ATC: 自动转弯控制(0)", "0:无, 1: 变道, 2: 变道 + 速度, 3: 速度", "../assets/offroad/icon_road.png", 0, 3, 1));
  speedToggles->addItem(new CValueControl("AutoTurnControlSpeedTurn", "ATC: 转弯速度 (20)", "0:无, 转弯速度", "../assets/offroad/icon_road.png", 0, 100, 5));
  speedToggles->addItem(new CValueControl("AutoTurnControlTurnEnd", "ATC: 转弯控制距离时间 (6)", "距离=速度*时间", "../assets/offroad/icon_road.png", 0, 30, 1));
  speedToggles->addItem(new CValueControl("AutoRoadSpeedAdjust", "自动道路限速调整 (50%)", "", "../assets/offroad/icon_road.png", -1, 100, 5));
  speedToggles->addItem(new CValueControl("AutoTurnMapChange", "ATC自动地图切换(0)", "", "../assets/offroad/icon_road.png", 0, 1, 1));

  toggles_layout->addWidget(cruiseToggles);
  toggles_layout->addWidget(latLongToggles);
  toggles_layout->addWidget(dispToggles);
  toggles_layout->addWidget(pathToggles);
  toggles_layout->addWidget(startToggles);
  toggles_layout->addWidget(speedToggles);
  ScrollView* toggles_view = new ScrollView(toggles, this);
  carrotLayout->addWidget(toggles_view, 1);

  homeScreen->setLayout(carrotLayout);
  main_layout->addWidget(homeScreen);
  main_layout->setCurrentWidget(homeScreen);

  togglesCarrot(0);
}

void CarrotPanel::togglesCarrot(int widgetIndex) {
  startToggles->setVisible(widgetIndex == 0);
  cruiseToggles->setVisible(widgetIndex == 1);
  speedToggles->setVisible(widgetIndex == 2);
  latLongToggles->setVisible(widgetIndex == 3);
  dispToggles->setVisible(widgetIndex == 4);
  pathToggles->setVisible(widgetIndex == 5);
}

void CarrotPanel::updateButtonStyles() {
  QString styleSheet = R"(
      #start_btn, #cruise_btn, #speed_btn, #latLong_btn ,#disp_btn, #path_btn {
          height: 120px; border-radius: 15px; background-color: #393939;
      }
      #start_btn:pressed, #cruise_btn:pressed, #speed_btn:pressed, #latLong_btn:pressed, #disp_btn:pressed, #path_btn:pressed {
          background-color: #4a4a4a;
      }
  )";

  switch (currentCarrotIndex) {
  case 0:
      styleSheet += "#start_btn { background-color: #33ab4c; }";
      break;
  case 1:
      styleSheet += "#cruise_btn { background-color: #33ab4c; }";
      break;
  case 2:
      styleSheet += "#speed_btn { background-color: #33ab4c; }";
      break;
  case 3:
      styleSheet += "#latLong_btn { background-color: #33ab4c; }";
      break;
  case 4:
      styleSheet += "#disp_btn { background-color: #33ab4c; }";
      break;
  case 5:
      styleSheet += "#path_btn { background-color: #33ab4c; }";
      break;
  }

  setStyleSheet(styleSheet);
}


CValueControl::CValueControl(const QString& params, const QString& title, const QString& desc, const QString& icon, int min, int max, int unit)
    : AbstractControl(title, desc, icon), m_params(params), m_min(min), m_max(max), m_unit(unit) {

    label.setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    label.setStyleSheet("color: #e0e879");
    hlayout->addWidget(&label);

    QString btnStyle = R"(
      QPushButton {
        padding: 0;
        border-radius: 50px;
        font-size: 35px;
        font-weight: 500;
        color: #E4E4E4;
        background-color: #393939;
      }
      QPushButton:pressed {
        background-color: #4a4a4a;
      }
    )";

    btnminus.setStyleSheet(btnStyle);
    btnplus.setStyleSheet(btnStyle);
    btnminus.setFixedSize(150, 100);
    btnplus.setFixedSize(150, 100);
    btnminus.setText("－");
    btnplus.setText("＋");
    hlayout->addWidget(&btnminus);
    hlayout->addWidget(&btnplus);

    connect(&btnminus, &QPushButton::released, this, &CValueControl::decreaseValue);
    connect(&btnplus, &QPushButton::released, this, &CValueControl::increaseValue);

    refresh();
}

void CValueControl::showEvent(QShowEvent* event) {
    AbstractControl::showEvent(event);
    refresh();
}

void CValueControl::refresh() {
    label.setText(QString::fromStdString(Params().get(m_params.toStdString())));
}

void CValueControl::adjustValue(int delta) {
    int value = QString::fromStdString(Params().get(m_params.toStdString())).toInt();
    value = qBound(m_min, value + delta, m_max);
    Params().putInt(m_params.toStdString(), value);
    refresh();
}

void CValueControl::increaseValue() {
    adjustValue(m_unit);
}

void CValueControl::decreaseValue() {
    adjustValue(-m_unit);
}
