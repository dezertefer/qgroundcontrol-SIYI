/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QJsonObject>
#include <QObject>

class AerokontikiMissionStore : public QObject
{
    Q_OBJECT

public:
    static AerokontikiMissionStore* instance(void);

    bool        hasMission     (void) const { return !_mission.isEmpty(); }
    QJsonObject mission        (void) const { return _mission; }
    bool        saveMission    (const QJsonObject& mission, QString& errorString);
    bool        clearMission   (QString& errorString);

signals:
    void missionChanged(void);

private:
    explicit AerokontikiMissionStore(QObject* parent = nullptr);

    QString _storagePath(void) const;

    QJsonObject _mission;
};
