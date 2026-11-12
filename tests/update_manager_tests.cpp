#include "ui/update_manager.h"

#include <QCheckBox>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>
#include <windows.h>
#include <bcrypt.h>

using namespace luma::update;

namespace {

const QByteArray manifestJson = R"({"schema":1,"repository":"mk0000001/luma-player","platform":"windows-x64","version":"0.2.0-beta.10","file":"LumaPlayer-0.2.0-beta.10-setup.1-windows-x64-cuda.exe","bytes":4,"sha256":"9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"})";

QString installer(const QString &version)
{
    return QStringLiteral("LumaPlayer-%1-setup.1-windows-x64-cuda.exe").arg(version);
}

QJsonObject asset(const QString &name, qint64 bytes)
{
    return {{"name", name}, {"size", bytes}, {"state", "uploaded"}};
}

QJsonObject release(const QString &version, bool prerelease = true)
{
    return {{"tag_name", "v" + version}, {"draft", false}, {"prerelease", prerelease},
            {"published_at", "2026-11-12T00:00:00Z"},
            {"assets", QJsonArray{asset("luma-update.json", manifestJson.size()),
                                  asset("luma-update.sig", 64), asset(installer(version), 4)}}};
}

QByteArray json(const QJsonArray &array)
{
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

struct TestSignature {
    QByteArray coordinates;
    QByteArray signature;
};

TestSignature signFixture(const QByteArray &message)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0)
        return {};
    const auto closeAlgorithm = qScopeGuard([&] { BCryptCloseAlgorithmProvider(algorithm, 0); });

    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptGenerateKeyPair(algorithm, &key, 256, 0) != 0)
        return {};
    const auto destroyKey = qScopeGuard([&] { BCryptDestroyKey(key); });
    if (BCryptFinalizeKeyPair(key, 0) != 0)
        return {};

    QByteArray publicBlob(sizeof(BCRYPT_ECCKEY_BLOB) + 64, '\0');
    ULONG exported = 0;
    if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                        reinterpret_cast<PUCHAR>(publicBlob.data()), ULONG(publicBlob.size()),
                        &exported, 0) != 0 || exported != ULONG(publicBlob.size()))
        return {};

    QByteArray hash = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
    QByteArray signature(64, '\0');
    ULONG written = 0;
    if (BCryptSignHash(key, nullptr, reinterpret_cast<PUCHAR>(hash.data()), ULONG(hash.size()),
                       reinterpret_cast<PUCHAR>(signature.data()), ULONG(signature.size()),
                       &written, 0) != 0 || written != 64)
        return {};
    return {publicBlob.mid(sizeof(BCRYPT_ECCKEY_BLOB)), signature};
}

Release matchingRelease()
{
    return {"v0.2.0-beta.10", "0.2.0-beta.10", manifestJson.size(),
            {{"LumaPlayer-0.2.0-beta.10-setup.1-windows-x64-cuda.exe", 4}}};
}

} // namespace

class UpdateManagerTests final : public QObject {
    Q_OBJECT

private slots:
    void picksHighestNumericBeta();
    void stableChannelSkipsPrereleases();
    void ignoresUnpublishedAndUnsignedReleases();
    void requiresStrictlyNewerSemver();
    void verifiesExactSignedBytes();
    void rejectsSignedWrongScopeAndDowngrades();
    void rejectsInstallerSizeAndHashChanges();
    void restrictsDownloadOrigins();
    void optOutPersistsAndManualCheckRemainsAvailable();
    void liveManualCheckWithNotificationsDisabled();
};

void UpdateManagerTests::picksHighestNumericBeta()
{
    const auto newest = newestRelease(json({release("0.2.0-beta.9"), release("0.2.0-beta.10")}),
                                      "0.2.0-beta.2");
    QVERIFY(newest);
    QCOMPARE(newest->version, QString("0.2.0-beta.10"));
}

void UpdateManagerTests::stableChannelSkipsPrereleases()
{
    const auto newest = newestRelease(json({release("0.3.0-beta.1"), release("0.2.1", false)}),
                                      "0.2.0");
    QVERIFY(newest);
    QCOMPARE(newest->version, QString("0.2.1"));
}

void UpdateManagerTests::ignoresUnpublishedAndUnsignedReleases()
{
    auto draft = release("0.3.0-beta.1");
    draft["draft"] = true;
    auto unpublished = release("0.4.0-beta.1");
    unpublished["published_at"] = QJsonValue::Null;
    auto unsignedRelease = release("0.5.0-beta.1");
    unsignedRelease["assets"] = QJsonArray{asset(installer("0.5.0-beta.1"), 4)};
    auto zipOnly = release("0.6.0-beta.1");
    zipOnly["assets"] = QJsonArray{asset("luma-update.json", 100), asset("luma-update.sig", 64),
                                  asset("LumaPlayer-0.6.0-beta.1-windows-x64-cuda.zip", 4)};
    const auto newest = newestRelease(json({draft, unpublished, unsignedRelease, zipOnly,
                                           release("0.2.0-beta.10")}), "0.2.0-beta.2");
    QVERIFY(newest);
    QCOMPARE(newest->version, QString("0.2.0-beta.10"));
}

void UpdateManagerTests::requiresStrictlyNewerSemver()
{
    QVERIFY(!newestRelease(json({release("0.2.0-beta.1"), release("0.2.0-beta.2"),
                                 release("0.2.0-beta.2+rebuild"), release("0.2.0-beta.09"),
                                 release("00.2.0-beta.10"), release("0.2.0-beta..10")}),
                           "0.2.0-beta.2"));
    const auto stable = newestRelease(json({release("0.2.0", false)}), "0.2.0-beta.10");
    QVERIFY(stable);
    QCOMPARE(stable->version, QString("0.2.0"));
}

void UpdateManagerTests::verifiesExactSignedBytes()
{
    const auto signedFixture = signFixture(manifestJson);
    QCOMPARE(signedFixture.signature.size(), 64);
    const auto verified = verifiedManifest(manifestJson, signedFixture.signature, matchingRelease(),
                                           "0.2.0-beta.2", signedFixture.coordinates);
    QVERIFY(verified);
    QCOMPARE(verified->bytes, 4);
    QCOMPARE(verified->file, QString("LumaPlayer-0.2.0-beta.10-setup.1-windows-x64-cuda.exe"));

    auto altered = manifestJson;
    altered.replace("9f86d081", "af86d081");
    QVERIFY(!verifiedManifest(altered, signedFixture.signature, matchingRelease(), "0.2.0-beta.2",
                              signedFixture.coordinates));
    auto wrongSignature = signedFixture.signature;
    wrongSignature[0] = char(wrongSignature.at(0) ^ 1);
    QVERIFY(!verifiedManifest(manifestJson, wrongSignature, matchingRelease(), "0.2.0-beta.2",
                              signedFixture.coordinates));
    QVERIFY(!verifiedManifest(manifestJson, signedFixture.signature.left(63), matchingRelease(),
                              "0.2.0-beta.2", signedFixture.coordinates));
    const auto otherKey = signFixture(manifestJson);
    QCOMPARE(otherKey.coordinates.size(), 64);
    QVERIFY(!verifiedManifest(manifestJson, signedFixture.signature, matchingRelease(), "0.2.0-beta.2",
                              otherKey.coordinates));
}

void UpdateManagerTests::rejectsSignedWrongScopeAndDowngrades()
{
    auto wrongScope = manifestJson;
    wrongScope.replace("windows-x64\"", "windows-x86\"");
    const auto wrongScopeSignature = signFixture(wrongScope);
    QCOMPARE(wrongScopeSignature.signature.size(), 64);
    QVERIFY(!verifiedManifest(wrongScope, wrongScopeSignature.signature, matchingRelease(),
                              "0.2.0-beta.2", wrongScopeSignature.coordinates));

    const auto signedFixture = signFixture(manifestJson);
    QCOMPARE(signedFixture.signature.size(), 64);
    QVERIFY(!verifiedManifest(manifestJson, signedFixture.signature, matchingRelease(), "0.2.0-beta.11",
                              signedFixture.coordinates));
    auto inconsistent = matchingRelease();
    inconsistent.installers[installer("0.2.0-beta.10")] = 5;
    QVERIFY(!verifiedManifest(manifestJson, signedFixture.signature, inconsistent, "0.2.0-beta.2",
                              signedFixture.coordinates));
}

void UpdateManagerTests::rejectsInstallerSizeAndHashChanges()
{
    const auto signedFixture = signFixture(manifestJson);
    const auto verified = verifiedManifest(manifestJson, signedFixture.signature, matchingRelease(),
                                           "0.2.0-beta.2", signedFixture.coordinates);
    QVERIFY(verified);
    const auto correctHash = QCryptographicHash::hash("test", QCryptographicHash::Sha256);
    const auto changedHash = QCryptographicHash::hash("text", QCryptographicHash::Sha256);
    QVERIFY(installerMatches(*verified, 4, correctHash));
    QVERIFY(!installerMatches(*verified, 3, correctHash));
    QVERIFY(!installerMatches(*verified, 4, changedHash));
}

void UpdateManagerTests::restrictsDownloadOrigins()
{
    const QString tag = "v0.2.0-beta.10";
    const QString assetName = "luma-update.sig";
    QVERIFY(allowedAssetUrl(QUrl("https://github.com/mk0000001/luma-player/releases/download/v0.2.0-beta.10/luma-update.sig"), tag, assetName));
    QVERIFY(allowedAssetUrl(QUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1446711230/c8f632dc-cfdd-412a-99c2-03272bbe608e?sp=r&sig=fixture"), tag, assetName));
    QVERIFY(!allowedAssetUrl(QUrl("http://github.com/mk0000001/luma-player/releases/download/v0.2.0-beta.10/luma-update.sig"), tag, assetName));
    QVERIFY(!allowedAssetUrl(QUrl("https://github.com/other/luma-player/releases/download/v0.2.0-beta.10/luma-update.sig"), tag, assetName));
    QVERIFY(!allowedAssetUrl(QUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1446711231/c8f632dc-cfdd-412a-99c2-03272bbe608e?sp=r&sig=fixture"), tag, assetName));
    QVERIFY(!allowedAssetUrl(QUrl("https://release-assets.githubusercontent.com.invalid/github-production-release-asset/1446711230/c8f632dc-cfdd-412a-99c2-03272bbe608e?sp=r&sig=fixture"), tag, assetName));
    QVERIFY(!allowedAssetUrl(QUrl("https://user@release-assets.githubusercontent.com/github-production-release-asset/1446711230/c8f632dc-cfdd-412a-99c2-03272bbe608e?sp=r&sig=fixture"), tag, assetName));
    QVERIFY(!allowedAssetUrl(QUrl("https://release-assets.githubusercontent.com:8443/github-production-release-asset/1446711230/c8f632dc-cfdd-412a-99c2-03272bbe608e?sp=r&sig=fixture"), tag, assetName));
}

void UpdateManagerTests::optOutPersistsAndManualCheckRemainsAvailable()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("LumaPlayer.Update.Tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    {
        QWidget window;
        QVBoxLayout layout(&window);
        luma::UpdateManager manager(&window);
        manager.addControls(&layout);
        auto *alerts = window.findChild<QCheckBox *>("updateNotifications");
        QVERIFY(alerts);
        QVERIFY(alerts->isChecked());
        alerts->setChecked(false);
    }
    QWidget window;
    QVBoxLayout layout(&window);
    luma::UpdateManager manager(&window);
    manager.addControls(&layout);
    auto *alerts = window.findChild<QCheckBox *>("updateNotifications");
    auto *check = window.findChild<QPushButton *>("checkForUpdates");
    QVERIFY(alerts);
    QVERIFY(check);
    QVERIFY(!alerts->isChecked());
    QVERIFY(check->isEnabled());
}

void UpdateManagerTests::liveManualCheckWithNotificationsDisabled()
{
    if (qEnvironmentVariableIntValue("LUMA_UPDATE_LIVE_SMOKE") != 1)
        QSKIP("Set LUMA_UPDATE_LIVE_SMOKE=1 to check the public GitHub release list.");
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("LumaPlayer.Update.Tests");
    QCoreApplication::setApplicationVersion("999.0.0");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings().setValue("update/notifyOnStartup", false);

    QWidget window;
    QVBoxLayout layout(&window);
    luma::UpdateManager manager(&window);
    manager.addControls(&layout);
    auto *check = window.findChild<QPushButton *>("checkForUpdates");
    QVERIFY(check);
    check->click();
    QVERIFY(!check->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>(), 25000);
    auto *notice = window.findChild<QMessageBox *>();
    QCOMPARE(notice->text(), QStringLiteral("설치 가능한 업데이트가 없습니다."));
    QVERIFY(check->isEnabled());
    notice->accept();
}

QTEST_MAIN(UpdateManagerTests)
#include "update_manager_tests.moc"
