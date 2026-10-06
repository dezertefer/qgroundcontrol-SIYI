/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


#include "QGCFileDialogController.h"
#include "QGCApplication.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include "AndroidDocumentUtils.h"

#include <QStandardPaths>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QPointer>

QGC_LOGGING_CATEGORY(QGCFileDialogControllerLog, "QGCFileDialogControllerLog")

bool QGCFileDialogController::openAndroidDocument()
{
#if defined(Q_OS_ANDROID)
    const QAndroidJniObject action = QAndroidJniObject::fromString(
                QStringLiteral("android.intent.action.OPEN_DOCUMENT"));
    QAndroidJniObject intent(
                "android/content/Intent",
                "(Ljava/lang/String;)V",
                action.object<jstring>());
    if(!intent.isValid() || AndroidDocumentUtils::clearException("Intent constructor")) {
        return false;
    }

    const QAndroidJniObject openableCategory = QAndroidJniObject::fromString(
                QStringLiteral("android.intent.category.OPENABLE"));
    intent.callObjectMethod(
                "addCategory",
                "(Ljava/lang/String;)Landroid/content/Intent;",
                openableCategory.object<jstring>());

    const QAndroidJniObject mimeType = QAndroidJniObject::fromString(QStringLiteral("*/*"));
    intent.callObjectMethod(
                "setType",
                "(Ljava/lang/String;)Landroid/content/Intent;",
                mimeType.object<jstring>());

    // Persistable read access is required so a mounted SD-card database can
    // be opened again after QGC restarts.
    constexpr jint grantReadUriPermission = 0x00000001;
    constexpr jint grantPersistableUriPermission = 0x00000040;
    intent.callObjectMethod(
                "addFlags",
                "(I)Landroid/content/Intent;",
                grantReadUriPermission | grantPersistableUriPermission);
    if(AndroidDocumentUtils::clearException("configure document picker intent")) {
        return false;
    }

    QPointer<QGCFileDialogController> guard(this);
    QtAndroid::startActivity(intent, 0x5147,
        [guard](int, int resultCode, const QAndroidJniObject& resultData) {
            QString selectedUri;
            if(resultCode == -1 && resultData.isValid()) { // Activity.RESULT_OK
                QAndroidJniObject uri = resultData.callObjectMethod(
                            "getData",
                            "()Landroid/net/Uri;");
                if(uri.isValid() && !AndroidDocumentUtils::clearException("Intent.getData")) {
                    QAndroidJniObject uriText = uri.callObjectMethod(
                                "toString",
                                "()Ljava/lang/String;");
                    if(uriText.isValid() && !AndroidDocumentUtils::clearException("Uri.toString")) {
                        selectedUri = uriText.toString();
                        const int flags = resultData.callMethod<jint>("getFlags", "()I");
                        const bool permissionPersisted =
                                !AndroidDocumentUtils::clearException("Intent.getFlags")
                                && AndroidDocumentUtils::takePersistableReadPermission(
                                    selectedUri, flags);
                        qInfo().noquote() << "[OfflineMapDebug] Android document selected"
                                          << "uri=" << selectedUri
                                          << "persistentReadPermission=" << permissionPersisted;
                    }
                }
            }

            if(guard) {
                QMetaObject::invokeMethod(
                            guard.data(),
                            "androidDocumentSelected",
                            Qt::QueuedConnection,
                            Q_ARG(QString, selectedUri));
            }
        });
    return true;
#else
    return false;
#endif
}

QStringList QGCFileDialogController::getFiles(const QString& directoryPath, const QStringList& nameFilters)
{
    qCDebug(QGCFileDialogControllerLog) << "getFiles" << directoryPath << nameFilters;
    QStringList files;

    QDir fileDir(directoryPath);

    QFileInfoList fileInfoList = fileDir.entryInfoList(nameFilters,  QDir::Files, QDir::Name);

    for (const QFileInfo& fileInfo: fileInfoList) {
        qCDebug(QGCFileDialogControllerLog) << "getFiles found" << fileInfo.fileName();
        files << fileInfo.fileName();
    }

    return files;
}

bool QGCFileDialogController::fileExists(const QString& filename)
{
    return QFile(filename).exists();
}

QString QGCFileDialogController::fullyQualifiedFilename(const QString& directoryPath, const QString& filename, const QStringList& nameFilters)
{
    QString firstFileExtention;

    // Check that the filename has one of the specified file extensions

    bool extensionFound = true;
    if (nameFilters.count()) {
        extensionFound = false;
        for (const QString& nameFilter: nameFilters) {
            if (nameFilter.startsWith("*.")) {
                QString fileExtension = nameFilter.right(nameFilter.length() - 2);
                if (fileExtension != "*") {
                    if (firstFileExtention.isEmpty()) {
                        firstFileExtention = fileExtension;
                    }
                    if (filename.endsWith(fileExtension)) {
                        extensionFound = true;
                        break;
                    }
                }
            } else if (nameFilter != "*") {
                qCWarning(QGCFileDialogControllerLog) << "unsupported name filter format" << nameFilter;
            }
        }
    }

    // Add the extension if it is missing
    QString filenameWithExtension = filename;
    if (!extensionFound) {
        filenameWithExtension = QStringLiteral("%1.%2").arg(filename).arg(firstFileExtention);
    }

    return directoryPath + QStringLiteral("/") + filenameWithExtension;
}

void QGCFileDialogController::deleteFile(const QString& filename)
{
    QFile::remove(filename);
}

QString QGCFileDialogController::fullFolderPathToShortMobilePath(const QString& fullFolderPath)
{
#ifdef __mobile__
    QString defaultSavePath = qgcApp()->toolbox()->settingsManager()->appSettings()->savePath()->rawValueString();
    if (fullFolderPath.startsWith(defaultSavePath)) {
        int lastDirSepIndex = fullFolderPath.lastIndexOf(QStringLiteral("/"));
        return qgcApp()->applicationName() + QStringLiteral("/") + fullFolderPath.right(fullFolderPath.length() - lastDirSepIndex);
    } else {
        return fullFolderPath;
    }
#else
    qWarning() << "QGCFileDialogController::fullFolderPathToShortMobilePath should only be used in mobile builds";
    return fullFolderPath;
#endif
}
