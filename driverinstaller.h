#ifndef DRIVERINSTALLER_H
#define DRIVERINSTALLER_H

#include <QString>

// Installs the usbip2_ude virtual host controller driver via SetupAPI, using
// the .inf/.cat/.sys package staged in the Drivers/ folder next to the executable.
class DriverInstaller {
public:
    static bool install(QString *errorMessage = nullptr);

private:
    static QString locateInf();
    static bool stageDriverPackage(const QString &infPath, QString *errorMessage);
    static bool isRootEnumeratedDevicePresent(const QString &hardwareId);
    static bool createRootEnumeratedNode(const QString &infPath, QString *errorMessage);
    static bool applyDriver(const QString &infPath, QString *errorMessage);
};

#endif // DRIVERINSTALLER_H
