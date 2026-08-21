#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QDockWidget>
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
#include <QMenuBar>
#include "logwindow.h"
#include <wtypes.h>
#include <usbspec.h>

namespace usbip { class UsbIds; }

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
    void handleNewProfile();
    void handleProfileChange(const QString &profileName);
    void refreshTelemetryStats();

private:
    void setupUi();
    void applyTheme(const QString &themeName);
    QWidget* createNetworkTab();
    QWidget* createAudioTab();
    QWidget* createSettingsTab();
    void addUsbDeviceToTable(const QString &name, const QString &busId, const QString &vidPid, const QString &status, bool attached, USB_DEVICE_SPEED detectedSpeed = UsbHighSpeed);
    bool validatePort(quint16 port);
    void loadSettings();
    void saveSettings();
    void loadProfileSettings(const QString &profileName);
    void saveProfileSettings(const QString &profileName);
    QString getFriendlyDeviceName(quint16 vendorId, quint16 productId);
    void loadUsbIdDatabase();
    // Returns the vhci hub port# for a given busid, or -1 if not attached
    int findAttachedPort(const QString &busid) const;
    QStringList getFavorites() const;
    void setFavorite(const QString &vidPid, bool favorite);
    void clearDeviceTable();
    QString getFreshBusId(const QString &targetVidPid);
    void syncDeviceStates();

    QTabWidget *tabWidget;
    QLineEdit *hostIpLineEdit;
    QLineEdit *portLineEdit;
    QPushButton *connectButton;
    QPushButton *scanHostButton;
    QPushButton *loggerButton;
    QTableWidget *usbDeviceTable;
    QLabel *connectionStatusLabel;
    QLabel *highBandwidthWarningLabel;
    QLabel *deviceDisconnectWarningLabel;

    // Audio Tab Controls
    QPushButton *enableAudioRelayButton;
    QComboBox *audioInputDeviceCombo;
    QComboBox *audioOutputDeviceCombo;
    QComboBox *audioQualityCombo;
    QPushButton *resetAudioButton;

    // Settings Tab Controls
    QComboBox *themeCombo;
    QCheckBox *minimizeToTrayCheckBox;
    QCheckBox *autoConnectCheckBox;
    QComboBox *profileCombo;

    QDockWidget *telemetryDock;
    QTableWidget *telemetryTable;
    QTimer *telemetryUpdateTimer;

    LogWindow *logWindow;
    bool isLogicallyConnected = false;
    
    QByteArray usbIdsData;
    usbip::UsbIds *usbIdsDb = nullptr;

    // Maps busid -> vhci hub port number (>= 1) for currently attached devices
    QHash<QString, int> attachedPorts;
    QSystemTrayIcon *trayIcon;
    bool isExiting = false;
    QString currentProfile;
    AudioRelayManager *audioRelayManager;
    uint64_t lastTotalBytes = 0;
};

#endif // MAINWINDOW_H
