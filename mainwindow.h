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
#include "logwindow.h"

// libusbip SDK — must come after windows.h is pulled in transitively
#define WIN32_LEAN_AND_MEAN
#include <usbip/vhci.h>
#include <usbip/remote.h>
#include <usbip/output.h>
#include <usbip/persistent.h>

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
    void handleBitrateSliderChange(int value);
    void handleBitrateSpinBoxChange(int value);
    void syncDeviceState();
    void handleNewProfile();
    void handleProfileChange(const QString &profileName);
    void handleDuoInstanceChanged(const QString &name);
    void handleDuoNewInstance();
    void handleDuoDeleteInstance();
    void handleDuoStartInstance();
    void handleDuoStopInstance();
    void handleDuoOpenSunshine();
    void handleDuoPairClient();
    void handleDuoTestCredentials();
    void handleDuoAutoStartApps();
    void handleDuoScaleTypeChanged(int index);

private:
    void setupUi();
    void applyTheme(const QString &themeName);
    QWidget* createNetworkTab();
    QWidget* createAudioTab();
    QWidget* createDisplayTab();
    QWidget* createSettingsTab();
    void addUsbDeviceToTable(const QString &name, const QString &busId, const QString &vidPid, const QString &status, bool attached);
    bool validatePort(quint16 port);
    void loadSettings();
    void saveSettings();
    void loadProfileSettings(const QString &profileName);
    void saveProfileSettings(const QString &profileName);
    void loadDuoInstances();
    void loadDuoInstanceSettings(const QString &name);
    void saveDuoInstanceSettings(const QString &name);
    void loadDuoGlobalSettings();
    void saveDuoGlobalSettings();
    void populateDuoUsers();
    void populateDuoAdapters();
    void setDuoInstanceControlsEnabled(bool enabled);
    QString encryptPassword(const QString &password);
    QString decryptPassword(const QString &base64);
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
    QComboBox *audioQualityCombo;
    QComboBox *audioSampleRateCombo;
    QSlider *audioBufferSlider;
    QLabel *audioBufferLabel;
    QPushButton *resetAudioButton;

    // Display Tab Controls
    QComboBox *resolutionCombo;
    QComboBox *framerateCombo;
    QComboBox *aspectRatioCombo;
    QSlider *bitrateSlider;
    QSpinBox *bitrateSpinBox;
    QComboBox *codecCombo;
    QCheckBox *hwDecodingCheckBox;
    QCheckBox *vsyncCheckBox;

    // DuoStream Controls
    QComboBox *duoInstanceCombo;
    QLineEdit *duoDisplayNameEdit;
    QLineEdit *duoPortEdit;
    QCheckBox *duoStartWithServiceCheck;
    QComboBox *duoUserCombo;
    QLineEdit *duoPasswordEdit;
    QComboBox *duoAdapterCombo;
    QComboBox *duoScaleTypeCombo;
    QSlider *duoScaleSlider;
    QLabel *duoScaleLabel;
    QSlider *duoSuperSamplingSlider;
    QLabel *duoSuperSamplingLabel;
    QSlider *duoSdrWhiteLevelSlider;
    QLabel *duoSdrWhiteLevelLabel;
    QCheckBox *duoForceSdrCheck;
    QCheckBox *duoStartWithWindowsCheck;
    QCheckBox *duoHidIsolationCheck;
    QCheckBox *duoProcessPatchingCheck;
    QCheckBox *duoIsolateStreamCheck;
    QLineEdit *duoWebPortEdit;
    QComboBox *duoVerbosityCombo;

    // Settings Tab Controls
    QComboBox *themeCombo;
    QCheckBox *minimizeToTrayCheckBox;
    QCheckBox *autoConnectCheckBox;
    QComboBox *profileCombo;

    LogWindow *logWindow;
    bool isLogicallyConnected = false;
    // Maps busid -> vhci hub port number (>= 1) for currently attached devices
    QHash<QString, int> attachedPorts;
    QHash<QString, QString> usbDeviceDb;
    QTimer *syncTimer;
    QSystemTrayIcon *trayIcon;
    bool isExiting = false;
    QString currentProfile;
};

#endif // MAINWINDOW_H
