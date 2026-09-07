/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AerokontikiMissionStore.h"

#include "QGCApplication.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kStorageVersion = 1;
const char* kVersionKey = "version";
const char* kMissionKey = "mission";
const char* kChecksumKey = "sha256";
}

AerokontikiMissionStore* AerokontikiMissionStore::instance(void)
{
    static AerokontikiMissionStore* store = new AerokontikiMissionStore(qgcApp());
    return store;
}

AerokontikiMissionStore::AerokontikiMissionStore(QObject* parent)
    : QObject(parent)
{
    const QString staleMissionPath = _storagePath();
    if (QFile::exists(staleMissionPath) && !QFile::remove(staleMissionPath)) {
        qWarning() << "Unable to discard stale Aerokontiki mission at startup:" << staleMissionPath;
    }
}

QString AerokontikiMissionStore::_storagePath(void) const
{
    const QString storageDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(storageDirectory).filePath(QStringLiteral("aerokontiki-mission.json"));
}

bool AerokontikiMissionStore::saveMission(const QJsonObject& mission, QString& errorString)
{
    if (mission.isEmpty()) {
        errorString = tr("The Aerokontiki mission is empty.");
        return false;
    }

    const QByteArray missionBytes = QJsonDocument(mission).toJson(QJsonDocument::Compact);
    QJsonObject wrapper;
    wrapper[kVersionKey] = kStorageVersion;
    wrapper[kMissionKey] = mission;
    wrapper[kChecksumKey] = QString::fromLatin1(QCryptographicHash::hash(missionBytes, QCryptographicHash::Sha256).toHex());

    const QFileInfo storageInfo(_storagePath());
    if (!QDir().mkpath(storageInfo.absolutePath())) {
        errorString = tr("Unable to create the Aerokontiki mission storage directory.");
        return false;
    }

    QSaveFile file(storageInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        errorString = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(wrapper).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        errorString = file.errorString();
        return false;
    }

    if (_mission != mission) {
        _mission = mission;
        emit missionChanged();
    }
    return true;
}

bool AerokontikiMissionStore::clearMission(QString& errorString)
{
    const QString path = _storagePath();
    if (QFile::exists(path) && !QFile::remove(path)) {
        errorString = tr("Unable to remove the prepared Aerokontiki mission: %1").arg(path);
        return false;
    }

    if (!_mission.isEmpty()) {
        _mission = QJsonObject();
        emit missionChanged();
    }
    return true;
}
