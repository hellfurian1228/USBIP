#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QCloseEvent>
#include <QMap>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QFile>
#include <QTimer>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QSet>
#include <QDateTime>
#include "logwindow.h"

// libusbip SDK — must come after windows.h is pulled in transitively
#define WIN32_LEAN_AND_MEAN
#include <usbip/vhci.h>
#include <usbip/remote.h>
#include <usbip/output.h>
#include <usbip/persistent.h>

class AudioRelayManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void handleConnect();
    void handleScanHost();
    void handleToggleLogWindow();
    void handleResetDeviceConnection(int row);
    void handleToggleDeviceAttach(int row);
    void handleResetAudioSubsystem();
    void handleThemeChange(int index);
    void syncDeviceState();
    void handleNewProfile();
    void handleProfileChange(const QString &profileName);

private:
    void setupUi();
    void applyTheme(const QString &themeName);
    QWidget* createNetworkTab();
    QWidget* createAudioTab();
    QWidget* createSettingsTab();
    void addUsbDeviceToTable(const QString &name, const QString &busId, const QString &vidPid, const QString &status, bool attached);
    bool validatePort(quint16 port);
    void loadSettings();
    void saveSettings();
    void loadProfileSettings(const QString &profileName);
    void saveProfileSettings(const QString &profileName);
    bool checkAndConfigureDrivers();
    QString getDriverPath();
    QString getFriendlyDeviceName(quint16 vendorId, quint16 productId);
    void loadUsbIdDatabase();
    // Returns the vhci port# for a given busid, or -1 if not attached
    int findAttachedPort(const QString &busid) const;

    QTabWidget *tabWidget;
    QLineEdit *hostIpLineEdit;
    QLineEdit *portLineEdit;
    QPushButton *connectButton;
    QPushButton *scanHostButton;
    QPushButton *loggerButton;
    QTableWidget *usbDeviceTable;
    QLabel *connectionStatusLabel;

    // Audio Tab Controls
    QPushButton *enableAudioRelayButton;
    QComboBox *audioInputDeviceCombo;
    QComboBox *audioOutputDeviceCombo;
    QComboBox *audioQualityCombo;
    QComboBox *audioSampleRateCombo;
    QSlider *audioBufferSlider;
    QLabel *audioBufferLabel;
    QPushButton *resetAudioButton;

    // Settings Tab Controls
    QComboBox *themeCombo;
    QCheckBox *minimizeToTrayCheckBox;
    QCheckBox *autoConnectCheckBox;
    QComboBox *profileCombo;

    LogWindow *logWindow;
    bool isLogicallyConnected = false;
    // Maps busid -> vhci hub port number (>= 1) for currently attached devices
    struct ReconnectInfo {
        int attempts = 0;
        QDateTime lastAttempt;
    };

    QHash<QString, int> attachedPorts;
    QHash<QString, QString> usbDeviceDb;
    QSet<QString> desiredAttachedDevices;
    QHash<QString, ReconnectInfo> reconnectTracker;
    QTimer *syncTimer;
    QSystemTrayIcon *trayIcon;
    bool isExiting = false;
    QString currentProfile;
    AudioRelayManager *audioRelayManager;
};

#endif // MAINWINDOW_H
