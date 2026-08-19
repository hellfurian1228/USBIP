#include "driverinstaller.h"

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

// Hardware ID of the root-enumerated virtual host controller node created by the .inf.
static const wchar_t kHardwareId[] = L"ROOT\\USBIP_WIN2\\UDE";

QString DriverInstaller::locateInf() {
    const QString path = QCoreApplication::applicationDirPath() + "/Drivers/usbip2_ude.inf";
    return QFileInfo::exists(path) ? QDir::toNativeSeparators(path) : QString();
}

bool DriverInstaller::stageDriverPackage(const QString &infPath, QString *errorMessage) {
    std::wstring infPathW = infPath.toStdWString();

    if (SetupCopyOEMInfW(infPathW.c_str(), nullptr, SPOST_PATH, 0, nullptr, 0, nullptr, nullptr))
        return true;

    const DWORD err = GetLastError();
    if (err == ERROR_FILE_EXISTS || err == ERROR_NO_MORE_ITEMS)
        return true; // an identical package is already staged

    if (errorMessage)
        *errorMessage = QString("SetupCopyOEMInfW failed (error %1).").arg(err);
    return false;
}

bool DriverInstaller::isRootEnumeratedDevicePresent(const QString &hardwareId) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, L"ROOT", nullptr, DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &data); ++i) {
        WCHAR buffer[512] = {};
        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &data, SPDRP_HARDWAREID,
                                               nullptr, reinterpret_cast<PBYTE>(buffer), sizeof(buffer), nullptr)) {
            if (QString::fromWCharArray(buffer).compare(hardwareId, Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}

bool DriverInstaller::createRootEnumeratedNode(const QString &infPath, QString *errorMessage) {
    std::wstring infPathW = infPath.toStdWString();

    GUID classGuid{};
    constexpr DWORD kClassNameLen = 32; // matches SetupAPI's MAX_CLASS_NAME_LEN
    WCHAR className[kClassNameLen] = {};
    if (!SetupDiGetINFClassW(infPathW.c_str(), &classGuid, className, kClassNameLen, nullptr)) {
        if (errorMessage) *errorMessage = QString("SetupDiGetINFClassW failed (error %1).").arg(GetLastError());
        return false;
    }

    HDEVINFO devInfo = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
    if (devInfo == INVALID_HANDLE_VALUE) {
        if (errorMessage) *errorMessage = QString("SetupDiCreateDeviceInfoList failed (error %1).").arg(GetLastError());
        return false;
    }

    SP_DEVINFO_DATA devInfoData{};
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    if (!SetupDiCreateDeviceInfoW(devInfo, className, &classGuid, nullptr, nullptr,
                                  DICD_GENERATE_ID, &devInfoData)) {
        if (errorMessage) *errorMessage = QString("SetupDiCreateDeviceInfoW failed (error %1).").arg(GetLastError());
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    WCHAR hwIdBuffer[MAX_PATH] = {};
    wcscpy_s(hwIdBuffer, kHardwareId);
    if (!SetupDiSetDeviceRegistryPropertyW(devInfo, &devInfoData, SPDRP_HARDWAREID,
                                            reinterpret_cast<const BYTE *>(hwIdBuffer),
                                            static_cast<DWORD>((wcslen(hwIdBuffer) + 2) * sizeof(WCHAR)))) {
        if (errorMessage) *errorMessage = QString("SetupDiSetDeviceRegistryPropertyW failed (error %1).").arg(GetLastError());
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devInfo, &devInfoData)) {
        if (errorMessage) *errorMessage = QString("SetupDiCallClassInstaller(DIF_REGISTERDEVICE) failed (error %1).").arg(GetLastError());
        SetupDiDestroyDeviceInfoList(devInfo);
        return false;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return true;
}

bool DriverInstaller::applyDriver(const QString &infPath, QString *errorMessage) {
    std::wstring infPathW = infPath.toStdWString();
    BOOL rebootRequired = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kHardwareId, infPathW.c_str(),
                                             INSTALLFLAG_FORCE, &rebootRequired)) {
        if (errorMessage) *errorMessage = QString("UpdateDriverForPlugAndPlayDevicesW failed (error %1).").arg(GetLastError());
        return false;
    }
    return true;
}

bool DriverInstaller::install(QString *errorMessage) {
    const QString infPath = locateInf();
    if (infPath.isEmpty()) {
        if (errorMessage) *errorMessage = "usbip2_ude.inf not found in Drivers directory.";
        return false;
    }

    if (!stageDriverPackage(infPath, errorMessage))
        return false;

    if (!isRootEnumeratedDevicePresent(QString::fromWCharArray(kHardwareId))) {
        if (!createRootEnumeratedNode(infPath, errorMessage))
            return false;
    }

    return applyDriver(infPath, errorMessage);
}
