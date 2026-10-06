/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QString>
#include <QtGlobal>
#include <QDir>
#include <QUrl>

#if defined(Q_OS_ANDROID)

#include <QDebug>
#include <QtAndroidExtras/QAndroidJniEnvironment>
#include <QtAndroidExtras/QAndroidJniObject>
#include <QtAndroidExtras/QtAndroid>

#include <sys/stat.h>
#include <unistd.h>

namespace AndroidDocumentUtils {

inline bool isContentUri(const QString& value)
{
    return value.startsWith(QStringLiteral("content://"), Qt::CaseInsensitive);
}

inline bool clearException(const char* operation)
{
    QAndroidJniEnvironment env;
    if(!env->ExceptionCheck()) {
        return false;
    }

    env->ExceptionClear();
    qWarning() << "Android document operation failed:" << operation;
    return true;
}

inline QAndroidJniObject uriFromString(const QString& uriString)
{
    const QAndroidJniObject value = QAndroidJniObject::fromString(uriString);
    QAndroidJniObject uri = QAndroidJniObject::callStaticObjectMethod(
                "android/net/Uri",
                "parse",
                "(Ljava/lang/String;)Landroid/net/Uri;",
                value.object<jstring>());
    if(clearException("Uri.parse")) {
        return QAndroidJniObject();
    }
    return uri;
}

inline QAndroidJniObject contentResolver()
{
    const QAndroidJniObject context = QtAndroid::androidContext();
    if(!context.isValid()) {
        return QAndroidJniObject();
    }

    QAndroidJniObject resolver = context.callObjectMethod(
                "getContentResolver",
                "()Landroid/content/ContentResolver;");
    if(clearException("Context.getContentResolver")) {
        return QAndroidJniObject();
    }
    return resolver;
}

inline bool takePersistableReadPermission(const QString& uriString, int returnedFlags)
{
    const QAndroidJniObject resolver = contentResolver();
    const QAndroidJniObject uri = uriFromString(uriString);
    if(!resolver.isValid() || !uri.isValid()) {
        return false;
    }

    // Intent.FLAG_GRANT_READ_URI_PERMISSION and FLAG_GRANT_WRITE_URI_PERMISSION
    // are the only flags accepted by takePersistableUriPermission.
    int permissionFlags = returnedFlags & 0x3;
    permissionFlags |= 0x1;
    resolver.callMethod<void>(
                "takePersistableUriPermission",
                "(Landroid/net/Uri;I)V",
                uri.object<jobject>(),
                static_cast<jint>(permissionFlags));
    return !clearException("ContentResolver.takePersistableUriPermission");
}

inline int openReadOnlyFileDescriptor(const QString& uriString, qint64* size = nullptr)
{
    if(size) {
        *size = -1;
    }

    const QAndroidJniObject resolver = contentResolver();
    const QAndroidJniObject uri = uriFromString(uriString);
    const QAndroidJniObject mode = QAndroidJniObject::fromString(QStringLiteral("r"));
    if(!resolver.isValid() || !uri.isValid()) {
        return -1;
    }

    QAndroidJniObject descriptor = resolver.callObjectMethod(
                "openFileDescriptor",
                "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",
                uri.object<jobject>(),
                mode.object<jstring>());
    if(clearException("ContentResolver.openFileDescriptor") || !descriptor.isValid()) {
        return -1;
    }

    qint64 descriptorSize = descriptor.callMethod<jlong>("getStatSize", "()J");
    if(clearException("ParcelFileDescriptor.getStatSize")) {
        descriptorSize = -1;
    }

    const int fd = descriptor.callMethod<jint>("detachFd", "()I");
    if(clearException("ParcelFileDescriptor.detachFd") || fd < 0) {
        return -1;
    }

    if(descriptorSize < 0) {
        struct stat statBuffer;
        if(::fstat(fd, &statBuffer) == 0) {
            descriptorSize = static_cast<qint64>(statBuffer.st_size);
        }
    }
    if(size) {
        // Some document providers do not report a length even though the
        // descriptor is a valid seekable file. Treat that as an available
        // file with unknown (zero for display) size, not as a missing file.
        *size = descriptorSize >= 0 ? descriptorSize : 0;
    }
    return fd;
}

inline QString directExternalStoragePath(const QString& uriString)
{
    const QUrl uri(uriString);
    if(uri.scheme().compare(QStringLiteral("content"), Qt::CaseInsensitive) != 0
            || uri.host().compare(QStringLiteral("com.android.externalstorage.documents"),
                                  Qt::CaseInsensitive) != 0) {
        return QString();
    }

    const QString prefix = QStringLiteral("/document/");
    const QString uriPath = uri.path(QUrl::FullyDecoded);
    if(!uriPath.startsWith(prefix)) {
        return QString();
    }

    const QString documentId = uriPath.mid(prefix.length());
    const int separator = documentId.indexOf(QLatin1Char(':'));
    if(separator <= 0) {
        return QString();
    }

    const QString volumeId = documentId.left(separator);
    const QString relativePath = documentId.mid(separator + 1);
    const QString volumeRoot = volumeId.compare(QStringLiteral("primary"),
                                                Qt::CaseInsensitive) == 0
            ? QStringLiteral("/storage/emulated/0")
            : QStringLiteral("/storage/%1").arg(volumeId);
    const QString candidate = QDir::cleanPath(
                relativePath.isEmpty()
                ? volumeRoot
                : QStringLiteral("%1/%2").arg(volumeRoot, relativePath));

    // Document IDs are supplied by Android, but still prevent an unexpected
    // path from escaping the selected storage volume.
    if(candidate != volumeRoot && !candidate.startsWith(volumeRoot + QLatin1Char('/'))) {
        return QString();
    }
    return candidate;
}

inline void closeFileDescriptor(int fd)
{
    if(fd >= 0) {
        ::close(fd);
    }
}

} // namespace AndroidDocumentUtils

#else

namespace AndroidDocumentUtils {

inline bool isContentUri(const QString&)
{
    return false;
}

inline bool takePersistableReadPermission(const QString&, int)
{
    return false;
}

inline int openReadOnlyFileDescriptor(const QString&, qint64* = nullptr)
{
    return -1;
}

inline QString directExternalStoragePath(const QString&)
{
    return QString();
}

inline void closeFileDescriptor(int)
{
}

} // namespace AndroidDocumentUtils

#endif
