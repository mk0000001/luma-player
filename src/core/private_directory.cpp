#include "private_directory.h"

#include <QDir>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <QByteArray>
#include <QScopeGuard>
#include <QUuid>
#include <windows.h>
#include <sddl.h>
#endif

QString luma::createPrivateDirectory(const QString &parent, const QString &prefix)
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    const auto closeToken = qScopeGuard([&] { CloseHandle(token); });
    DWORD tokenBytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
    if (!tokenBytes || tokenBytes > 4096)
        return {};
    QByteArray tokenUser(tokenBytes, '\0');
    if (!GetTokenInformation(token, TokenUser, tokenUser.data(), tokenBytes, &tokenBytes))
        return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER *>(tokenUser.constData())->User.Sid, &sid))
        return {};
    const auto freeSid = qScopeGuard([&] { LocalFree(sid); });
    const auto sddl = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%1)").arg(QString::fromWCharArray(sid));
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(sddl.utf16()),
                                                               SDDL_REVISION_1, &descriptor, nullptr))
        return {};
    const auto freeDescriptor = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const auto path = QDir(parent).filePath(prefix + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto native = QDir::toNativeSeparators(path);
    return CreateDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()), &attributes) ? path : QString{};
#else
    QTemporaryDir directory(QDir(parent).filePath(prefix + "XXXXXX"));
    if (!directory.isValid())
        return {};
    directory.setAutoRemove(false);
    return directory.path();
#endif
}
