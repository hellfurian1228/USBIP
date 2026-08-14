#include "mainwindow.h"
#include "audiorelaymanager.h"
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
    setWindowTitle("USBIP Client v1.0.0");
    resize(1000, 650);

    logWindow = new LogWindow(this);
    audioRelayManager = new AudioRelayManager(this);

    setupUi();

    loadSettings();
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

    logWindow->appendLog("INFO", "USBIP Client initialized successfully.");

    if (autoConnectCheckBox->isChecked()) {
        logWindow->appendLog("INFO", "Auto-connect enabled. Initiating startup connection...");
        handleConnect();
    }
}

MainWindow::~MainWindow() {}

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

    audioInputDeviceCombo = new QComboBox(this);
    audioInputDeviceCombo->addItems(audioRelayManager->getAvailableInputDevices());

    audioOutputDeviceCombo = new QComboBox(this);
    audioOutputDeviceCombo->addItems(audioRelayManager->getAvailableOutputDevices());

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
    formLayout->addRow("Input Device:", audioInputDeviceCombo);
    formLayout->addRow("Output Device:", audioOutputDeviceCombo);
    formLayout->addRow("Preset Quality:", audioQualityCombo);
    formLayout->addRow("Sample Rate:", audioSampleRateCombo);
    formLayout->addRow(audioBufferLabel, audioBufferSlider);
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

            int sampleRate = 48000;
            QString rateStr = audioSampleRateCombo->currentText();
            if (rateStr.contains("44100")) sampleRate = 44100;
            else if (rateStr.contains("48000")) sampleRate = 48000;
            else if (rateStr.contains("96000")) sampleRate = 96000;

            QAudioDevice inputDevice = audioRelayManager->findInputDevice(audioInputDeviceCombo->currentText());
            QAudioDevice outputDevice = audioRelayManager->findOutputDevice(audioOutputDeviceCombo->currentText());

            logWindow->appendLog("INFO", QString("Starting Audio Relay: Streaming to %1:48100, Receiving on port 48100...").arg(ipStr));
            
            audioRelayManager->startStreaming(inputDevice, targetIp, 48100, sampleRate, 2);
            audioRelayManager->startReceiving(outputDevice, 48100);
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

    btn->setEnabled(false);

    QString ip   = hostIpLineEdit->text().trimmed();
    QString port = portLineEdit->text().trimmed();

    try {
        if (btn->text() == "Detach") {
            int hubPort = findAttachedPort(busid);
            if (hubPort < 1) {
                QMessageBox::warning(this, "Detach Failed", QString("Device %1 is not recorded as attached.").arg(busid));
                logWindow->appendLog("WARNING", QString("Device %1 is not recorded as attached; skipping detach.").arg(busid));
                btn->setEnabled(true);
                return;
            }

            usbip::Handle dev = usbip::vhci::open();
            if (!dev) {
                QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver. Is the UDE driver loaded?");
                logWindow->appendLog("ERROR", "vhci::open() failed — is the UDE driver loaded?");
                btn->setEnabled(true);
                return;
            }

            logWindow->appendLog("INFO", QString("Detaching bus %1 (port %2)...").arg(busid).arg(hubPort));

            if (!usbip::vhci::detach(dev.get(), hubPort)) {
                DWORD err = GetLastError();
                QMessageBox::warning(this, "Detach Failed", QString("Failed to detach device on bus %1. Error code: %2").arg(busid).arg(err));
                logWindow->appendLog("ERROR", QString("vhci::detach() failed (error %1).").arg(err));
                btn->setEnabled(true);
                return;
            }

            attachedPorts.remove(busid);
            desiredAttachedDevices.remove(busid);
            reconnectTracker.remove(busid);

            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 2)->setText("Available");
                btn->setText("Attach");
            }
            logWindow->appendLog("INFO", QString("Detached device on bus %1.").arg(busid));
            btn->setEnabled(true);
            return;
        }

        // Attach path
        usbip::Handle dev = usbip::vhci::open();
        if (!dev) {
            QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver. Is the UDE driver loaded?");
            logWindow->appendLog("ERROR", "vhci::open() failed — is the UDE driver loaded?");
            btn->setEnabled(true);
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
            btn->setEnabled(true);
            return;
        }

        attachedPorts[busid] = hubPort;
        desiredAttachedDevices.insert(busid);
        reconnectTracker.remove(busid);

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

    btn->setEnabled(true);
}

void MainWindow::handleResetDeviceConnection(int row) {
    if (row < 0 || row >= usbDeviceTable->rowCount()) return;

    QString busid = usbDeviceTable->item(row, 0)->data(Qt::UserRole).toString();
    QString ip    = hostIpLineEdit->text().trimmed();
    QString port  = portLineEdit->text().trimmed();

    QPushButton *attachBtn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 3));
    QPushButton *resetBtn = qobject_cast<QPushButton*>(usbDeviceTable->cellWidget(row, 4));
    if (attachBtn) attachBtn->setEnabled(false);
    if (resetBtn) resetBtn->setEnabled(false);

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
                if (attachBtn) attachBtn->setEnabled(true);
                if (resetBtn) resetBtn->setEnabled(true);
                return;
            }
            if (!usbip::vhci::detach(dev.get(), hubPort)) {
                DWORD err = GetLastError();
                QMessageBox::warning(this, "Reset Failed", QString("Failed to detach device during reset. Error code: %1").arg(err));
                logWindow->appendLog("ERROR", QString("vhci::detach() failed during reset (error %1).").arg(err));
                if (attachBtn) attachBtn->setEnabled(true);
                if (resetBtn) resetBtn->setEnabled(true);
                return;
            }
            attachedPorts.remove(busid);
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 2)->setText("Available");
                if (attachBtn) attachBtn->setText("Attach");
            }
            logWindow->appendLog("INFO", QString("Detached bus %1 for reset.").arg(busid));
        }
    } catch (const std::exception &ex) {
        QMessageBox::warning(this, "Error", QString("SDK exception during reset detach: %1").arg(ex.what()));
        logWindow->appendLog("ERROR", QString("SDK exception during reset detach: %1").arg(ex.what()));
        if (attachBtn) attachBtn->setEnabled(true);
        if (resetBtn) resetBtn->setEnabled(true);
        return;
    }

    // Re-attach after 2 seconds
    QTimer::singleShot(2000, this, [this, ip, port, busid, row, attachBtn, resetBtn]() {
        logWindow->appendLog("INFO", QString("Re-attaching bus %1 to %2...").arg(busid, ip));
        try {
            usbip::Handle dev = usbip::vhci::open();
            if (!dev) {
                QMessageBox::critical(this, "Driver Error", "Failed to open VHCI driver during reset re-attach.");
                logWindow->appendLog("ERROR", "vhci::open() failed during reset re-attach.");
                if (attachBtn) attachBtn->setEnabled(true);
                if (resetBtn) resetBtn->setEnabled(true);
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
                if (attachBtn) attachBtn->setEnabled(true);
                if (resetBtn) resetBtn->setEnabled(true);
                return;
            }

            attachedPorts[busid] = hubPort;
            if (row < usbDeviceTable->rowCount()) {
                usbDeviceTable->item(row, 2)->setText("Attached (Native)");
                if (attachBtn) attachBtn->setText("Detach");
            }
            logWindow->appendLog("INFO", QString("Re-attached bus %1 on hub port %2.").arg(busid).arg(hubPort));
        } catch (const std::exception &ex) {
            QMessageBox::warning(this, "Error", QString("SDK exception during reset re-attach: %1").arg(ex.what()));
            logWindow->appendLog("ERROR", QString("SDK exception during reset re-attach: %1").arg(ex.what()));
        }

        if (attachBtn) attachBtn->setEnabled(true);
        if (resetBtn) resetBtn->setEnabled(true);
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
    QSettings settings("USBIPClient", "USBIPClient");
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
        QString udeInf    = QDir::toNativeSeparators(driverDir + "/usbip2_ude.inf");
        QString filterInf = QDir::toNativeSeparators(driverDir + "/usbip2_filter.inf");

        QProcess::execute("InfDefaultInstall.exe", QStringList() << udeInf);
        QProcess::execute("InfDefaultInstall.exe", QStringList() << filterInf);
        logWindow->appendLog("INFO", "Driver installation sequence executed.");
    } else {
        logWindow->appendLog("INFO", "USB/IP kernel drivers are already registered in the system.");
    }

    // Ensure the ROOT\USBIP_WIN2\UDE virtual host controller node exists
    QString usbipPath = QDir::toNativeSeparators(driverDir + "/usbip.exe");
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
            if (btn && !btn->isEnabled()) {
                continue; // Skip updating if an operation is in progress
            }

            if (found) {
                usbDeviceTable->setItem(row, 2, new QTableWidgetItem("Attached (Native)"));
                usbDeviceTable->item(row, 2)->setForeground(QBrush(QColor("#00ffcc")));
                if (btn) btn->setText("Detach");
            } else {
                usbDeviceTable->setItem(row, 2, new QTableWidgetItem("Available"));
                if (btn) btn->setText("Attach");

                // Auto-reconnect logic
                if (desiredAttachedDevices.contains(busid)) {
                    ReconnectInfo &info = reconnectTracker[busid];
                    QDateTime now = QDateTime::currentDateTime();
                    if (info.attempts < 3 && (!info.lastAttempt.isValid() || info.lastAttempt.secsTo(now) >= 10)) {
                        info.attempts++;
                        info.lastAttempt = now;
                        logWindow->appendLog("WARNING", QString("Device on bus %1 disconnected unexpectedly. Auto-reconnect attempt %2/3...").arg(busid).arg(info.attempts));
                        
                        QTimer::singleShot(0, this, [this, row]() {
                            handleToggleDeviceAttach(row);
                        });
                    } else if (info.attempts >= 3) {
                        logWindow->appendLog("ERROR", QString("Auto-reconnect failed for device on bus %1 after 3 attempts. Giving up.").arg(busid));
                        desiredAttachedDevices.remove(busid);
                        reconnectTracker.remove(busid);
                    }
                }
            }
        }
    } catch (...) {
    }
}
