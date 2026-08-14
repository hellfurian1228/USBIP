#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QDataStream>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QStyle>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QUrl>
#include <lm.h>
#include <dxgi.h>
#include <wincrypt.h>
#include <windows.h>

#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "crypt32.lib")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("OmniStream Desktop Client v1.0.0");
    resize(1000, 650);

    logWindow = new LogWindow(this);

    setupUi();
    loadSettings();
    populateDuoUsers();
    populateDuoAdapters();
    loadDuoInstances();
    loadDuoGlobalSettings();
    checkAndConfigureDrivers();

    try {
        usbip::Handle dev = usbip::vhci::open();
        if (dev) {
            usbip::vhci::set_persistent(dev.get(), std::vector<usbip::persistent_device>());
        }
    } catch (...) {}

    libusbip::set_debug_output([this](std::string msg) {
        logWindow->appendLog("[USBIP-SDK]", QString::fromStdString(msg).trimmed());
    });

    loadUsbIdDatabase();

    syncTimer = new QTimer(this);
    connect(syncTimer, &QTimer::timeout, this, &MainWindow::syncDeviceState);
    syncTimer->start(3000);

    trayIcon = new QSystemTrayIcon(this);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QIcon icon;
        if (style()) {
            icon = style()->standardIcon(QStyle::SP_ComputerIcon);
        }
        trayIcon->setIcon(icon);

        QMenu *trayMenu = new QMenu(this);
        QAction *restoreAction = trayMenu->addAction("Restore");
        connect(restoreAction, &QAction::triggered, this, &MainWindow::showNormal);
        QAction *exitAction = trayMenu->addAction("Exit");
        connect(exitAction, &QAction::triggered, this, [this]() {
            isExiting = true;
            close();
        });
        trayIcon->setContextMenu(trayMenu);
        connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
                showNormal();
                activateWindow();
                raise();
            }
        });
        trayIcon->show();
    }

    logWindow->appendLog("INFO", "OmniStream Client initialized successfully.");

    if (autoConnectCheckBox->isChecked()) {
        logWindow->appendLog("INFO", "Auto-connect enabled. Initiating startup connection...");
        handleConnect();
    }
}

MainWindow::~MainWindow() {}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    QString activeInst = duoInstanceCombo->currentText();
    if (!activeInst.isEmpty()) {
        saveDuoInstanceSettings(activeInst);
    }
    saveDuoGlobalSettings();

    if (minimizeToTrayCheckBox->isChecked() && !isExiting && QSystemTrayIcon::isSystemTrayAvailable()) {
        event->ignore();
        hide();
        trayIcon->showMessage("OmniStream Client", "Application minimized to system tray.", QSystemTrayIcon::Information, 2000);
    } else {
        event->accept();
    }
}

void MainWindow::loadSettings() {
    QSettings settings("OmniStream", "OmniStreamClient");
    
    QStringList profiles = settings.value("profiles/list", QStringList() << "Default").toStringList();
    currentProfile = settings.value("profiles/active", "Default").toString();
    
    profileCombo->blockSignals(true);
    profileCombo->clear();
    profileCombo->addItems(profiles);
    int profileIndex = profileCombo->findText(currentProfile);
    if (profileIndex >= 0) {
        profileCombo->setCurrentIndex(profileIndex);
    } else {
        profileCombo->setCurrentIndex(0);
        currentProfile = profileCombo->currentText();
    }
    profileCombo->blockSignals(false);
    
    loadProfileSettings(currentProfile);
}

void MainWindow::saveSettings() {
    QSettings settings("OmniStream", "OmniStreamClient");
    
    QStringList profiles;
    for (int i = 0; i < profileCombo->count(); ++i) {
        profiles.append(profileCombo->itemText(i));
    }
    settings.setValue("profiles/list", profiles);
    settings.setValue("profiles/active", currentProfile);
    
    saveProfileSettings(currentProfile);
}

void MainWindow::loadProfileSettings(const QString &profileName) {
    QSettings settings("OmniStream", "OmniStreamClient");
    settings.beginGroup("profiles/" + profileName);
    
    QString savedIp = settings.value("hostIp", "192.168.1.11").toString();
    QString savedPort = settings.value("port", "3240").toString();
    QString savedTheme = settings.value("theme", "Dark").toString();
    int savedBitrate = settings.value("bitrate", 45).toInt();
    bool savedAutoConnect = settings.value("autoConnect", false).toBool();
    bool savedMinimizeToTray = settings.value("minimizeToTray", false).toBool();
    
    settings.endGroup();

    hostIpLineEdit->setText(savedIp);
    portLineEdit->setText(savedPort);
    
    int themeIndex = themeCombo->findText(savedTheme);
    if (themeIndex >= 0) {
        themeCombo->setCurrentIndex(themeIndex);
    }
    applyTheme(savedTheme);

    bitrateSpinBox->setValue(savedBitrate);
    bitrateSlider->setValue(savedBitrate);
    autoConnectCheckBox->setChecked(savedAutoConnect);
    minimizeToTrayCheckBox->setChecked(savedMinimizeToTray);

    logWindow->appendLog("INFO", QString("Loaded profile '%1' settings. Last host IP: %2:%3").arg(profileName, savedIp, savedPort));
}

void MainWindow::saveProfileSettings(const QString &profileName) {
    QSettings settings("OmniStream", "OmniStreamClient");
    settings.beginGroup("profiles/" + profileName);
    
    settings.setValue("hostIp", hostIpLineEdit->text().trimmed());
    settings.setValue("port", portLineEdit->text().trimmed());
    settings.setValue("theme", themeCombo->currentText());
    settings.setValue("bitrate", bitrateSpinBox->value());
    settings.setValue("autoConnect", autoConnectCheckBox->isChecked());
    settings.setValue("minimizeToTray", minimizeToTrayCheckBox->isChecked());
    
    settings.endGroup();
    logWindow->appendLog("INFO", QString("Saved settings for profile '%1'.").arg(profileName));
}

bool MainWindow::validatePort(quint16 port) {
    if (port < 3240 || port > 3260) {
        logWindow->appendLog("ERROR", QString("Port %1 out of bounds. Allowed range: 3240-3260.").arg(port));
        QMessageBox::critical(this, "Port Error", "Invalid port specified! OmniStream requires a port between 3240 and 3260.");
        return false;
    }
    return true;
}

void MainWindow::setupUi() {
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    QHBoxLayout *topBarLayout = new QHBoxLayout();
    
    QLabel *ipLabel = new QLabel("Host IP:", this);
    hostIpLineEdit = new QLineEdit("192.168.1.11", this);
    
    QLabel *portLabel = new QLabel("Port (3240-3260):", this);
    portLineEdit = new QLineEdit("3240", this);
    portLineEdit->setFixedWidth(60);

    connectButton = new QPushButton("Connect", this);
    scanHostButton = new QPushButton("Scan Host", this);
    loggerButton = new QPushButton("Debug Console", this);
    connectionStatusLabel = new QLabel("Status: Disconnected", this);

    topBarLayout->addWidget(ipLabel);
    topBarLayout->addWidget(hostIpLineEdit);
    topBarLayout->addWidget(portLabel);
    topBarLayout->addWidget(portLineEdit);
    topBarLayout->addWidget(connectButton);
    topBarLayout->addWidget(scanHostButton);
    topBarLayout->addWidget(connectionStatusLabel);
    topBarLayout->addStretch();
    topBarLayout->addWidget(loggerButton);

    tabWidget = new QTabWidget(this);
    tabWidget->addTab(createNetworkTab(), "Network & USB/IP");
    tabWidget->addTab(createAudioTab(), "Audio Relay");
    tabWidget->addTab(createDisplayTab(), "Display & Streaming");
    tabWidget->addTab(createSettingsTab(), "Settings");

    mainLayout->addLayout(topBarLayout);
    mainLayout->addWidget(tabWidget);

    setCentralWidget(centralWidget);

    connect(connectButton, &QPushButton::clicked, this, &MainWindow::handleConnect);
    connect(scanHostButton, &QPushButton::clicked, this, &MainWindow::handleScanHost);
    connect(loggerButton, &QPushButton::clicked, this, &MainWindow::handleToggleLogWindow);
}

QWidget* MainWindow::createNetworkTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QGroupBox *usbGroupBox = new QGroupBox("Remote USB Devices (USB/IP)", tab);
    QVBoxLayout *usbLayout = new QVBoxLayout(usbGroupBox);

    usbDeviceTable = new QTableWidget(0, 5, this);
    usbDeviceTable->setHorizontalHeaderLabels({"Device Name", "VID:PID", "Status", "Attach Action", "Reset Action"});
    usbDeviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    usbLayout->addWidget(usbDeviceTable);
    layout->addWidget(usbGroupBox);

    return tab;
}

void MainWindow::addUsbDeviceToTable(const QString &name, const QString &busId, const QString &vidPid, const QString &status, bool attached) {
    int row = usbDeviceTable->rowCount();
    usbDeviceTable->insertRow(row);

    QTableWidgetItem *nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, busId);
    usbDeviceTable->setItem(row, 0, nameItem);

    usbDeviceTable->setItem(row, 1, new QTableWidgetItem(vidPid));
    usbDeviceTable->setItem(row, 2, new QTableWidgetItem(status));

    QPushButton *attachBtn = new QPushButton(attached ? "Detach" : "Attach", this);
    QPushButton *resetBtn = new QPushButton("Reset Connection", this);

    connect(attachBtn, &QPushButton::clicked, [this, row]() { handleToggleDeviceAttach(row); });
    connect(resetBtn, &QPushButton::clicked, [this, row]() { handleResetDeviceConnection(row); });

    usbDeviceTable->setCellWidget(row, 3, attachBtn);
    usbDeviceTable->setCellWidget(row, 4, resetBtn);
}

QWidget* MainWindow::createAudioTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QGroupBox *audioGroup = new QGroupBox("Audio Relay Engine Settings", tab);
    QFormLayout *formLayout = new QFormLayout(audioGroup);

    enableAudioRelayButton = new QPushButton("Enable Audio Relay Stream", this);
    enableAudioRelayButton->setCheckable(true);

    audioQualityCombo = new QComboBox(this);
    audioQualityCombo->addItems({"Low Latency (64 kbps)", "Balanced (128 kbps)", "High Fidelity (256 kbps)", "Lossless Uncompressed"});
    audioQualityCombo->setCurrentIndex(2);

    audioSampleRateCombo = new QComboBox(this);
    audioSampleRateCombo->addItems({"44100 Hz", "48000 Hz", "96000 Hz"});
    audioSampleRateCombo->setCurrentIndex(1);

    audioBufferSlider = new QSlider(Qt::Horizontal, this);
    audioBufferSlider->setRange(5, 100);
    audioBufferSlider->setValue(20);
    audioBufferLabel = new QLabel("Buffer Size: 20 ms", this);

    connect(audioBufferSlider, &QSlider::valueChanged, [this](int val) {
        audioBufferLabel->setText(QString("Buffer Size: %1 ms").arg(val));
    });

    resetAudioButton = new QPushButton("Reset Audio Subsystem", this);

    formLayout->addRow("Relay State:", enableAudioRelayButton);
    formLayout->addRow("Preset Quality:", audioQualityCombo);
    formLayout->addRow("Sample Rate:", audioSampleRateCombo);
    formLayout->addRow(audioBufferLabel, audioBufferSlider);
    formLayout->addRow("Subsystem Recovery:", resetAudioButton);

    layout->addWidget(audioGroup);
    layout->addStretch();

    connect(resetAudioButton, &QPushButton::clicked, this, &MainWindow::handleResetAudioSubsystem);

    return tab;
}

QWidget* MainWindow::createDisplayTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QGroupBox *displayGroup = new QGroupBox("Display & Stream Configurations", tab);
    QFormLayout *formLayout = new QFormLayout(displayGroup);

    resolutionCombo = new QComboBox(this);
    resolutionCombo->addItems({"1280x720 (720p)", "1920x1080 (1080p)", "2560x1440 (1440p)", "3840x2160 (4K)", "Native Host Display"});
    resolutionCombo->setCurrentIndex(1);

    framerateCombo = new QComboBox(this);
    framerateCombo->addItems({"30 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS"});
    framerateCombo->setCurrentIndex(1);

    aspectRatioCombo = new QComboBox(this);
    aspectRatioCombo->addItems({"16:9 Standard", "16:10 Widescreen", "21:9 Ultrawide", "32:9 Super Ultrawide"});

    QHBoxLayout *bitrateLayout = new QHBoxLayout();
    bitrateSlider = new QSlider(Qt::Horizontal, this);
    bitrateSlider->setRange(1, 150);
    bitrateSlider->setValue(45);
    bitrateSpinBox = new QSpinBox(this);
    bitrateSpinBox->setRange(1, 150);
    bitrateSpinBox->setValue(45);
    bitrateSpinBox->setSuffix(" Mbps");

    bitrateLayout->addWidget(bitrateSlider);
    bitrateLayout->addWidget(bitrateSpinBox);

    connect(bitrateSlider, &QSlider::valueChanged, this, &MainWindow::handleBitrateSliderChange);
    connect(bitrateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::handleBitrateSpinBoxChange);

    codecCombo = new QComboBox(this);
    codecCombo->addItems({"H.264 / AVC", "H.265 / HEVC", "AV1 Next-Gen"});
    codecCombo->setCurrentIndex(1);

    hwDecodingCheckBox = new QCheckBox("Enable Hardware Acceleration (NVDEC / QuickSync / AMF)", this);
    hwDecodingCheckBox->setChecked(true);

    vsyncCheckBox = new QCheckBox("Enable VSync & Frame Pacing", this);
    vsyncCheckBox->setChecked(true);

    formLayout->addRow("Stream Resolution:", resolutionCombo);
    formLayout->addRow("Target Frame Rate:", framerateCombo);
    formLayout->addRow("Aspect Ratio:", aspectRatioCombo);
    formLayout->addRow("Target Bitrate:", bitrateLayout);
    formLayout->addRow("Video Codec:", codecCombo);
    formLayout->addRow("Hardware Acceleration:", hwDecodingCheckBox);
    formLayout->addRow("VSync Sync:", vsyncCheckBox);

    layout->addWidget(displayGroup);

    QGroupBox *duoGroup = new QGroupBox("DuoStream (Virtual Displays)", tab);
    QVBoxLayout *duoLayout = new QVBoxLayout(duoGroup);

    QHBoxLayout *instMgmtLayout = new QHBoxLayout();
    duoInstanceCombo = new QComboBox(this);
    QPushButton *newInstBtn = new QPushButton("New", this);
    QPushButton *deleteInstBtn = new QPushButton("Delete", this);
    QPushButton *startInstBtn = new QPushButton("Start", this);
    QPushButton *stopInstBtn = new QPushButton("Stop", this);
    QPushButton *openSunshineBtn = new QPushButton("Open Sunshine", this);
    QPushButton *pairClientBtn = new QPushButton("Pair Client", this);

    instMgmtLayout->addWidget(new QLabel("Active Instance:", this));
    instMgmtLayout->addWidget(duoInstanceCombo, 1);
    instMgmtLayout->addWidget(newInstBtn);
    instMgmtLayout->addWidget(deleteInstBtn);
    instMgmtLayout->addWidget(startInstBtn);
    instMgmtLayout->addWidget(stopInstBtn);
    instMgmtLayout->addWidget(openSunshineBtn);
    instMgmtLayout->addWidget(pairClientBtn);
    duoLayout->addLayout(instMgmtLayout);

    QHBoxLayout *columnsLayout = new QHBoxLayout();
    QVBoxLayout *leftCol = new QVBoxLayout();

    QGroupBox *globalSettingsGroup = new QGroupBox("Global Settings", this);
    QFormLayout *globalForm = new QFormLayout(globalSettingsGroup);
    duoStartWithWindowsCheck = new QCheckBox("Start with Windows", this);
    duoHidIsolationCheck = new QCheckBox("HID Isolation", this);
    duoProcessPatchingCheck = new QCheckBox("Process Patching", this);
    duoIsolateStreamCheck = new QCheckBox("Isolate Stream", this);
    duoWebPortEdit = new QLineEdit(this);
    duoWebPortEdit->setPlaceholderText("38299");
    duoVerbosityCombo = new QComboBox(this);
    duoVerbosityCombo->addItems({"Error", "Warning", "Info", "Debug"});
    duoVerbosityCombo->setCurrentIndex(2);

    globalForm->addRow(duoStartWithWindowsCheck);
    globalForm->addRow(duoHidIsolationCheck);
    globalForm->addRow(duoProcessPatchingCheck);
    globalForm->addRow(duoIsolateStreamCheck);
    globalForm->addRow("Web UI Port:", duoWebPortEdit);
    globalForm->addRow("Log Verbosity:", duoVerbosityCombo);
    leftCol->addWidget(globalSettingsGroup);

    QGroupBox *userSettingsGroup = new QGroupBox("User Settings", this);
    QFormLayout *userForm = new QFormLayout(userSettingsGroup);
    duoUserCombo = new QComboBox(this);
    duoPasswordEdit = new QLineEdit(this);
    duoPasswordEdit->setEchoMode(QLineEdit::Password);
    QPushButton *testCredsBtn = new QPushButton("Test User Credentials", this);

    userForm->addRow("User Name:", duoUserCombo);
    userForm->addRow("Password:", duoPasswordEdit);
    userForm->addRow(testCredsBtn);
    leftCol->addWidget(userSettingsGroup);

    columnsLayout->addLayout(leftCol);

    QVBoxLayout *rightCol = new QVBoxLayout();

    QGroupBox *instSettingsGroup = new QGroupBox("Instance Settings", this);
    QFormLayout *instForm = new QFormLayout(instSettingsGroup);
    duoDisplayNameEdit = new QLineEdit(this);
    duoPortEdit = new QLineEdit(this);
    duoPortEdit->setPlaceholderText("44282");
    duoStartWithServiceCheck = new QCheckBox("Start with Service", this);
    QPushButton *autoStartAppsBtn = new QPushButton("AutoStart Applications", this);

    instForm->addRow("Display Name:", duoDisplayNameEdit);
    instForm->addRow("Port:", duoPortEdit);
    instForm->addRow(duoStartWithServiceCheck);
    instForm->addRow(autoStartAppsBtn);
    rightCol->addWidget(instSettingsGroup);

    QGroupBox *dispSettingsGroup = new QGroupBox("Display Settings", this);
    QFormLayout *dispForm = new QFormLayout(dispSettingsGroup);
    duoAdapterCombo = new QComboBox(this);
    duoScaleTypeCombo = new QComboBox(this);
    duoScaleTypeCombo->addItems({"Dynamic", "Static"});

    QHBoxLayout *scaleSliderLayout = new QHBoxLayout();
    duoScaleSlider = new QSlider(Qt::Horizontal, this);
    duoScaleSlider->setRange(100, 200);
    duoScaleSlider->setValue(100);
    duoScaleLabel = new QLabel("100%", this);
    scaleSliderLayout->addWidget(duoScaleSlider);
    scaleSliderLayout->addWidget(duoScaleLabel);

    QHBoxLayout *ssSliderLayout = new QHBoxLayout();
    duoSuperSamplingSlider = new QSlider(Qt::Horizontal, this);
    duoSuperSamplingSlider->setRange(100, 200);
    duoSuperSamplingSlider->setValue(100);
    duoSuperSamplingLabel = new QLabel("100%", this);
    ssSliderLayout->addWidget(duoSuperSamplingSlider);
    ssSliderLayout->addWidget(duoSuperSamplingLabel);

    QHBoxLayout *whiteSliderLayout = new QHBoxLayout();
    duoSdrWhiteLevelSlider = new QSlider(Qt::Horizontal, this);
    duoSdrWhiteLevelSlider->setRange(80, 480);
    duoSdrWhiteLevelSlider->setValue(80);
    duoSdrWhiteLevelLabel = new QLabel("80 nits", this);
    whiteSliderLayout->addWidget(duoSdrWhiteLevelSlider);
    whiteSliderLayout->addWidget(duoSdrWhiteLevelLabel);

    duoForceSdrCheck = new QCheckBox("Force SDR in HDR Streams", this);

    dispForm->addRow("Render Adapter:", duoAdapterCombo);
    dispForm->addRow("Desktop Scale:", duoScaleTypeCombo);
    dispForm->addRow("Static Scale:", scaleSliderLayout);
    dispForm->addRow("Super-Sampling:", ssSliderLayout);
    dispForm->addRow("SDR White-Level:", whiteSliderLayout);
    dispForm->addRow(duoForceSdrCheck);
    rightCol->addWidget(dispSettingsGroup);

    columnsLayout->addLayout(rightCol);
    duoLayout->addLayout(columnsLayout);
    layout->addWidget(duoGroup);

    layout->addStretch();

    connect(duoInstanceCombo, &QComboBox::currentTextChanged, this, &MainWindow::handleDuoInstanceChanged);
    connect(newInstBtn, &QPushButton::clicked, this, &MainWindow::handleDuoNewInstance);
    connect(deleteInstBtn, &QPushButton::clicked, this, &MainWindow::handleDuoDeleteInstance);
    connect(startInstBtn, &QPushButton::clicked, this, &MainWindow::handleDuoStartInstance);
    connect(stopInstBtn, &QPushButton::clicked, this, &MainWindow::handleDuoStopInstance);
    connect(openSunshineBtn, &QPushButton::clicked, this, &MainWindow::handleDuoOpenSunshine);
    connect(pairClientBtn, &QPushButton::clicked, this, &MainWindow::handleDuoPairClient);
    connect(testCredsBtn, &QPushButton::clicked, this, &MainWindow::handleDuoTestCredentials);
    connect(autoStartAppsBtn, &QPushButton::clicked, this, &MainWindow::handleDuoAutoStartApps);
    connect(duoScaleTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::handleDuoScaleTypeChanged);

    connect(duoScaleSlider, &QSlider::valueChanged, this, [this](int val) { duoScaleLabel->setText(QString("%1%").arg(val)); });
    connect(duoSuperSamplingSlider, &QSlider::valueChanged, this, [this](int val) { duoSuperSamplingLabel->setText(QString("%1%").arg(val)); });
    connect(duoSdrWhiteLevelSlider, &QSlider::valueChanged, this, [this](int val) { duoSdrWhiteLevelLabel->setText(QString("%1 nits").arg(val)); });

    return tab;
}

QWidget* MainWindow::createSettingsTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QGroupBox *settingsGroup = new QGroupBox("Application Preferences", tab);
    QFormLayout *formLayout = new QFormLayout(settingsGroup);

    themeCombo = new QComboBox(this);
    themeCombo->addItems({"Dark", "Light", "High Contrast"});

    minimizeToTrayCheckBox = new QCheckBox("Minimize to system tray on close", this);
    autoConnectCheckBox = new QCheckBox("Auto-connect to previously paired host on startup", this);

    profileCombo = new QComboBox(this);
    QPushButton *newProfileBtn = new QPushButton("New Profile", this);

    QHBoxLayout *profileLayout = new QHBoxLayout();
    profileLayout->addWidget(profileCombo, 1);
    profileLayout->addWidget(newProfileBtn);

    formLayout->addRow("UI Theme:", themeCombo);
    formLayout->addRow("Tray Behavior:", minimizeToTrayCheckBox);
    formLayout->addRow("Auto Connection:", autoConnectCheckBox);
    formLayout->addRow("Active Profile:", profileLayout);

    layout->addWidget(settingsGroup);
    layout->addStretch();

    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::handleThemeChange);
    connect(profileCombo, &QComboBox::currentTextChanged, this, &MainWindow::handleProfileChange);
    connect(newProfileBtn, &QPushButton::clicked, this, &MainWindow::handleNewProfile);

    return tab;
}

void MainWindow::handleConnect() {
    QString ip = hostIpLineEdit->text().trimmed();
    bool ok = false;
    quint16 port = portLineEdit->text().toUShort(&ok);

    if (!ok || !validatePort(port)) {
        return;
    }

    if (isLogicallyConnected) {
        isLogicallyConnected = false;
        connectButton->setText("Connect");
        connectionStatusLabel->setText("Status: Disconnected");
        connectionStatusLabel->setStyleSheet("color: #ff3366; font-weight: bold;");
        logWindow->appendLog("INFO", "Disconnected from host by user request.");
        return;
    }

    logWindow->appendLog("INFO", QString("Connecting to %1:%2 via libusbip...").arg(ip).arg(port));
    connectButton->setEnabled(false);
    connectButton->setText("Connecting...");

    QThread *thread = QThread::create([this, ip, port]() {
        usbip::Socket sock = usbip::connect(
            ip.toStdString().c_str(),
            QString::number(port).toStdString().c_str()
        );

        QMetaObject::invokeMethod(this, [this, sock = std::move(sock), ip, port]() mutable {
            connectButton->setEnabled(true);
            if (!sock) {
                DWORD err = GetLastError();
                logWindow->appendLog("ERROR", QString("usbip::connect failed (error %1). Host unreachable.").arg(err));
                connectionStatusLabel->setText("Status: Disconnected");
                connectionStatusLabel->setStyleSheet("color: #ff3366; font-weight: bold;");
                connectButton->setText("Connect");
            } else {
                isLogicallyConnected = true;
                connectButton->setText("Disconnect");
                connectionStatusLabel->setText("<font color='green'>Status: Connected (Logical)</font>");
                connectionStatusLabel->setStyleSheet("font-weight: bold;");
                logWindow->appendLog("INFO", QString("Connected to %1:%2.").arg(ip).arg(port));
                saveSettings();
            }
        });
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void MainWindow::handleScanHost() {
    if (!isLogicallyConnected) {
        logWindow->appendLog("ERROR", "Cannot scan: not logically connected to a host. Click 'Connect' first.");
        return;
    }

    QString ip = hostIpLineEdit->text().trimmed();
    bool ok = false;
    quint16 port = portLineEdit->text().toUShort(&ok);
    if (!ok || !validatePort(port)) {
        return;
    }

    logWindow->appendLog("INFO", "Scanning exportable devices via libusbip...");

    usbip::Socket sock = usbip::connect(
        ip.toStdString().c_str(),
        portLineEdit->text().trimmed().toStdString().c_str()
    );

    if (!sock) {
        DWORD err = GetLastError();
        logWindow->appendLog("ERROR", QString("Scan connect failed (error %1).").arg(err));
        return;
    }

    QList<usbip::usb_device> devices;

    bool enumOk = usbip::enum_exportable_devices(
        sock.get(),
        [&devices](int, const usbip::usb_device &dev) {
            devices.append(dev);
        },
        [](int, const usbip::usb_device &, int, const usbip::usb_interface &) {},
        nullptr
    );

    if (!enumOk) {
        DWORD err = GetLastError();
        logWindow->appendLog("ERROR", QString("enum_exportable_devices failed (error %1).").arg(err));
        return;
    }

    if (devices.isEmpty()) {
        QMessageBox::information(this, "Scan Result", "No exportable USB devices found on the remote host.");
        logWindow->appendLog("INFO", "Scan complete: no devices found.");
        return;
    }

    usbDeviceTable->setRowCount(0);

    for (const usbip::usb_device &dev : devices) {
        QString busid   = QString::fromStdString(dev.busid);
        QString vidPid  = QString("%1:%2")
                            .arg(dev.idVendor,  4, 16, QChar('0'))
                            .arg(dev.idProduct, 4, 16, QChar('0'))
                            .toUpper();
        QString name    = getFriendlyDeviceName(dev.idVendor, dev.idProduct);
        int attachedPort = findAttachedPort(busid);
        bool attached   = (attachedPort >= 1);
        QString status  = attached ? "Attached (Native)" : "Available";

        addUsbDeviceToTable(name, busid, vidPid, status, attached);
        logWindow->appendLog("INFO", QString("Found: %1  [%2]  %3").arg(busid, vidPid, name));
    }

    logWindow->appendLog("INFO", QString("Scan complete: %1 device(s) found.").arg(devices.size()));
}

void MainWindow::handleToggleLogWindow() {
    if (logWindow->isVisible()) {
        logWindow->hide();
    } else {
        logWindow->show();
        logWindow->raise();
    }
}

void MainWindow::handleToggleDeviceAttach(int row) {
    if (row < 0 || row >= usbDeviceTable->rowCount()) return;

    QString busid = usbDeviceTable->item(row, 0)->data(Qt::UserRole).toString();
    QPushButton *btn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 3));
    if (!btn) return;

    QString ip   = hostIpLineEdit->text().trimmed();
    QString port = portLineEdit->text().trimmed();

    try {
        if (btn->text() == "Detach") {
            int hubPort = findAttachedPort(busid);
            if (hubPort < 1) {
                QMessageBox::warning(this, "Detach Failed", QString("Device %1 is not recorded as attached.").arg(busid));
                logWindow->appendLog("WARNING", QString("Device %1 is not recorded as attached; skipping detach.").arg(busid));
                return;
            }

            usbip::Handle dev = usbip::vhci::open();
            if (!dev) {
                QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver. Is the UDE driver loaded?");
                logWindow->appendLog("ERROR", "vhci::open() failed — is the UDE driver loaded?");
                return;
            }

            logWindow->appendLog("INFO", QString("Detaching bus %1 (port %2)...").arg(busid).arg(hubPort));

            if (!usbip::vhci::detach(dev.get(), hubPort)) {
                DWORD err = GetLastError();
                QMessageBox::warning(this, "Detach Failed", QString("Failed to detach device on bus %1. Error code: %2").arg(busid).arg(err));
                logWindow->appendLog("ERROR", QString("vhci::detach() failed (error %1).").arg(err));
                return;
            }

            attachedPorts.remove(busid);
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 2)->setText("Available");
                btn->setText("Attach");
            }
            logWindow->appendLog("INFO", QString("Detached device on bus %1.").arg(busid));
            return;
        }

        // Attach path
        usbip::Handle dev = usbip::vhci::open();
        if (!dev) {
            QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver. Is the UDE driver loaded?");
            logWindow->appendLog("ERROR", "vhci::open() failed — is the UDE driver loaded?");
            return;
        }

        usbip::vhci::attach_args args;
        args.location.hostname = ip.toStdString();
        args.location.service  = port.toStdString();
        args.location.busid    = busid.toStdString();
        args.once              = true;

        logWindow->appendLog("INFO", QString("Attaching bus %1 from %2...").arg(busid, ip));

        int hubPort = usbip::vhci::attach(dev.get(), args);
        if (hubPort < 1) {
            DWORD err = GetLastError();
            QMessageBox::warning(this, "Attach Failed", QString("Failed to attach device on bus %1. Error code: %2").arg(busid).arg(err));
            logWindow->appendLog("ERROR", QString("vhci::attach() failed (error %1).").arg(err));
            return;
        }

        attachedPorts[busid] = hubPort;
        if (row < usbDeviceTable->rowCount()) {
            usbDeviceTable->item(row, 2)->setText("Attached (Native)");
            btn->setText("Detach");
        }
        logWindow->appendLog("INFO", QString("Attached bus %1 on hub port %2.").arg(busid).arg(hubPort));

    } catch (const std::exception &ex) {
        QMessageBox::warning(this, "Error", QString("SDK exception in attach/detach: %1").arg(ex.what()));
        logWindow->appendLog("ERROR", QString("SDK exception in attach/detach: %1").arg(ex.what()));
    } catch (...) {
        QMessageBox::warning(this, "Error", "Unknown SDK exception in attach/detach.");
        logWindow->appendLog("ERROR", "Unknown SDK exception in attach/detach.");
    }
}

void MainWindow::handleResetDeviceConnection(int row) {
    if (row < 0 || row >= usbDeviceTable->rowCount()) return;

    QString busid = usbDeviceTable->item(row, 0)->data(Qt::UserRole).toString();
    QString ip    = hostIpLineEdit->text().trimmed();
    QString port  = portLineEdit->text().trimmed();

    logWindow->appendLog("INFO", QString("Resetting connection for bus %1 — detaching...").arg(busid));

    try {
        int hubPort = findAttachedPort(busid);
        if (hubPort < 1) {
            QMessageBox::warning(this, "Reset Failed", QString("Bus %1 not found in attached ports; skipping detach.").arg(busid));
            logWindow->appendLog("WARNING", QString("Bus %1 not found in attached ports; skipping detach.").arg(busid));
        } else {
            usbip::Handle dev = usbip::vhci::open();
            if (!dev) {
                QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver during reset.");
                logWindow->appendLog("ERROR", "vhci::open() failed during reset.");
                return;
            }
            if (!usbip::vhci::detach(dev.get(), hubPort)) {
                DWORD err = GetLastError();
                QMessageBox::warning(this, "Reset Failed", QString("Failed to detach device during reset. Error code: %1").arg(err));
                logWindow->appendLog("ERROR", QString("vhci::detach() failed during reset (error %1).").arg(err));
                return;
            }
            attachedPorts.remove(busid);
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 2)->setText("Available");
                if (QPushButton *b = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 3)))
                    b->setText("Attach");
            }
            logWindow->appendLog("INFO", QString("Detached bus %1 for reset.").arg(busid));
        }
    } catch (const std::exception &ex) {
        QMessageBox::warning(this, "Error", QString("SDK exception during reset detach: %1").arg(ex.what()));
        logWindow->appendLog("ERROR", QString("SDK exception during reset detach: %1").arg(ex.what()));
        return;
    }

    // Re-attach after 2 seconds
    QTimer::singleShot(2000, this, [this, ip, port, busid, row]() {
        logWindow->appendLog("INFO", QString("Re-attaching bus %1 to %2...").arg(busid, ip));
        try {
            usbip::Handle dev = usbip::vhci::open();
            if (!dev) {
                QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver during reset re-attach.");
                logWindow->appendLog("ERROR", "vhci::open() failed during reset re-attach.");
                return;
            }

            usbip::vhci::attach_args args;
            args.location.hostname = ip.toStdString();
            args.location.service  = port.toStdString();
            args.location.busid    = busid.toStdString();
            args.once              = true;

            int hubPort = usbip::vhci::attach(dev.get(), args);
            if (hubPort < 1) {
                DWORD err = GetLastError();
                QMessageBox::warning(this, "Reconnection Failed", QString("Failed to reconnect device. Error code: %1").arg(err));
                logWindow->appendLog("ERROR", QString("vhci::attach() failed during reset (error %1).").arg(err));
                return;
            }

            attachedPorts[busid] = hubPort;
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 2)->setText("Attached (Native)");
                if (QPushButton *b = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 3)))
                    b->setText("Detach");
            }
            logWindow->appendLog("INFO", QString("Re-attached bus %1 on hub port %2.").arg(busid).arg(hubPort));
        } catch (const std::exception &ex) {
            QMessageBox::warning(this, "Error", QString("SDK exception during reset re-attach: %1").arg(ex.what()));
            logWindow->appendLog("ERROR", QString("SDK exception during reset re-attach: %1").arg(ex.what()));
        }
    });
}

void MainWindow::handleResetAudioSubsystem() {
    logWindow->appendLog("WARNING", "Resetting Audio Relay subsystem pipelines...");
    enableAudioRelayButton->setChecked(false);
}

void MainWindow::handleThemeChange(int index) {
    QString theme = themeCombo->itemText(index);
    applyTheme(theme);
    logWindow->appendLog("INFO", QString("UI Theme changed to %1").arg(theme));
}

void MainWindow::handleNewProfile() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Profile", "Enter profile name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    
    name = name.trimmed();
    if (profileCombo->findText(name) >= 0) {
        QMessageBox::warning(this, "Profile Exists", "A profile with this name already exists.");
        return;
    }
    
    profileCombo->addItem(name);
    profileCombo->setCurrentText(name);
}

void MainWindow::handleProfileChange(const QString &profileName) {
    if (profileName.isEmpty() || profileName == currentProfile) return;
    
    saveProfileSettings(currentProfile);
    currentProfile = profileName;
    loadProfileSettings(currentProfile);
    
    QSettings settings("OmniStream", "OmniStreamClient");
    settings.setValue("profiles/active", currentProfile);
    
    logWindow->appendLog("INFO", QString("Switched to profile: %1").arg(profileName));
}

void MainWindow::handleBitrateSliderChange(int value) {
    bitrateSpinBox->blockSignals(true);
    bitrateSpinBox->setValue(value);
    bitrateSpinBox->blockSignals(false);
}

void MainWindow::handleBitrateSpinBoxChange(int value) {
    bitrateSlider->blockSignals(true);
    bitrateSlider->setValue(value);
    bitrateSlider->blockSignals(false);
}

void MainWindow::applyTheme(const QString &themeName) {
    if (themeName == "Dark") {
        setStyleSheet(
            "QMainWindow, QDialog { background-color: #12141d; color: #e0e6ed; }"
            "QTabWidget::pane { border: 1px solid #23273a; background: #1a1d2e; }"
            "QTabBar::tab { background: #12141d; color: #8a99ad; padding: 8px 16px; border: 1px solid #23273a; }"
            "QTabBar::tab:selected { background: #1a1d2e; color: #00f2fe; border-bottom: 2px solid #00f2fe; }"
            "QGroupBox { border: 1px solid #23273a; margin-top: 10px; font-weight: bold; color: #00f2fe; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
            "QPushButton { background-color: #23273a; color: #ffffff; border: 1px solid #343b54; padding: 6px 12px; border-radius: 4px; }"
            "QPushButton:hover { background-color: #343b54; border-color: #00f2fe; }"
            "QLineEdit, QComboBox, QSpinBox { background-color: #0d0e15; border: 1px solid #23273a; color: #ffffff; padding: 4px; border-radius: 4px; }"
            "QComboBox QAbstractItemView { background-color: #0d0e15; color: #ffffff; border: 1px solid #23273a; selection-background-color: #00f2fe; selection-color: #12141d; }"
            "QTableWidget { background-color: #0d0e15; gridline-color: #23273a; color: #ffffff; }"
            "QHeaderView::section { background-color: #12141d; color: #00f2fe; border: 1px solid #23273a; padding: 4px; }"
            "QLabel, QCheckBox { color: #e0e6ed; }"
        );
    } else if (themeName == "Light") {
        setStyleSheet(
            "QMainWindow, QDialog { background-color: #f1f5f9; color: #0f172a; }"
            "QTabWidget::pane { border: 1px solid #cbd5e1; background: #ffffff; }"
            "QTabBar::tab { background: #e2e8f0; color: #64748b; padding: 8px 16px; border: 1px solid #cbd5e1; }"
            "QTabBar::tab:selected { background: #ffffff; color: #2563eb; border-bottom: 2px solid #2563eb; }"
            "QGroupBox { border: 1px solid #cbd5e1; margin-top: 10px; font-weight: bold; color: #2563eb; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
            "QPushButton { background-color: #e2e8f0; color: #0f172a; border: 1px solid #cbd5e1; padding: 6px 12px; border-radius: 4px; }"
            "QPushButton:hover { background-color: #cbd5e1; border-color: #2563eb; }"
            "QLineEdit, QComboBox, QSpinBox { background-color: #ffffff; border: 1px solid #cbd5e1; color: #0f172a; padding: 4px; border-radius: 4px; }"
            "QComboBox QAbstractItemView { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; selection-background-color: #2563eb; selection-color: #ffffff; }"
            "QTableWidget { background-color: #ffffff; gridline-color: #cbd5e1; color: #0f172a; }"
            "QHeaderView::section { background-color: #f8fafc; color: #2563eb; border: 1px solid #cbd5e1; padding: 4px; }"
            "QLabel, QCheckBox { color: #0f172a; }"
        );
    } else if (themeName == "High Contrast") {
        setStyleSheet(
            "QMainWindow, QDialog { background-color: #000000; color: #ffffff; }"
            "QTabWidget::pane { border: 2px solid #ffffff; background: #000000; }"
            "QTabBar::tab { background: #000000; color: #ffffff; padding: 8px 16px; border: 2px solid #ffffff; }"
            "QTabBar::tab:selected { background: #000000; color: #ffff00; border-bottom: 2px solid #ffff00; }"
            "QGroupBox { border: 2px solid #ffffff; margin-top: 10px; font-weight: bold; color: #ffff00; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
            "QPushButton { background-color: #000000; color: #ffffff; border: 2px solid #ffffff; padding: 6px 12px; border-radius: 4px; }"
            "QPushButton:hover { background-color: #ffffff; color: #000000; border-color: #ffff00; }"
            "QLineEdit, QComboBox, QSpinBox { background-color: #000000; border: 2px solid #ffffff; color: #ffffff; padding: 4px; border-radius: 4px; }"
            "QComboBox QAbstractItemView { background-color: #000000; color: #ffffff; border: 2px solid #ffffff; selection-background-color: #ffff00; selection-color: #000000; }"
            "QTableWidget { background-color: #000000; gridline-color: #ffffff; color: #ffffff; }"
            "QHeaderView::section { background-color: #000000; color: #ffff00; border: 2px solid #ffffff; padding: 4px; }"
            "QLabel, QCheckBox { color: #ffffff; }"
        );
    }
}

int MainWindow::findAttachedPort(const QString &busid) const {
    return attachedPorts.value(busid, -1);
}

QString MainWindow::getDriverPath() {
    QSettings settings("OmniStream", "OmniStreamClient");
    QString defaultPath = QCoreApplication::applicationDirPath() + "/Drivers";
    QString configuredPath = settings.value("paths/drivers", defaultPath).toString();

    QFileInfo checkExe(configuredPath + "/usbip.exe");
    if (!checkExe.exists()) {
        configuredPath = defaultPath;
        checkExe.setFile(configuredPath + "/usbip.exe");
    }

    if (!checkExe.exists()) {
        logWindow->appendLog("WARNING", "usbip.exe not found in configured path. Prompting user for directory...");
        QString selectedDir = QFileDialog::getExistingDirectory(this, "Select usbip-win2 Drivers & Binaries Directory", defaultPath);
        if (!selectedDir.isEmpty()) {
            settings.setValue("paths/drivers", selectedDir);
            return selectedDir;
        }
    } else {
        settings.setValue("paths/drivers", configuredPath);
    }
    return configuredPath;
}

bool MainWindow::checkAndConfigureDrivers() {
    QSettings udeService("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\usbip2_ude", QSettings::NativeFormat);
    QSettings filterService("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\usbip2_filter", QSettings::NativeFormat);

    bool driversInstalled = udeService.contains("ImagePath") && filterService.contains("ImagePath");
    QString driverDir = getDriverPath();

    if (!driversInstalled) {
        logWindow->appendLog("INFO", "USB/IP kernel driver services not detected in registry. Installing...");
        QString udeInf    = driverDir + "/usbip2_ude.inf";
        QString filterInf = driverDir + "/usbip2_filter.inf";

        QProcess::execute("InfDefaultInstall.exe", QStringList() << udeInf);
        QProcess::execute("InfDefaultInstall.exe", QStringList() << filterInf);
        logWindow->appendLog("INFO", "Driver installation sequence executed.");
    } else {
        logWindow->appendLog("INFO", "USB/IP kernel drivers are already registered in the system.");
    }

    // Ensure the ROOT\USBIP_WIN2\UDE virtual host controller node exists
    QString usbipPath = driverDir + "/usbip.exe";
    QProcess *installProc = new QProcess(this);
    installProc->setWorkingDirectory(driverDir);

    connect(installProc, &QProcess::readyReadStandardOutput, this, [this, installProc]() {
        QString out = QString::fromUtf8(installProc->readAllStandardOutput()).trimmed();
        if (!out.isEmpty())
            logWindow->appendLog("USBIP-CORE", out);
    });
    connect(installProc, &QProcess::readyReadStandardError, this, [this, installProc]() {
        QString err = QString::fromUtf8(installProc->readAllStandardError()).trimmed();
        if (!err.isEmpty())
            logWindow->appendLog("USBIP-CORE", err);
    });
    connect(installProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, installProc](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0)
            logWindow->appendLog("INFO", "usbip install: ROOT\\USBIP_WIN2\\UDE node verified/created.");
        else
            logWindow->appendLog("WARNING", QString("usbip install exited with code %1.").arg(exitCode));
        installProc->deleteLater();
    });

    logWindow->appendLog("INFO", "Running 'usbip install' to verify virtual host controller node...");
    installProc->start(usbipPath, QStringList() << "install");

    return true;
}

QString MainWindow::getFriendlyDeviceName(quint16 vendorId, quint16 productId) {
    QString key = QString("%1:%2")
                    .arg(vendorId, 4, 16, QChar('0'))
                    .arg(productId, 4, 16, QChar('0'))
                    .toUpper();

    if (usbDeviceDb.contains(key)) {
        return usbDeviceDb.value(key);
    }
    return QString("Unknown Device [%1]").arg(key);
}

void MainWindow::loadUsbIdDatabase() {
    QString dbPath = QCoreApplication::applicationDirPath() + "/usb.ids";
    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logWindow->appendLog("WARNING", "Failed to open usb.ids database at " + dbPath);
        return;
    }

    QTextStream in(&file);
    QString currentVid;
    int count = 0;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("C ") || line.startsWith("\t\t")) continue;
        if (line.startsWith('\t')) {
            QString pid = line.mid(1, 4).toUpper();
            QString name = line.mid(6).trimmed();
            usbDeviceDb[currentVid + ":" + pid] = name;
            count++;
        } else {
            currentVid = line.left(4).toUpper();
        }
    }

    usbDeviceDb["05C6:F000"] = "Qualcomm HS-USB (Essential PH-1)";
    logWindow->appendLog("INFO", QString("Loaded %1 USB devices from database.").arg(count));
}

void MainWindow::syncDeviceState() {
    try {
        usbip::Handle dev = usbip::vhci::open();
        if (!dev) {
            return;
        }

        auto importedOpt = usbip::vhci::get_imported_devices(dev.get());
        if (!importedOpt) {
            return;
        }

        const auto &imported = *importedOpt;

        attachedPorts.clear();
        for (const auto &device : imported) {
            QString busid = QString::fromStdString(device.location.busid);
            attachedPorts[busid] = device.port;
        }

        for (int row = 0; row < usbDeviceTable->rowCount(); ++row) {
            QString busid = usbDeviceTable->item(row, 0)->data(Qt::UserRole).toString();
            bool found = attachedPorts.contains(busid);

            QPushButton *btn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 3));
            if (found) {
                usbDeviceTable->setItem(row, 2, new QTableWidgetItem("Attached (Native)"));
                usbDeviceTable->item(row, 2)->setForeground(QBrush(QColor("#00ffcc")));
                if (btn) btn->setText("Detach");
            } else {
                usbDeviceTable->setItem(row, 2, new QTableWidgetItem("Available"));
                if (btn) btn->setText("Attach");
            }
        }
    } catch (...) {
    }
}

void MainWindow::handleDuoInstanceChanged(const QString &name) {
    if (name.isEmpty()) return;
    loadDuoInstanceSettings(name);
}

void MainWindow::handleDuoNewInstance() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Instance", "Enter instance name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    
    name = name.trimmed();
    if (duoInstanceCombo->findText(name) >= 0) {
        QMessageBox::warning(this, "Instance Exists", "An instance with this name already exists.");
        return;
    }
    
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo\\Instances\\" + name, QSettings::NativeFormat);
    settings.setValue("DisplayName", name);
    settings.setValue("Port", 44282);
    settings.setValue("Enabled", 1);
    settings.setValue("Sandboxed", 0);
    settings.setValue("ScaleFactor", 100);
    settings.setValue("SuperSamplingFactor", 100);
    settings.setValue("ForceSDR", 0);
    settings.setValue("SDRWhiteLevel", 80);
    settings.setValue("HideVMSuffix", 0);
    settings.setValue("IsScaleFactorStatic", 1);
    
    duoInstanceCombo->addItem(name);
    duoInstanceCombo->setCurrentText(name);
}

void MainWindow::handleDuoDeleteInstance() {
    QString name = duoInstanceCombo->currentText();
    if (name.isEmpty()) return;
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Delete Instance", QString("Are you sure you want to delete instance '%1'?").arg(name),
        QMessageBox::Yes | QMessageBox::No
    );
    if (reply != QMessageBox::Yes) return;
    
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo\\Instances", QSettings::NativeFormat);
    settings.remove(name);
    
    loadDuoInstances();
}

void MainWindow::handleDuoStartInstance() {
    QString name = duoInstanceCombo->currentText();
    if (name.isEmpty()) return;
    
    saveDuoInstanceSettings(name);
    saveDuoGlobalSettings();
    
    QString port = duoWebPortEdit->text().trimmed();
    if (port.isEmpty()) port = "38299";
    QUrl url(QString("http://localhost:%1/instances/%2/start").arg(port, name));
    
    QNetworkAccessManager *mgr = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    QNetworkReply *reply = mgr->get(req);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        if (reply->error() == QNetworkReply::NoError) {
            logWindow->appendLog("INFO", QString("Successfully started Duo instance '%1'.").arg(name));
        } else {
            QMessageBox::warning(this, "Start Failed", QString("Failed to start instance '%1'. Error: %2").arg(name, reply->errorString()));
            logWindow->appendLog("ERROR", QString("Failed to start instance '%1': %2").arg(name, reply->errorString()));
        }
        reply->deleteLater();
    });
}

void MainWindow::handleDuoStopInstance() {
    QString name = duoInstanceCombo->currentText();
    if (name.isEmpty()) return;
    
    QString port = duoWebPortEdit->text().trimmed();
    if (port.isEmpty()) port = "38299";
    QUrl url(QString("http://localhost:%1/instances/%2/stop").arg(port, name));
    
    QNetworkAccessManager *mgr = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    QNetworkReply *reply = mgr->get(req);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        if (reply->error() == QNetworkReply::NoError) {
            logWindow->appendLog("INFO", QString("Successfully stopped Duo instance '%1'.").arg(name));
        } else {
            QMessageBox::warning(this, "Stop Failed", QString("Failed to stop instance '%1'. Error: %2").arg(name, reply->errorString()));
            logWindow->appendLog("ERROR", QString("Failed to stop instance '%1': %2").arg(name, reply->errorString()));
        }
        reply->deleteLater();
    });
}

void MainWindow::handleDuoOpenSunshine() {
    QString port = duoPortEdit->text().trimmed();
    if (port.isEmpty()) port = "44282";
    QUrl url(QString("https://localhost:%1").arg(port));
    QDesktopServices::openUrl(url);
}

void MainWindow::handleDuoPairClient() {
    QString port = duoPortEdit->text().trimmed();
    if (port.isEmpty()) port = "44282";
    QUrl url(QString("https://localhost:%1/pin").arg(port));
    QDesktopServices::openUrl(url);
}

void MainWindow::handleDuoTestCredentials() {
    QString username = duoUserCombo->currentText();
    QString password = duoPasswordEdit->text();
    
    logWindow->appendLog("INFO", QString("Testing credentials for user '%1'...").arg(username));
    
    HANDLE hToken = nullptr;
    bool success = LogonUserW(
        username.toStdWString().c_str(),
        L".",
        password.toStdWString().c_str(),
        LOGON32_LOGON_INTERACTIVE,
        LOGON32_PROVIDER_DEFAULT,
        &hToken
    );
    
    if (success) {
        CloseHandle(hToken);
        QMessageBox::information(this, "Credentials Valid", "User credentials are valid!");
        logWindow->appendLog("INFO", "User credentials verified successfully.");
    } else {
        DWORD err = GetLastError();
        QMessageBox::warning(this, "Credentials Invalid", QString("Failed to verify credentials. Error code: %1").arg(err));
        logWindow->appendLog("ERROR", QString("Failed to verify credentials. Error code: %1").arg(err));
    }
}

void MainWindow::handleDuoAutoStartApps() {
    QString path = "C:/Program Files/Duo/config/apps.json";
    QFileInfo checkFile(path);
    if (!checkFile.exists()) {
        path = QCoreApplication::applicationDirPath() + "/Duo/config/apps.json";
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::handleDuoScaleTypeChanged(int index) {
    bool isStatic = (index == 1);
    duoScaleSlider->setVisible(isStatic);
    duoScaleLabel->setVisible(isStatic);
}

void MainWindow::loadDuoInstances() {
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo\\Instances", QSettings::NativeFormat);
    QStringList childKeys = settings.childGroups();
    
    duoInstanceCombo->blockSignals(true);
    duoInstanceCombo->clear();
    duoInstanceCombo->addItems(childKeys);
    duoInstanceCombo->blockSignals(false);
    
    if (duoInstanceCombo->count() > 0) {
        duoInstanceCombo->setCurrentIndex(0);
        loadDuoInstanceSettings(duoInstanceCombo->currentText());
    } else {
        setDuoInstanceControlsEnabled(false);
    }
}

void MainWindow::loadDuoInstanceSettings(const QString &name) {
    if (name.isEmpty()) return;
    
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo\\Instances\\" + name, QSettings::NativeFormat);
    
    duoDisplayNameEdit->setText(settings.value("DisplayName", name).toString());
    duoPortEdit->setText(settings.value("Port", 44282).toString());
    duoStartWithServiceCheck->setChecked(settings.value("Enabled", 1).toInt() == 1);
    
    QString user = settings.value("UserName", "").toString();
    int userIndex = duoUserCombo->findText(user);
    if (userIndex >= 0) {
        duoUserCombo->setCurrentIndex(userIndex);
    }
    
    QString encryptedPass = settings.value("Password", "").toString();
    if (!encryptedPass.isEmpty()) {
        duoPasswordEdit->setText(decryptPassword(encryptedPass));
    } else {
        duoPasswordEdit->clear();
    }
    
    QString adapter = settings.value("RenderAdapter", "").toString();
    int adapterIndex = duoAdapterCombo->findText(adapter);
    if (adapterIndex >= 0) {
        duoAdapterCombo->setCurrentIndex(adapterIndex);
    }
    
    int isStatic = settings.value("IsScaleFactorStatic", 1).toInt();
    duoScaleTypeCombo->setCurrentIndex(isStatic == 1 ? 1 : 0);
    
    int scale = settings.value("ScaleFactor", 100).toInt();
    duoScaleSlider->setValue(scale);
    
    int superSampling = settings.value("SuperSamplingFactor", 100).toInt();
    duoSuperSamplingSlider->setValue(superSampling);
    
    int whiteLevel = settings.value("SDRWhiteLevel", 80).toInt();
    duoSdrWhiteLevelSlider->setValue(whiteLevel);
    
    duoForceSdrCheck->setChecked(settings.value("ForceSDR", 0).toInt() == 1);
    
    setDuoInstanceControlsEnabled(true);
    handleDuoScaleTypeChanged(isStatic == 1 ? 1 : 0);
}

void MainWindow::saveDuoInstanceSettings(const QString &name) {
    if (name.isEmpty()) return;
    
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo\\Instances\\" + name, QSettings::NativeFormat);
    
    settings.setValue("DisplayName", duoDisplayNameEdit->text().trimmed());
    settings.setValue("Port", duoPortEdit->text().toUInt());
    settings.setValue("Enabled", duoStartWithServiceCheck->isChecked() ? 1 : 0);
    settings.setValue("UserName", duoUserCombo->currentText());
    
    QString pass = duoPasswordEdit->text();
    if (!pass.isEmpty()) {
        settings.setValue("Password", encryptPassword(pass));
    } else {
        settings.setValue("Password", "");
    }
    
    settings.setValue("RenderAdapter", duoAdapterCombo->currentText());
    settings.setValue("IsScaleFactorStatic", duoScaleTypeCombo->currentIndex() == 1 ? 1 : 0);
    settings.setValue("ScaleFactor", duoScaleSlider->value());
    settings.setValue("SuperSamplingFactor", duoSuperSamplingSlider->value());
    settings.setValue("SDRWhiteLevel", duoSdrWhiteLevelSlider->value());
    settings.setValue("ForceSDR", duoForceSdrCheck->isChecked() ? 1 : 0);
}

void MainWindow::loadDuoGlobalSettings() {
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo", QSettings::NativeFormat);
    
    duoHidIsolationCheck->setChecked(settings.value("HostHidIsolation", 0).toInt() == 1);
    duoProcessPatchingCheck->setChecked(settings.value("EnableProcessPatching", 0).toInt() == 1);
    duoIsolateStreamCheck->setChecked(settings.value("SteamIsolation", 0).toInt() == 1);
    duoWebPortEdit->setText(settings.value("HostPort", 38299).toString());
    
    int verbosity = settings.value("Verbosity", 1).toInt();
    duoVerbosityCombo->setCurrentIndex(verbosity);
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenServiceW(hSCM, L"DuoService", SERVICE_QUERY_CONFIG);
        if (hService) {
            DWORD dwBytesNeeded = 0;
            QueryServiceConfigW(hService, nullptr, 0, &dwBytesNeeded);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                QUERY_SERVICE_CONFIGW *pConfig = (QUERY_SERVICE_CONFIGW*)LocalAlloc(LPTR, dwBytesNeeded);
                if (pConfig) {
                    if (QueryServiceConfigW(hService, pConfig, dwBytesNeeded, &dwBytesNeeded)) {
                        duoStartWithWindowsCheck->setChecked(pConfig->dwStartType == SERVICE_AUTO_START);
                    }
                    LocalFree(pConfig);
                }
            }
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }
}

void MainWindow::saveDuoGlobalSettings() {
    QSettings settings("HKEY_LOCAL_MACHINE\\SOFTWARE\\Duo", QSettings::NativeFormat);
    
    settings.setValue("HostHidIsolation", duoHidIsolationCheck->isChecked() ? 1 : 0);
    settings.setValue("EnableProcessPatching", duoProcessPatchingCheck->isChecked() ? 1 : 0);
    settings.setValue("SteamIsolation", duoIsolateStreamCheck->isChecked() ? 1 : 0);
    settings.setValue("HostPort", duoWebPortEdit->text().toUInt());
    settings.setValue("Verbosity", duoVerbosityCombo->currentIndex());
    
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenServiceW(hSCM, L"DuoService", SERVICE_CHANGE_CONFIG);
        if (hService) {
            ChangeServiceConfigW(
                hService,
                SERVICE_NO_CHANGE,
                duoStartWithWindowsCheck->isChecked() ? SERVICE_AUTO_START : SERVICE_DEMAND_START,
                SERVICE_NO_CHANGE,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
            );
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }
}

void MainWindow::populateDuoUsers() {
    duoUserCombo->clear();
    
    DWORD dwLevel = 0;
    LPUSER_INFO_0 pBuf = nullptr;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    
    NET_API_STATUS nStatus = NetUserEnum(
        nullptr,
        dwLevel,
        FILTER_NORMAL_ACCOUNT,
        (LPBYTE*)&pBuf,
        MAX_PREFERRED_LENGTH,
        &dwEntriesRead,
        &dwTotalEntries,
        &dwResumeHandle
    );
    
    if (nStatus == NERR_Success || nStatus == ERROR_MORE_DATA) {
        if (pBuf != nullptr) {
            for (DWORD i = 0; i < dwEntriesRead; i++) {
                duoUserCombo->addItem(QString::fromWCharArray(pBuf[i].usri0_name));
            }
        }
    }
    
    if (pBuf != nullptr) {
        NetApiBufferFree(pBuf);
    }
    
    if (duoUserCombo->count() == 0) {
        wchar_t username[UNLEN + 1];
        DWORD username_len = UNLEN + 1;
        if (GetUserNameW(username, &username_len)) {
            duoUserCombo->addItem(QString::fromWCharArray(username));
        } else {
            duoUserCombo->addItem("Administrator");
        }
    }
}

void MainWindow::populateDuoAdapters() {
    duoAdapterCombo->clear();
    
    IDXGIFactory1 *pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
        IDXGIAdapter1 *pAdapter = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);
            duoAdapterCombo->addItem(QString::fromWCharArray(desc.Description));
            pAdapter->Release();
        }
        pFactory->Release();
    }
    
    if (duoAdapterCombo->count() == 0) {
        duoAdapterCombo->addItem("Default Graphics Adapter");
    }
}

void MainWindow::setDuoInstanceControlsEnabled(bool enabled) {
    duoDisplayNameEdit->setEnabled(enabled);
    duoPortEdit->setEnabled(enabled);
    duoStartWithServiceCheck->setEnabled(enabled);
    duoUserCombo->setEnabled(enabled);
    duoPasswordEdit->setEnabled(enabled);
    duoAdapterCombo->setEnabled(enabled);
    duoScaleTypeCombo->setEnabled(enabled);
    duoScaleSlider->setEnabled(enabled);
    duoSuperSamplingSlider->setEnabled(enabled);
    duoSdrWhiteLevelSlider->setEnabled(enabled);
    duoForceSdrCheck->setEnabled(enabled);
}

QString MainWindow::encryptPassword(const QString &password) {
    DATA_BLOB input;
    QByteArray utf8 = password.toUtf8();
    input.pbData = (BYTE*)utf8.constData();
    input.cbData = utf8.length();
    
    DATA_BLOB output;
    if (CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
        QByteArray encrypted((char*)output.pbData, output.cbData);
        LocalFree(output.pbData);
        return encrypted.toBase64();
    }
    return "";
}

QString MainWindow::decryptPassword(const QString &base64) {
    QByteArray encrypted = QByteArray::fromBase64(base64.toUtf8());
    DATA_BLOB input;
    input.pbData = (BYTE*)encrypted.constData();
    input.cbData = encrypted.length();
    
    DATA_BLOB output;
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
        QString decrypted = QString::fromUtf8((char*)output.pbData, output.cbData);
        LocalFree(output.pbData);
        return decrypted;
    }
    return "";
}
