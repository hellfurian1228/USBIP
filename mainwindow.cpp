#include "mainwindow.h"
#include "audiorelaymanager.h"
#include "driverinstaller.h"
#include <algorithm>
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
#include <windows.h>
#include <lm.h>
#include <dxgi.h>
#include <wincrypt.h>

// usbip_sdk: vhci.h provides Handle/attach/detach, remote.h provides Socket/connect/enum_exportable_devices
#include <vhci.h>
#include <remote.h>
#include "src/usbip_sdk/libusbip/src/usb_ids.h"

#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "crypt32.lib")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("USBIP Client v1.0.1");
    resize(1000, 650);

    logWindow = new LogWindow(this);
    audioRelayManager = new AudioRelayManager(this);

    setupUi();

    loadSettings();

    QString driverError;
    if (DriverInstaller::install(&driverError)) {
        logWindow->appendLog("INFO", "Kernel driver installation succeeded.");
    } else {
        logWindow->appendLog("ERROR", QString("Kernel driver installation failed: %1").arg(driverError));
    }

    loadUsbIdDatabase();

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

    logWindow->appendLog("INFO", "USBIP Client initialized successfully.");

    if (autoConnectCheckBox->isChecked()) {
        logWindow->appendLog("INFO", "Auto-connect enabled. Initiating startup connection...");
        handleConnect();
    }
}

MainWindow::~MainWindow() {
    delete usbIdsDb;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();

    if (minimizeToTrayCheckBox->isChecked() && !isExiting && QSystemTrayIcon::isSystemTrayAvailable()) {
        event->ignore();
        hide();
        trayIcon->showMessage("USBIP Client", "Application minimized to system tray.", QSystemTrayIcon::Information, 2000);
    } else {
        event->accept();
    }
}

void MainWindow::loadSettings() {
    QSettings settings("USBIPClient", "USBIPClient");
    
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
    QSettings settings("USBIPClient", "USBIPClient");
    
    QStringList profiles;
    for (int i = 0; i < profileCombo->count(); ++i) {
        profiles.append(profileCombo->itemText(i));
    }
    settings.setValue("profiles/list", profiles);
    settings.setValue("profiles/active", currentProfile);
    
    saveProfileSettings(currentProfile);
}

void MainWindow::loadProfileSettings(const QString &profileName) {
    QSettings settings("USBIPClient", "USBIPClient");
    settings.beginGroup("profiles/" + profileName);
    
    QString savedIp = settings.value("hostIp", "192.168.1.11").toString();
    QString savedPort = settings.value("port", "3240").toString();
    QString savedTheme = settings.value("theme", "Dark").toString();
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

    autoConnectCheckBox->setChecked(savedAutoConnect);
    minimizeToTrayCheckBox->setChecked(savedMinimizeToTray);

    logWindow->appendLog("INFO", QString("Loaded profile '%1' settings. Last host IP: %2:%3").arg(profileName, savedIp, savedPort));
}

void MainWindow::saveProfileSettings(const QString &profileName) {
    QSettings settings("USBIPClient", "USBIPClient");
    settings.beginGroup("profiles/" + profileName);
    
    settings.setValue("hostIp", hostIpLineEdit->text().trimmed());
    settings.setValue("port", portLineEdit->text().trimmed());
    settings.setValue("theme", themeCombo->currentText());
    settings.setValue("autoConnect", autoConnectCheckBox->isChecked());
    settings.setValue("minimizeToTray", minimizeToTrayCheckBox->isChecked());
    
    settings.endGroup();
    logWindow->appendLog("INFO", QString("Saved settings for profile '%1'.").arg(profileName));
}

bool MainWindow::validatePort(quint16 port) {
    if (port < 3240 || port > 3260) {
        logWindow->appendLog("ERROR", QString("Port %1 out of bounds. Allowed range: 3240-3260.").arg(port));
        QMessageBox::critical(this, "Port Error", "Invalid port specified! USBIP Client requires a port between 3240 and 3260.");
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
    connectButton->setToolTip("Establish a logical connection to the remote USB/IP host.");
    scanHostButton = new QPushButton("Scan Host", this);
    scanHostButton->setToolTip("Query the host for available and exportable USB devices.");
    loggerButton = new QPushButton("Debug Console", this);
    loggerButton->setToolTip("Toggle the debug console to view application logs and errors.");
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
    tabWidget->addTab(createSettingsTab(), "Settings");

    mainLayout->addLayout(topBarLayout);
    mainLayout->addWidget(tabWidget);

    highBandwidthWarningLabel = new QLabel("Note: A powered USB Hub and stable 5GHz WiFi or Ethernet connection are required for stable use of high-bandwidth devices (e.g., 3D scanners, cameras).", this);
    highBandwidthWarningLabel->setObjectName("highBandwidthWarningLabel");
    highBandwidthWarningLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(highBandwidthWarningLabel);

    deviceDisconnectWarningLabel = new QLabel("Note: Disconnect attached devices from client before host to prevent errors.", this);
    deviceDisconnectWarningLabel->setObjectName("deviceDisconnectWarningLabel");
    deviceDisconnectWarningLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(deviceDisconnectWarningLabel);

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

    usbDeviceTable = new QTableWidget(0, 6, this);
    usbDeviceTable->setHorizontalHeaderLabels({"Fav", "Device Name", "VID:PID", "Status", "Attach Action", "Reset Action"});
    usbDeviceTable->horizontalHeaderItem(0)->setToolTip("Attach device automatically when connected to host.");
    usbDeviceTable->horizontalHeaderItem(4)->setToolTip("Mount or unmount this USB device to the Windows kernel.");
    usbDeviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    usbDeviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    usbLayout->addWidget(usbDeviceTable);
    layout->addWidget(usbGroupBox);

    return tab;
}

void MainWindow::addUsbDeviceToTable(const QString &name, const QString &busId, const QString &vidPid, const QString &status, bool attached) {
    int row = usbDeviceTable->rowCount();
    usbDeviceTable->insertRow(row);

    QWidget *favWidget = new QWidget(this);
    QHBoxLayout *favLayout = new QHBoxLayout(favWidget);
    favLayout->setAlignment(Qt::AlignCenter);
    favLayout->setContentsMargins(0, 0, 0, 0);
    QCheckBox *favCheck = new QCheckBox(favWidget);
    favCheck->setChecked(getFavorites().contains(vidPid));
    connect(favCheck, &QCheckBox::toggled, [this, vidPid](bool checked) { setFavorite(vidPid, checked); });
    favLayout->addWidget(favCheck);
    favWidget->setToolTip("Attach device automatically when connected to host.");
    usbDeviceTable->setCellWidget(row, 0, favWidget);

    QTableWidgetItem *nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, busId);
    usbDeviceTable->setItem(row, 1, nameItem);

    usbDeviceTable->setItem(row, 2, new QTableWidgetItem(vidPid));
    usbDeviceTable->setItem(row, 3, new QTableWidgetItem(status));

    QPushButton *attachBtn = new QPushButton(attached ? "Detach" : "Attach", this);
    attachBtn->setToolTip("Mount or unmount this USB device to the Windows kernel.");
    QPushButton *resetBtn = new QPushButton("Reset Connection", this);
    resetBtn->setToolTip("Force reset the connection state for this device.");

    connect(attachBtn, &QPushButton::clicked, [this, row]() { handleToggleDeviceAttach(row); });
    connect(resetBtn, &QPushButton::clicked, [this, row]() { handleResetDeviceConnection(row); });

    usbDeviceTable->setCellWidget(row, 4, attachBtn);
    usbDeviceTable->setCellWidget(row, 5, resetBtn);
}

QWidget* MainWindow::createAudioTab() {
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QGroupBox *audioGroup = new QGroupBox("Opus Audio Relay Engine", tab);
    QFormLayout *formLayout = new QFormLayout(audioGroup);

    enableAudioRelayButton = new QPushButton("Enable Audio Relay Stream", this);
    enableAudioRelayButton->setCheckable(true);

    audioInputDeviceCombo = new QComboBox(this);
    audioInputDeviceCombo->addItems(audioRelayManager->getAvailableInputDevices());

    audioOutputDeviceCombo = new QComboBox(this);
    audioOutputDeviceCombo->addItems(audioRelayManager->getAvailableOutputDevices());

    audioQualityCombo = new QComboBox(this);
    audioQualityCombo->addItems({"Low Latency (64 kbps)", "Balanced (128 kbps)", "High Fidelity (256 kbps)"});
    audioQualityCombo->setCurrentIndex(1);

#ifndef HAVE_QT_MULTIMEDIA
    audioInputDeviceCombo->setEnabled(false);
    audioOutputDeviceCombo->setEnabled(false);
    audioQualityCombo->setEnabled(false);
    enableAudioRelayButton->setEnabled(false);
    enableAudioRelayButton->setToolTip("Qt6 Multimedia module is missing. Audio relay is disabled.");
#endif

    resetAudioButton = new QPushButton("Reset Audio Subsystem", this);

    formLayout->addRow("Relay State:", enableAudioRelayButton);
    formLayout->addRow("Input Device:", audioInputDeviceCombo);
    formLayout->addRow("Output Device:", audioOutputDeviceCombo);
    formLayout->addRow("Preset Quality:", audioQualityCombo);
    formLayout->addRow("Subsystem Recovery:", resetAudioButton);

    layout->addWidget(audioGroup);
    layout->addStretch();

    connect(resetAudioButton, &QPushButton::clicked, this, &MainWindow::handleResetAudioSubsystem);

    connect(enableAudioRelayButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            QString ipStr = hostIpLineEdit->text().trimmed();
            QHostAddress targetIp(ipStr);
            if (targetIp.isNull()) {
                logWindow->appendLog("ERROR", "Invalid Host IP address for Audio Relay.");
                enableAudioRelayButton->setChecked(false);
                return;
            }

            const int bitrateTable[] = {64000, 128000, 256000};
            int bitrate = bitrateTable[std::clamp(audioQualityCombo->currentIndex(), 0, 2)];

#ifdef HAVE_QT_MULTIMEDIA
            QAudioDevice inputDevice = audioRelayManager->findInputDevice(audioInputDeviceCombo->currentText());
            QAudioDevice outputDevice = audioRelayManager->findOutputDevice(audioOutputDeviceCombo->currentText());

            logWindow->appendLog("INFO", QString("Starting Opus Audio Relay: Streaming to %1:48100, Receiving on port 48100...").arg(ipStr));

            audioRelayManager->startStreaming(inputDevice, targetIp, 48100, 48000, 2, bitrate);
            audioRelayManager->startReceiving(outputDevice, 48100);
#endif
        } else {
            logWindow->appendLog("INFO", "Stopping Audio Relay.");
            audioRelayManager->stopAll();
        }
    });

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

    isLogicallyConnected = true;
    connectButton->setText("Disconnect");
    connectionStatusLabel->setText("<font color='green'>Status: Connected (Logical)</font>");
    connectionStatusLabel->setStyleSheet("font-weight: bold;");
    logWindow->appendLog("INFO", QString("Connected to %1:%2 in UI-only mode.").arg(ip).arg(port));
    saveSettings();
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

    logWindow->appendLog("INFO", "Scanning exportable devices via usbip_sdk...");

    usbip::Socket sock = usbip::connect(ip.toStdString().c_str(), QString::number(port).toStdString().c_str());
    if (!sock) {
        logWindow->appendLog("ERROR", QString("Scan connect to %1:%2 failed (error %3).").arg(ip).arg(port).arg(GetLastError()));
        return;
    }

    QList<usbip::usb_device> devices;
    bool enumOk = false;
    try {
        enumOk = usbip::enum_exportable_devices(
            sock.get(),
            [&devices](int, const usbip::usb_device &dev) { devices.append(dev); },
            [](int, const usbip::usb_device &, int, const usbip::usb_interface &) {},
            nullptr);
    } catch (const std::exception &ex) {
        logWindow->appendLog("ERROR", QString("enum_exportable_devices threw an exception: %1").arg(ex.what()));
        return;
    }

    if (!enumOk) {
        logWindow->appendLog("ERROR", QString("enum_exportable_devices failed (error %1).").arg(GetLastError()));
        return;
    }

    usbDeviceTable->setRowCount(0);

    if (devices.isEmpty()) {
        logWindow->appendLog("INFO", "Scan complete: no exportable devices found.");
        return;
    }

    for (const usbip::usb_device &dev : devices) {
        QString busid  = QString::fromStdString(dev.busid);
        QString vidPid = QString("%1:%2")
                            .arg(dev.idVendor,  4, 16, QChar('0'))
                            .arg(dev.idProduct, 4, 16, QChar('0'))
                            .toUpper();
        QString name   = getFriendlyDeviceName(dev.idVendor, dev.idProduct);
        int hubPort    = findAttachedPort(busid);
        bool attached  = hubPort >= 1;
        QString status = attached ? "Attached" : "Available";

        addUsbDeviceToTable(name, busid, vidPid, status, attached);
        logWindow->appendLog("INFO", QString("Found: %1  [%2]  %3").arg(busid, vidPid, name));
    }

    logWindow->appendLog("INFO", QString("Scan complete: %1 device(s) found.").arg(devices.size()));

    for (int r = 0; r < usbDeviceTable->rowCount(); ++r) {
        QTableWidgetItem *vidPidItem = usbDeviceTable->item(r, 2);
        if (!vidPidItem) continue;
        QString vidPid = vidPidItem->text();
        if (!getFavorites().contains(vidPid)) continue;
        QPushButton *attachBtn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(r, 4));
        if (attachBtn && attachBtn->text() == "Attach") {
            logWindow->appendLog("INFO", QString("Auto-attaching favorite device: %1").arg(vidPid));
            handleToggleDeviceAttach(r);
        }
    }
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

    QString busid = usbDeviceTable->item(row, 1)->data(Qt::UserRole).toString();
    QPushButton *btn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 4));
    if (!btn) return;

    btn->setEnabled(false);

    try {
        if (btn->text() == "Detach") {
            int hubPort = findAttachedPort(busid);
            if (hubPort < 1) {
                logWindow->appendLog("WARNING", QString("Device %1 is not recorded as attached; skipping detach.").arg(busid));
                btn->setEnabled(true);
                return;
            }

            usbip::Handle dev = usbip::vhci::open();
            if (!dev) {
                logWindow->appendLog("ERROR", QString("vhci::open() failed (error %1).").arg(GetLastError()));
                btn->setEnabled(true);
                return;
            }

            if (!usbip::vhci::detach(dev.get(), hubPort)) {
                DWORD err = GetLastError();
                if (err == ERROR_DEVICE_NOT_CONNECTED) {
                    logWindow->appendLog("WARNING", QString("Device on bus %1 was already disconnected on the host/OS level (error 1167). Force-syncing local state.").arg(busid));
                    attachedPorts.remove(busid);
                    if (row < usbDeviceTable->rowCount()) {
                        usbDeviceTable->item(row, 3)->setText("Available");
                        QPushButton *attachBtn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 4));
                        if (attachBtn) attachBtn->setText("Attach");
                    }
                } else {
                    logWindow->appendLog("ERROR", QString("vhci::detach() failed for bus %1 (error %2).").arg(busid).arg(err));
                }
                btn->setEnabled(true);
                return;
            }

            attachedPorts.remove(busid);
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 3)->setText("Available");
                btn->setText("Attach");
            }
            logWindow->appendLog("INFO", QString("Detached bus %1 (port %2).").arg(busid).arg(hubPort));
            btn->setEnabled(true);
            return;
        }

        // Attach path
        QString ip   = hostIpLineEdit->text().trimmed();
        QString port = portLineEdit->text().trimmed();

        usbip::Handle dev = usbip::vhci::open();
        if (!dev) {
            logWindow->appendLog("ERROR", QString("vhci::open() failed (error %1).").arg(GetLastError()));
            btn->setEnabled(true);
            return;
        }

        usbip::device_location location;
        location.hostname = ip.toStdString();
        location.service  = port.toStdString();
        location.busid    = busid.toStdString();

        logWindow->appendLog("INFO", QString("Attaching bus %1 from %2:%3...").arg(busid, ip, port));

        int hubPort = usbip::vhci::attach(dev.get(), location);
        if (hubPort < 1) {
            logWindow->appendLog("ERROR", QString("vhci::attach() failed for bus %1 (error %2).").arg(busid).arg(GetLastError()));
            btn->setEnabled(true);
            return;
        }

        attachedPorts[busid] = hubPort;
        if (row < usbDeviceTable->rowCount()) {
            usbDeviceTable->item(row, 3)->setText("Attached");
            btn->setText("Detach");
        }
        logWindow->appendLog("INFO", QString("Attached bus %1 on hub port %2.").arg(busid).arg(hubPort));
    } catch (const std::exception &ex) {
        logWindow->appendLog("ERROR", QString("SDK exception in attach/detach: %1").arg(ex.what()));
    }

    btn->setEnabled(true);
}

void MainWindow::handleResetDeviceConnection(int row) {
    if (row < 0 || row >= usbDeviceTable->rowCount()) return;

    QString busid = usbDeviceTable->item(row, 1)->data(Qt::UserRole).toString();

    int hubPort = findAttachedPort(busid);
    if (hubPort < 1) {
        logWindow->appendLog("WARNING", QString("Device on bus %1 is not attached; cannot reset.").arg(busid));
        return;
    }

    usbip::Handle dev = usbip::vhci::open();
    if (!dev) {
        logWindow->appendLog("ERROR", QString("vhci::open() failed (error %1).").arg(GetLastError()));
        return;
    }

    if (!usbip::vhci::detach(dev.get(), hubPort)) {
        DWORD err = GetLastError();
        if (err == ERROR_DEVICE_NOT_CONNECTED) {
            logWindow->appendLog("WARNING", QString("Device on bus %1 was already disconnected on the host/OS level (error 1167). Force-syncing local state.").arg(busid));
            attachedPorts.remove(busid);
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 3)->setText("Available");
                QPushButton *attachBtn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 4));
                if (attachBtn) attachBtn->setText("Attach");
            }
            // stale port cleared; fall through to re-attach
        } else {
            logWindow->appendLog("ERROR", QString("vhci::detach() failed for bus %1 (error %2).").arg(busid).arg(err));
            return;
        }
    } else {
        attachedPorts.remove(busid);
    }
    logWindow->appendLog("INFO", QString("Resetting device on bus %1 (detaching and re-attaching)...").arg(busid));

    QString ip   = hostIpLineEdit->text().trimmed();
    QString port = portLineEdit->text().trimmed();

    usbip::device_location location;
    location.hostname = ip.toStdString();
    location.service  = port.toStdString();
    location.busid    = busid.toStdString();

    int newHubPort = usbip::vhci::attach(dev.get(), location);
    if (newHubPort < 1) {
        logWindow->appendLog("ERROR", QString("Re-attach failed for bus %1 (error %2).").arg(busid).arg(GetLastError()));
        if (row < usbDeviceTable->rowCount())
            usbDeviceTable->item(row, 3)->setText("Available");
        return;
    }

    attachedPorts[busid] = newHubPort;
    if (row < usbDeviceTable->rowCount())
        usbDeviceTable->item(row, 3)->setText("Attached");
    logWindow->appendLog("INFO", QString("Reset complete: bus %1 re-attached on hub port %2.").arg(busid).arg(newHubPort));
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
    
    QSettings settings("USBIPClient", "USBIPClient");
    settings.setValue("profiles/active", currentProfile);
    
    logWindow->appendLog("INFO", QString("Switched to profile: %1").arg(profileName));
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
            "QLabel#highBandwidthWarningLabel { color: #ffaa00; font-size: 11px; padding: 6px; background-color: #1a1d2e; border-top: 1px solid #23273a; }"
            "QLabel#deviceDisconnectWarningLabel { color: #ffaa00; font-size: 11px; padding: 2px 6px 6px 6px; background-color: #1a1d2e; }"
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
            "QLabel#highBandwidthWarningLabel { color: #b45309; font-size: 11px; padding: 6px; background-color: #ffffff; border-top: 1px solid #cbd5e1; }"
            "QLabel#deviceDisconnectWarningLabel { color: #b45309; font-size: 11px; padding: 2px 6px 6px 6px; background-color: #ffffff; }"
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
            "QLabel#highBandwidthWarningLabel { color: #ffff00; font-size: 11px; padding: 6px; background-color: #000000; border-top: 2px solid #ffffff; }"
            "QLabel#deviceDisconnectWarningLabel { color: #ffff00; font-size: 11px; padding: 2px 6px 6px 6px; background-color: #000000; }"
        );
    }
}

int MainWindow::findAttachedPort(const QString &busid) const {
    return attachedPorts.value(busid, -1);
}

QStringList MainWindow::getFavorites() const {
    QSettings settings("USBIPClient", "USBIPClient");
    return settings.value("favorites").toStringList();
}

void MainWindow::setFavorite(const QString &vidPid, bool favorite) {
    QSettings settings("USBIPClient", "USBIPClient");
    QStringList favs = settings.value("favorites").toStringList();
    if (favorite && !favs.contains(vidPid)) { favs.append(vidPid); }
    else if (!favorite) { favs.removeAll(vidPid); }
    settings.setValue("favorites", favs);
    logWindow->appendLog("INFO", QString("Device %1 %2 favorites.").arg(vidPid, favorite ? "added to" : "removed from"));
}

QString MainWindow::getFriendlyDeviceName(quint16 vendorId, quint16 productId) {
    if (usbIdsDb) {
        auto [vendorStr, productStr] = usbIdsDb->find_product(vendorId, productId);
        if (!vendorStr.empty() || !productStr.empty()) {
            QString mfg = vendorStr.empty() ? "Unknown Vendor" : QString::fromUtf8(vendorStr.data(), vendorStr.size());
            QString prod = productStr.empty() ? "Unknown Product" : QString::fromUtf8(productStr.data(), productStr.size());
            QString vidPid = QString("%1:%2").arg(vendorId, 4, 16, QChar('0')).arg(productId, 4, 16, QChar('0')).toUpper();
            return QString("%1 - %2 (%3)").arg(mfg, prod, vidPid);
        }
    }
    return "Unknown Device";
}

void MainWindow::loadUsbIdDatabase() {
    QString dbPath = QCoreApplication::applicationDirPath() + "/usb.ids";
    QFile file(dbPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logWindow->appendLog("WARNING", "Failed to open usb.ids database at " + dbPath);
        return;
    }

    usbIdsData = file.readAll();
    usbIdsDb = new usbip::UsbIds(std::string_view(usbIdsData.constData(), usbIdsData.size()));
    logWindow->appendLog("INFO", "Loaded usb.ids database via SDK.");
}

