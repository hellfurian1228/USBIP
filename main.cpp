#include <QApplication>
#include <QString>
#include "mainwindow.h"
#include <windows.h>
#include <shellapi.h>
#include <win_socket.h>

static bool isRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0,
                                  &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

int main(int argc, char *argv[]) {
    if (!isRunningAsAdmin()) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.nShow  = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
        return 0;
    }

    QApplication app(argc, argv);
    usbip::InitWinSock2 ws2; // required by usbip::connect() / enum_exportable_devices()
    MainWindow window;
    window.show();
    return app.exec();
}
