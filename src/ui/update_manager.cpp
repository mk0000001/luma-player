#include "update_manager.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <mutex>
#include <utility>
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <winhttp.h>

namespace luma {
namespace {

const auto repository = QStringLiteral("mk0000001/luma-player");
const auto platform = QStringLiteral("windows-x64");
const auto manifestName = QStringLiteral("luma-update.json");
const auto signatureName = QStringLiteral("luma-update.sig");
const auto notificationSetting = QStringLiteral("update/notifyOnStartup");
const QUrl releaseListUrl(QStringLiteral("https://api.github.com/repos/mk0000001/luma-player/releases?per_page=30"));
const QUrl publicReleasesUrl(QStringLiteral("https://github.com/mk0000001/luma-player/releases/latest"));
constexpr char trustedCoordinates[] =
    "030d278213ed871b4be26a45ebf038a93ce8af23bc55a7d0933855b4c7e1e73e"
    "b5bea934a2ad48dfd6a4fdae5254849c10299ea1a15a4e35fa01d3a7a003a96f";
constexpr qint64 releaseListLimit = 2 * 1024 * 1024;
constexpr qint64 manifestLimit = 16 * 1024;
constexpr qint64 installerLimit = 2147483648LL;
constexpr qint64 signatureSize = 64;
constexpr qint64 networkChunk = 64 * 1024;
constexpr int maxRedirects = 3;

struct TransferContext {
    std::mutex mutex;
    QObject *receiver = nullptr;
    std::function<void(DWORD, const QByteArray &)> deliver;
    QByteArray readBuffer = QByteArray(networkChunk, '\0');
};

void CALLBACK transferCallback(HINTERNET, DWORD_PTR cookie, DWORD status, void *information, DWORD length)
{
    auto *context = reinterpret_cast<TransferContext *>(cookie);
    if (!context)
        return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        // WinHTTP owns this context until the final handle notification.
        delete context;
        return;
    }
    if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE
        && status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        return;

    std::lock_guard lock(context->mutex);
    if (!context->receiver)
        return;
    QByteArray bytes;
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE && length) {
        if (!information || length > networkChunk)
            status = WINHTTP_CALLBACK_STATUS_REQUEST_ERROR;
        else
            bytes = QByteArray(static_cast<const char *>(information), qsizetype(length));
    }
    QMetaObject::invokeMethod(context->receiver, [deliver = context->deliver, status, bytes = std::move(bytes)] {
        deliver(status, bytes);
    }, Qt::QueuedConnection);
}

bool responseHeader(HINTERNET request, DWORD field, QString &value)
{
    wchar_t data[4096]{};
    DWORD length = sizeof(data);
    if (WinHttpQueryHeaders(request, field, WINHTTP_HEADER_NAME_BY_INDEX, data, &length, WINHTTP_NO_HEADER_INDEX)) {
        value = QString::fromWCharArray(data, qsizetype(length / sizeof(wchar_t))).trimmed();
        return true;
    }
    value.clear();
    return GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND;
}

struct Version {
    QStringList core;
    QStringList prerelease;
};

bool numeric(QStringView identifier)
{
    return std::all_of(identifier.begin(), identifier.end(), [](QChar character) {
        return character >= u'0' && character <= u'9';
    });
}

std::optional<Version> parseVersion(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(
        R"(\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?\z)"));
    if (text.size() > 128)
        return {};
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return {};
    Version version{{match.captured(1), match.captured(2), match.captured(3)}, {}};
    if (!match.captured(4).isEmpty())
        version.prerelease = match.captured(4).split(u'.');
    for (const QString &identifier : version.prerelease) {
        if (identifier.size() > 1 && identifier.startsWith(u'0') && numeric(identifier))
            return {};
    }
    return version;
}

int compareNumber(QStringView first, QStringView second)
{
    if (first.size() != second.size())
        return first.size() < second.size() ? -1 : 1;
    return first.compare(second);
}

int compareVersions(const Version &first, const Version &second)
{
    for (int index = 0; index < 3; ++index) {
        const auto order = compareNumber(first.core.at(index), second.core.at(index));
        if (order)
            return order;
    }
    if (first.prerelease.isEmpty() != second.prerelease.isEmpty())
        return first.prerelease.isEmpty() ? 1 : -1;
    const auto count = std::min(first.prerelease.size(), second.prerelease.size());
    for (qsizetype index = 0; index < count; ++index) {
        const auto &left = first.prerelease.at(index);
        const auto &right = second.prerelease.at(index);
        const bool leftNumeric = numeric(left);
        const bool rightNumeric = numeric(right);
        if (leftNumeric != rightNumeric)
            return leftNumeric ? -1 : 1;
        const auto order = leftNumeric ? compareNumber(left, right) : left.compare(right);
        if (order)
            return order;
    }
    if (first.prerelease.size() == second.prerelease.size())
        return 0;
    return first.prerelease.size() < second.prerelease.size() ? -1 : 1;
}

bool eligibleVersion(const Version &candidate, const Version &installed)
{
    return (!installed.prerelease.isEmpty() || candidate.prerelease.isEmpty())
           && compareVersions(candidate, installed) > 0;
}

bool isInstallerName(const QString &version, const QString &name)
{
    static const QRegularExpression suffix(QStringLiteral(R"(\A[1-9][0-9]{0,5}-windows-x64-cuda\.exe\z)"));
    const auto prefix = QStringLiteral("LumaPlayer-") + version + QStringLiteral("-setup.");
    return name.startsWith(prefix) && suffix.match(name.mid(prefix.size())).hasMatch();
}

qint64 positiveInteger(const QJsonValue &value, qint64 limit)
{
    if (!value.isDouble())
        return -1;
    const auto integer = value.toInteger(-1);
    return integer > 0 && integer <= limit && value.toDouble() == double(integer) ? integer : -1;
}

QUrl assetUrl(const QString &tag, const QString &name)
{
    return QUrl(QStringLiteral("https://github.com/") + repository + QStringLiteral("/releases/download/")
                + tag + u'/' + name, QUrl::StrictMode);
}

bool verifySignature(const QByteArray &data, const QByteArray &signature, const QByteArray &coordinates)
{
    if (coordinates.size() != 64 || signature.size() != signatureSize)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0)
        return false;
    const auto closeAlgorithm = qScopeGuard([&] { BCryptCloseAlgorithmProvider(algorithm, 0); });

    const BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
    QByteArray publicKey(reinterpret_cast<const char *>(&header), sizeof(header));
    publicKey.append(coordinates);
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                            reinterpret_cast<PUCHAR>(publicKey.data()), ULONG(publicKey.size()), 0) != 0)
        return false;
    const auto destroyKey = qScopeGuard([&] { BCryptDestroyKey(key); });

    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return BCryptVerifySignature(key, nullptr, reinterpret_cast<PUCHAR>(hash.data()), ULONG(hash.size()),
                                 reinterpret_cast<PUCHAR>(const_cast<char *>(signature.constData())),
                                 ULONG(signature.size()), 0) == 0;
}

bool notificationsEnabled()
{
    return QSettings().value(notificationSetting, true).toBool();
}

QString createPrivateStaging(const QString &folder)
{
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
    const auto path = QDir(folder).filePath(QStringLiteral("LumaPlayer-update-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto native = QDir::toNativeSeparators(path);
    return CreateDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()), &attributes) ? path : QString{};
}

HANDLE lockPath(const QString &path, bool directory)
{
    const auto native = QDir::toNativeSeparators(path);
    const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT
                        | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN);
    const DWORD share = directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ;
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                      directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
                                      share, nullptr, OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return handle;
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info))
        && !(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && bool(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory)
        return handle;
    CloseHandle(handle);
    return INVALID_HANDLE_VALUE;
}

QString finalFilePath(HANDLE handle)
{
    QString path(32768, QChar());
    const auto length = GetFinalPathNameByHandleW(handle, reinterpret_cast<LPWSTR>(path.data()),
                                                 DWORD(path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= DWORD(path.size()))
        return {};
    path.truncate(qsizetype(length));
    return path;
}

QString installedPlayerPath()
{
    QSettings installer(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\{C6A16658-7E17-4F16-B49F-AC61297DD921}_is1)"),
                        QSettings::Registry64Format);
    const auto folder = QDir::fromNativeSeparators(installer.value(QStringLiteral("InstallLocation")).toString());
    if (folder.size() < 3 || folder[1] != u':' || folder[2] != u'/')
        return {};
    return QDir(folder).filePath(QStringLiteral("LumaPlayer.exe"));
}

QString playerVersion(HANDLE file)
{
    const auto path = finalFilePath(file);
    if (path.isEmpty())
        return {};
    const auto native = reinterpret_cast<LPCWSTR>(path.utf16());
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(native, &unused);
    if (!size || size > 256 * 1024)
        return {};
    QByteArray data(size, '\0');
    if (!GetFileVersionInfoW(native, 0, size, data.data()))
        return {};
    LPVOID value = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.constData(), L"\\StringFileInfo\\040904B0\\ProductVersion", &value, &length)
        || !value || length <= 1 || length > 129)
        return {};
    return QString::fromWCharArray(static_cast<LPCWSTR>(value), length - 1);
}

QString existingVersion()
{
    const auto path = installedPlayerPath();
    if (path.isEmpty())
        return {};
    const auto file = lockPath(path, false);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    const auto closeFile = qScopeGuard([&] { CloseHandle(file); });
    return playerVersion(file);
}

QByteArray updateTargetHash(const QString &newVersion)
{
    auto path = installedPlayerPath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        path = QCoreApplication::applicationFilePath();
    const auto file = lockPath(path, false);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    const auto closeFile = qScopeGuard([&] { CloseHandle(file); });
    const auto existing = parseVersion(playerVersion(file));
    const auto updateVersion = parseVersion(newVersion);
    LARGE_INTEGER size{};
    if (!existing || !updateVersion || !eligibleVersion(*updateVersion, *existing)
        || !GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024 * 1024)
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(256 * 1024, '\0');
    qint64 bytes = 0;
    for (DWORD count = 0;;) {
        if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &count, nullptr))
            return {};
        if (!count)
            break;
        hash.addData(QByteArrayView(buffer.constData(), count));
        bytes += count;
    }
    return bytes == size.QuadPart ? hash.result().toHex() : QByteArray{};
}

bool startInstaller(HANDLE lockedFile, const QString &directory, const QByteArray &targetHash)
{
    const auto executable = finalFilePath(lockedFile);
    if (executable.isEmpty())
        return false;
    auto arguments = u'"' + executable + QStringLiteral("\" /LumaCurrentHash=") + QString::fromLatin1(targetHash);
    const auto workingDirectory = QDir::toNativeSeparators(directory);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(executable.utf16()), reinterpret_cast<LPWSTR>(arguments.data()), nullptr, nullptr,
                        FALSE, 0, nullptr, reinterpret_cast<LPCWSTR>(workingDirectory.utf16()),
                        &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace

namespace update {

static std::optional<Release> newestReleaseFromArray(const QJsonArray &releases, const QString &installedVersion)
{
    const auto installed = parseVersion(installedVersion);
    if (!installed || releases.size() > 30)
        return {};

    std::optional<Release> best;
    std::optional<Version> bestVersion;
    for (const auto &value : releases) {
        const auto item = value.toObject();
        if (item.value("draft") != QJsonValue(false)
            || !item.value("prerelease").isBool()
            || !QDateTime::fromString(item.value("published_at").toString(), Qt::ISODate).isValid())
            continue;
        const auto tag = item.value("tag_name").toString();
        if (!tag.startsWith(u'v'))
            continue;
        const auto versionText = tag.mid(1);
        const auto version = parseVersion(versionText);
        if (!version || item.value("prerelease").toBool() != !version->prerelease.isEmpty()
            || !eligibleVersion(*version, *installed)
            || (bestVersion && compareVersions(*version, *bestVersion) <= 0))
            continue;

        Release candidate{tag, versionText, 0, {}};
        bool hasSignature = false;
        bool duplicate = false;
        QSet<QString> namedAssets;
        const auto assets = item.value("assets").toArray();
        for (const auto &assetValue : assets) {
            const auto asset = assetValue.toObject();
            if (asset.value("state").toString() != QStringLiteral("uploaded"))
                continue;
            const auto name = asset.value("name").toString();
            const bool installer = isInstallerName(versionText, name);
            if (name != manifestName && name != signatureName && !installer)
                continue;
            if (namedAssets.contains(name)) {
                duplicate = true;
                break;
            }
            namedAssets.insert(name);
            const auto size = positiveInteger(asset.value("size"), installerLimit);
            if (name == manifestName && size > 0 && size <= manifestLimit) {
                candidate.manifestBytes = size;
            } else if (name == signatureName && size == signatureSize) {
                hasSignature = true;
            } else if (size > 0 && installer) {
                candidate.installers.insert(name, size);
            }
        }
        if (duplicate || !candidate.manifestBytes || !hasSignature || candidate.installers.isEmpty())
            continue;
        best = std::move(candidate);
        bestVersion = version;
    }
    return best;
}

std::optional<Release> newestRelease(const QByteArray &json, const QString &installedVersion)
{
    if (json.size() > releaseListLimit)
        return {};
    const auto document = QJsonDocument::fromJson(json);
    return document.isArray() ? newestReleaseFromArray(document.array(), installedVersion) : std::nullopt;
}

std::optional<Manifest> verifiedManifest(const QByteArray &json, const QByteArray &signature,
                                         const Release &release, const QString &installedVersion,
                                         const QByteArray &publicCoordinates)
{
    if (json.isEmpty() || json.size() > manifestLimit || json.size() != release.manifestBytes
        || !verifySignature(json, signature, publicCoordinates))
        return {};
    const auto document = QJsonDocument::fromJson(json);
    const auto object = document.object();
    if (!document.isObject() || object.size() != 7 || positiveInteger(object.value("schema"), 1) != 1
        || object.value("repository").toString() != repository
        || object.value("platform").toString() != platform)
        return {};

    const auto versionText = object.value("version").toString();
    const auto version = parseVersion(versionText);
    const auto installed = parseVersion(installedVersion);
    const auto name = object.value("file").toString();
    const auto bytes = positiveInteger(object.value("bytes"), installerLimit);
    const auto hash = object.value("sha256").toString();
    static const QRegularExpression sha256Pattern(QStringLiteral(R"(\A[0-9a-f]{64}\z)"));
    if (!version || !installed || !eligibleVersion(*version, *installed)
        || release.version != versionText || release.tag != u'v' + versionText
        || !isInstallerName(versionText, name) || bytes <= 0 || release.installers.value(name, -1) != bytes
        || !sha256Pattern.match(hash).hasMatch())
        return {};
    return Manifest{versionText, name, bytes, QByteArray::fromHex(hash.toLatin1())};
}

bool installerMatches(const Manifest &manifest, qint64 bytes, const QByteArray &sha256)
{
    return manifest.bytes > 0 && bytes == manifest.bytes && sha256.size() == 32
           && sha256 == manifest.sha256;
}

bool allowedAssetUrl(const QUrl &url, const QString &tag, const QString &assetName)
{
    if (!tag.startsWith(u'v') || !parseVersion(tag.mid(1))
        || (assetName != manifestName && assetName != signatureName && !isInstallerName(tag.mid(1), assetName)))
        return false;
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.hasFragment()
        || url.authority().contains(u'@') || url.port(443) != 443 || url.toEncoded().size() > 8192)
        return false;
    if (url == assetUrl(tag, assetName))
        return true;
    static const QRegularExpression cdnPath(QStringLiteral(
        R"(\A/github-production-release-asset/1446711230/[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\z)"));
    return url.host() == QStringLiteral("release-assets.githubusercontent.com") && url.hasQuery()
           && cdnPath.match(url.path(QUrl::FullyEncoded)).hasMatch();
}

} // namespace update

struct UpdateManager::State {
    enum class Request { Releases, Manifest, Signature, Installer };

    State(UpdateManager &manager, QWidget *parentWindow) : owner(manager), window(parentWindow)
    {
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &owner, [this] {
            fail(QStringLiteral("업데이트 서버 응답 시간이 초과되었습니다."));
        });
        QObject::connect(&verifyTimer, &QTimer::timeout, &owner, [this] { verifyChunk(); });
    }

    ~State() { clear(); }

    void startCheck(bool userRequested);
    QString requestAsset() const;
    qint64 responseLimit() const;
    void fetch(Request requestKind, const QUrl &url, int hops = 0);
    void transferEvent(DWORD status, const QByteArray &bytes);
    void receivedHeaders();
    void readNext();
    void receivedResponse();
    void closeTransfer();
    void ask(const QString &message, const QString &action, std::function<void()> accepted);
    void offerDownload();
    void chooseFolder();
    void beginDownload(const QString &folder);
    void setProgress(qint64 bytes);
    void beginVerification();
    void verifyChunk();
    void fail(const QString &message) { finish(message, true); }
    void finish(const QString &message, bool warning);
    void closeProgress();
    void clear(bool keepInstaller = false);

    UpdateManager &owner;
    QPointer<QWidget> window;
    QTimer deadline;
    QDeadlineTimer transferDeadline;
    QTimer verifyTimer;
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    TransferContext *transfer = nullptr;
    quint64 transferGeneration = 0;
    QUrl requestUrl;
    QPointer<QDialog> dialog;
    QPointer<QProgressDialog> progress;
    QPointer<QPushButton> checkButton;
    bool startupScheduled = false;
    bool checked = false;
    bool busy = false;
    bool interactive = false;
    bool createdFile = false;
    Request kind = Request::Releases;
    int redirects = 0;
    QString installed;
    update::Release release;
    update::Manifest manifest;
    QByteArray buffer;
    QByteArray manifestData;
    qint64 received = 0;
    QString stagingPath;
    QFile output;
    HANDLE stagingLock = INVALID_HANDLE_VALUE;
    HANDLE installerLock = INVALID_HANDLE_VALUE;
    qint64 hashedBytes = 0;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QByteArray verifyBuffer;
};

void UpdateManager::State::startCheck(bool userRequested)
{
    if (busy || !window)
        return;
    busy = true;
    checked = true;
    interactive = userRequested;
    installed = QCoreApplication::applicationVersion();
    const auto otherVersion = existingVersion();
    const auto other = parseVersion(otherVersion);
    const auto running = parseVersion(installed);
    if (other && running && compareVersions(*other, *running) > 0)
        installed = otherVersion;
    if (checkButton)
        checkButton->setEnabled(false);
    fetch(Request::Releases, releaseListUrl);
}

QString UpdateManager::State::requestAsset() const
{
    switch (kind) {
    case Request::Manifest: return manifestName;
    case Request::Signature: return signatureName;
    case Request::Installer: return manifest.file;
    default: return {};
    }
}

qint64 UpdateManager::State::responseLimit() const
{
    switch (kind) {
    case Request::Releases: return releaseListLimit;
    case Request::Manifest: return release.manifestBytes;
    case Request::Signature: return signatureSize;
    case Request::Installer: return manifest.bytes;
    }
    return 0;
}

void UpdateManager::State::fetch(Request requestKind, const QUrl &url, int hops)
{
    kind = requestKind;
    if (hops == 0)
        transferDeadline.setRemainingTime(kind == Request::Installer ? 2 * 60 * 60 * 1000 : 20000);
    if (transferDeadline.hasExpired()) {
        fail(QStringLiteral("업데이트 서버 응답 시간이 초과되었습니다."));
        return;
    }
    redirects = hops;
    received = 0;
    buffer.clear();
    const bool trusted = kind == Request::Releases ? url == releaseListUrl
        : update::allowedAssetUrl(url, release.tag, requestAsset());
    if (!trusted) {
        fail(QStringLiteral("업데이트 다운로드 주소를 확인할 수 없습니다."));
        return;
    }

    requestUrl = url;
    session = WinHttpOpen(L"LumaPlayer", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!session || !WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000)
        || !WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        fail(QStringLiteral("보안 업데이트 연결을 준비할 수 없습니다."));
        return;
    }
    const auto host = url.host();
    connection = WinHttpConnect(session, reinterpret_cast<LPCWSTR>(host.utf16()), INTERNET_DEFAULT_HTTPS_PORT, 0);
    auto resource = url.path(QUrl::FullyEncoded);
    if (url.hasQuery())
        resource += u'?' + url.query(QUrl::FullyEncoded);
    if (connection) {
        request = WinHttpOpenRequest(connection, L"GET", reinterpret_cast<LPCWSTR>(resource.utf16()),
                                     nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE | WINHTTP_FLAG_ESCAPE_DISABLE_QUERY);
    }
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_REDIRECTS;
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    DWORD maxHeaders = 64 * 1024;
    if (!request || !WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))
        || !WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon))
        || !WinHttpSetOption(request, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &maxHeaders, sizeof(maxHeaders))) {
        fail(QStringLiteral("보안 업데이트 요청을 준비할 수 없습니다."));
        return;
    }

    auto context = std::make_unique<TransferContext>();
    const auto generation = ++transferGeneration;
    context->receiver = &owner;
    context->deliver = [this, generation](DWORD status, const QByteArray &bytes) {
        if (generation == transferGeneration)
            transferEvent(status, bytes);
    };
    DWORD_PTR cookie = reinterpret_cast<DWORD_PTR>(context.get());
    const DWORD notifications = WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE | WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE
                                | WINHTTP_CALLBACK_FLAG_READ_COMPLETE | WINHTTP_CALLBACK_FLAG_REQUEST_ERROR
                                | WINHTTP_CALLBACK_FLAG_HANDLES;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &cookie, sizeof(cookie))
        || WinHttpSetStatusCallback(request, transferCallback, notifications, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        fail(QStringLiteral("업데이트 요청을 시작할 수 없습니다."));
        return;
    }
    transfer = context.release();
    const wchar_t *headers = kind == Request::Releases
        ? L"Accept: application/vnd.github+json\r\nAccept-Encoding: identity\r\n"
        : L"Accept: application/octet-stream\r\nAccept-Encoding: identity\r\n";
    deadline.start(int(transferDeadline.remainingTime()));
    if (!WinHttpSendRequest(request, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, cookie)
        && GetLastError() != ERROR_IO_PENDING) {
        fail(QStringLiteral("GitHub에서 업데이트 파일을 받을 수 없습니다."));
    }
}

void UpdateManager::State::transferEvent(DWORD status, const QByteArray &bytes)
{
    if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE) {
        if (!WinHttpReceiveResponse(request, nullptr) && GetLastError() != ERROR_IO_PENDING)
            fail(QStringLiteral("GitHub 응답을 받을 수 없습니다."));
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        receivedHeaders();
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        fail(QStringLiteral("GitHub에서 업데이트 파일을 받을 수 없습니다."));
        return;
    }
    if (status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
        return;
    if (bytes.isEmpty()) {
        receivedResponse();
        return;
    }
    if (bytes.size() > responseLimit() - received) {
        fail(QStringLiteral("업데이트 응답이 허용된 크기를 초과했습니다."));
        return;
    }
    received += bytes.size();
    if (kind == Request::Installer) {
        if (output.write(bytes) != bytes.size()) {
            fail(QStringLiteral("설치 파일을 저장할 수 없습니다. 선택한 폴더의 여유 공간을 확인해 주세요."));
            return;
        }
        setProgress(received);
    } else {
        buffer.append(bytes);
    }
    readNext();
}

void UpdateManager::State::receivedHeaders()
{
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        fail(QStringLiteral("GitHub 응답 상태를 읽을 수 없습니다."));
        return;
    }
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        QString location;
        if (kind == Request::Releases || redirects >= maxRedirects
            || !responseHeader(request, WINHTTP_QUERY_LOCATION, location) || location.isEmpty()) {
            fail(QStringLiteral("업데이트 다운로드 주소를 확인할 수 없습니다."));
            return;
        }
        const QUrl target(location, QUrl::StrictMode);
        if (!target.isValid()) {
            fail(QStringLiteral("업데이트 다운로드 주소를 확인할 수 없습니다."));
            return;
        }
        const auto next = requestUrl.resolved(target);
        closeTransfer();
        fetch(kind, next, redirects + 1);
        return;
    }
    QString lengthHeader;
    QString encoding;
    if (status != 200 || !responseHeader(request, WINHTTP_QUERY_CONTENT_LENGTH, lengthHeader)
        || !responseHeader(request, WINHTTP_QUERY_CONTENT_ENCODING, encoding)
        || (!encoding.isEmpty() && encoding.compare(QStringLiteral("identity"), Qt::CaseInsensitive) != 0)) {
        fail(QStringLiteral("GitHub에서 업데이트 응답을 확인할 수 없습니다."));
        return;
    }
    if (!lengthHeader.isEmpty()) {
        bool ok = false;
        const auto length = lengthHeader.toLongLong(&ok);
        if (!ok || !numeric(lengthHeader) || length < 0 || length > responseLimit()
            || (kind != Request::Releases && length != responseLimit())) {
            fail(QStringLiteral("업데이트 파일 크기가 확인된 정보와 다릅니다."));
            return;
        }
    }
    readNext();
}

void UpdateManager::State::readNext()
{
    // A completed read is consumed on the GUI thread before requesting another chunk.
    const auto length = std::min(networkChunk, responseLimit() - received + 1);
    if (!WinHttpReadData(request, transfer->readBuffer.data(), DWORD(length), nullptr)
        && GetLastError() != ERROR_IO_PENDING)
        fail(QStringLiteral("업데이트 파일을 읽을 수 없습니다."));
}

void UpdateManager::State::closeTransfer()
{
    deadline.stop();
    ++transferGeneration;
    if (auto *context = std::exchange(transfer, nullptr)) {
        std::lock_guard lock(context->mutex);
        context->receiver = nullptr;
    }
    if (request)
        WinHttpCloseHandle(std::exchange(request, nullptr));
    if (connection)
        WinHttpCloseHandle(std::exchange(connection, nullptr));
    if (session)
        WinHttpCloseHandle(std::exchange(session, nullptr));
}

void UpdateManager::State::receivedResponse()
{
    closeTransfer();
    if (kind != Request::Releases && received != responseLimit()) {
        fail(QStringLiteral("업데이트 응답이 완전하지 않습니다."));
        return;
    }

    switch (kind) {
    case Request::Releases: {
        const auto document = QJsonDocument::fromJson(buffer);
        if (!document.isArray()) {
            fail(QStringLiteral("GitHub 릴리스 목록을 읽을 수 없습니다."));
            return;
        }
        const auto candidate = update::newestReleaseFromArray(document.array(), installed);
        if (!candidate) {
            finish(QStringLiteral("설치 가능한 업데이트가 없습니다."), false);
            return;
        }
        release = *candidate;
        fetch(Request::Manifest, assetUrl(release.tag, manifestName));
        break;
    }
    case Request::Manifest:
        manifestData = buffer;
        fetch(Request::Signature, assetUrl(release.tag, signatureName));
        break;
    case Request::Signature: {
        const auto verified = update::verifiedManifest(manifestData, buffer, release, installed,
                                                       QByteArray::fromHex(trustedCoordinates));
        if (!verified) {
            fail(QStringLiteral("업데이트 서명을 확인할 수 없습니다."));
            return;
        }
        manifest = *verified;
        offerDownload();
        break;
    }
    case Request::Installer:
        beginVerification();
        break;
    }
}

void UpdateManager::State::ask(const QString &message, const QString &action, std::function<void()> accepted)
{
    auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("LumaPlayer 업데이트"),
                                message, QMessageBox::NoButton, window);
    box->setTextFormat(Qt::PlainText);
    auto *confirm = box->addButton(action, QMessageBox::AcceptRole);
    auto *later = box->addButton(QStringLiteral("나중에"), QMessageBox::RejectRole);
    box->setDefaultButton(later);
    box->setEscapeButton(later);
    dialog = box;
    QObject::connect(box, &QDialog::finished, &owner, [this, box, confirm, accepted = std::move(accepted)] {
        if (dialog != box)
            return;
        dialog = nullptr;
        const bool proceed = box->clickedButton() == confirm;
        box->deleteLater();
        if (proceed)
            accepted();
        else
            clear();
    });
    box->open();
}

void UpdateManager::State::offerDownload()
{
    if (!interactive && !notificationsEnabled()) {
        clear();
        return;
    }
    const auto size = QLocale().formattedDataSize(manifest.bytes);
    ask(QStringLiteral("LumaPlayer %1 업데이트를 받을 수 있습니다.\n설치 파일 크기: %2")
            .arg(manifest.version, size), QStringLiteral("다운로드…"), [this] {
        interactive = true;
        chooseFolder();
    });
}

void UpdateManager::State::chooseFolder()
{
    auto *picker = new QFileDialog(window, QStringLiteral("설치 파일을 저장할 폴더 선택"),
                                   QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    picker->setFileMode(QFileDialog::Directory);
    picker->setOption(QFileDialog::ShowDirsOnly);
    dialog = picker;
    QObject::connect(picker, &QDialog::finished, &owner, [this, picker](int result) {
        if (dialog != picker)
            return;
        dialog = nullptr;
        const auto folder = picker->selectedFiles().value(0);
        picker->deleteLater();
        if (result == QDialog::Accepted && !folder.isEmpty())
            beginDownload(folder);
        else
            clear();
    });
    picker->open();
}

void UpdateManager::State::beginDownload(const QString &folder)
{
    const auto selected = QFileInfo(folder).canonicalFilePath();
    const QStorageInfo storage(selected);
    const auto volume = QDir::toNativeSeparators(storage.rootPath());
    const auto drive = GetDriveTypeW(reinterpret_cast<LPCWSTR>(volume.utf16()));
    DWORD volumeFlags = 0;
    if (selected.isEmpty() || !storage.isValid() || !storage.isReady()
        || drive != DRIVE_FIXED
        || !GetVolumeInformationW(reinterpret_cast<LPCWSTR>(volume.utf16()), nullptr, 0, nullptr, nullptr, &volumeFlags, nullptr, 0)
        || !(volumeFlags & FILE_PERSISTENT_ACLS)
        || storage.bytesAvailable() < manifest.bytes) {
        ask(QStringLiteral("설치 파일을 저장할 여유 공간이 있는 로컬 폴더를 선택해 주세요."),
            QStringLiteral("다른 폴더 선택…"), [this] { chooseFolder(); });
        return;
    }
    stagingPath = createPrivateStaging(selected);
    if (stagingPath.isEmpty()) {
        fail(QStringLiteral("선택한 폴더에 설치 파일을 준비할 수 없습니다."));
        return;
    }
    stagingLock = lockPath(stagingPath, true);
    output.setFileName(QDir(stagingPath).filePath(manifest.file + QStringLiteral(".part")));
    if (stagingLock == INVALID_HANDLE_VALUE || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        fail(QStringLiteral("설치 파일을 안전하게 저장할 수 없습니다."));
        return;
    }
    createdFile = true;
    progress = new QProgressDialog(QStringLiteral("업데이트 설치 파일을 받는 중…"),
                                   QStringLiteral("취소"), 0, 1000, window);
    progress->setWindowTitle(QStringLiteral("LumaPlayer 업데이트"));
    progress->setWindowModality(Qt::NonModal);
    progress->setAutoReset(false);
    progress->setAutoClose(false);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    QObject::connect(progress, &QProgressDialog::canceled, &owner, [this] { clear(); });
    progress->show();
    fetch(Request::Installer, assetUrl(release.tag, manifest.file));
}

void UpdateManager::State::setProgress(qint64 bytes)
{
    if (progress && manifest.bytes > 0)
        progress->setValue(int(bytes * 1000 / manifest.bytes));
}

void UpdateManager::State::beginVerification()
{
    if (!output.flush()) {
        fail(QStringLiteral("설치 파일을 저장할 수 없습니다."));
        return;
    }
    output.close();
    const auto finalPath = QDir(stagingPath).filePath(manifest.file);
    const auto from = QDir::toNativeSeparators(output.fileName());
    const auto to = QDir::toNativeSeparators(finalPath);
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()), reinterpret_cast<LPCWSTR>(to.utf16()), 0)) {
        fail(QStringLiteral("설치 파일을 준비할 수 없습니다."));
        return;
    }
    output.setFileName(finalPath);
    // Keep this handle open from the on-disk check through process creation.
    installerLock = lockPath(finalPath, false);
    LARGE_INTEGER size{};
    if (installerLock == INVALID_HANDLE_VALUE || !GetFileSizeEx(installerLock, &size)
        || size.QuadPart != manifest.bytes) {
        fail(QStringLiteral("저장된 설치 파일의 크기를 확인할 수 없습니다."));
        return;
    }
    hashedBytes = 0;
    hash.reset();
    verifyBuffer.resize(512 * 1024);
    if (progress) {
        progress->setLabelText(QStringLiteral("설치 파일 무결성을 확인하는 중…"));
        progress->setValue(0);
    }
    verifyTimer.start(0);
}

void UpdateManager::State::verifyChunk()
{
    DWORD count = 0;
    if (!ReadFile(installerLock, verifyBuffer.data(), DWORD(verifyBuffer.size()), &count, nullptr)
        || count > manifest.bytes - hashedBytes) {
        fail(QStringLiteral("설치 파일을 확인할 수 없습니다."));
        return;
    }
    if (count > 0) {
        hash.addData(QByteArrayView(verifyBuffer.constData(), qsizetype(count)));
        hashedBytes += count;
        setProgress(hashedBytes);
        return;
    }
    verifyTimer.stop();
    if (!update::installerMatches(manifest, hashedBytes, hash.result())) {
        fail(QStringLiteral("설치 파일의 SHA-256이 서명된 정보와 다릅니다."));
        return;
    }
    closeProgress();
    ask(QStringLiteral("LumaPlayer %1 설치를 시작할까요?\n설치를 시작하면 플레이어가 종료됩니다.")
            .arg(manifest.version), QStringLiteral("설치 시작"), [this] {
        const auto targetHash = updateTargetHash(manifest.version);
        if (targetHash.isEmpty()) {
            fail(QStringLiteral("설치 대상 버전을 확인할 수 없거나 더 새 버전이 설치되어 있습니다."));
            return;
        }
        if (!startInstaller(installerLock, stagingPath, targetHash)) {
            fail(QStringLiteral("설치 프로그램을 시작할 수 없습니다."));
            return;
        }
        clear(true);
        QCoreApplication::quit();
    });
}

void UpdateManager::State::finish(const QString &message, bool warning)
{
    const bool report = interactive;
    clear();
    if (!report || !window)
        return;
    auto *notice = new QMessageBox(warning ? QMessageBox::Warning : QMessageBox::Information,
                                   QStringLiteral("LumaPlayer 업데이트"), message, QMessageBox::Ok, window);
    notice->setTextFormat(Qt::PlainText);
    notice->setAttribute(Qt::WA_DeleteOnClose);
    if (warning) {
        notice->setInformativeText(QStringLiteral("공식 릴리스에서 설치 파일을 직접 받을 수 있습니다."));
        auto *releases = notice->addButton(QStringLiteral("공식 다운로드 열기"), QMessageBox::ActionRole);
        QObject::connect(releases, &QPushButton::clicked, notice, [] {
            QDesktopServices::openUrl(publicReleasesUrl);
        });
    }
    notice->open();
}

void UpdateManager::State::closeProgress()
{
    if (progress) {
        QObject::disconnect(progress, nullptr, &owner, nullptr);
        progress->hide();
        progress->deleteLater();
        progress = nullptr;
    }
}

void UpdateManager::State::clear(bool keepInstaller)
{
    closeTransfer();
    verifyTimer.stop();
    verifyBuffer.clear();
    if (dialog) {
        QObject::disconnect(dialog, nullptr, &owner, nullptr);
        dialog->hide();
        dialog->deleteLater();
        dialog = nullptr;
    }
    closeProgress();
    output.close();
    if (installerLock != INVALID_HANDLE_VALUE) {
        CloseHandle(installerLock);
        installerLock = INVALID_HANDLE_VALUE;
    }
    if (createdFile && !keepInstaller)
        output.remove();
    createdFile = false;
    if (stagingLock != INVALID_HANDLE_VALUE) {
        CloseHandle(stagingLock);
        stagingLock = INVALID_HANDLE_VALUE;
    }
    if (!stagingPath.isEmpty() && !keepInstaller)
        QDir().rmdir(stagingPath);
    stagingPath.clear();
    buffer.clear();
    manifestData.clear();
    release = {};
    manifest = {};
    busy = false;
    interactive = false;
    if (checkButton)
        checkButton->setEnabled(true);
}

UpdateManager::UpdateManager(QWidget *parent) : QObject(parent), state_(std::make_unique<State>(*this, parent)) {}
UpdateManager::~UpdateManager() = default;

void UpdateManager::addControls(QVBoxLayout *layout)
{
    if (!layout || state_->checkButton)
        return;
    auto *row = new QHBoxLayout;
    auto *alerts = new QCheckBox(QStringLiteral("업데이트 알림 받기"));
    alerts->setObjectName(QStringLiteral("updateNotifications"));
    alerts->setChecked(notificationsEnabled());
    auto *check = new QPushButton(QStringLiteral("업데이트 확인"));
    check->setObjectName(QStringLiteral("checkForUpdates"));
    check->setEnabled(!state_->busy);
    state_->checkButton = check;
    row->addWidget(alerts);
    row->addStretch();
    row->addWidget(check);
    layout->addLayout(row);
    connect(alerts, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue(notificationSetting, enabled);
        if (!enabled && state_->busy && !state_->interactive)
            state_->clear();
    });
    connect(check, &QPushButton::clicked, this, [this] { state_->startCheck(true); });
}

void UpdateManager::checkAtStartup()
{
    if (state_->startupScheduled)
        return;
    state_->startupScheduled = true;
    QTimer::singleShot(1500, this, [this] {
        if (!state_->checked && notificationsEnabled())
            state_->startCheck(false);
    });
}

} // namespace luma
