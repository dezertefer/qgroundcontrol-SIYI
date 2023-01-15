#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QString>
#include <qqml.h>
#include <QGeoCoordinate>
#include <QtMath>
#include "SettingsManager.h"

class BackEnd : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)
    Q_PROPERTY(QGeoCoordinate A READ A WRITE setA NOTIFY AChanged)
    Q_PROPERTY(QGeoCoordinate B READ B WRITE setB NOTIFY BChanged)
    Q_PROPERTY(QGeoCoordinate C READ C WRITE setC NOTIFY CChanged)
    Q_PROPERTY(double angle READ angle WRITE setAngle NOTIFY angleChanged)
    Q_PROPERTY(QString currentProfile  WRITE setCurrentProfile NOTIFY currentProfileChanged)
    Q_PROPERTY(QVariantMap profiles READ profiles  NOTIFY profilesChanged)
    Q_PROPERTY(QStringList profileList READ profileList NOTIFY profileListChanged)
    Q_PROPERTY(QString editProfile WRITE editProfile NOTIFY editProfileChanged)

    QML_ELEMENT

public:
    explicit BackEnd(QObject *parent = nullptr);
    BackEnd(QString path);

    QString userName();
    void CreateJson();
    QGeoCoordinate A();
    QGeoCoordinate B();
    QGeoCoordinate C();

    void editProfile(const QString &profile);

    void readJson();
    void writeJson();

    void updateCurrentProfile(QString profile);

    QStringList profileList();

    double angle();

    QGeoCoordinate calculateC(QGeoCoordinate &A, QGeoCoordinate &B);



    void setUserName(const QString &userName);
    void setCurrentProfile(const QString &profile);



    void setAngle(double &newAngle);

    void setA(const QGeoCoordinate &newA);

    void setB(const QGeoCoordinate &newB);

    void setC(const QGeoCoordinate &newC);

    QVariantMap profiles();


signals:
    void userNameChanged();


    void angleChanged();

    void AChanged();

    void BChanged();

    void CChanged();

    void editProfileChanged();

    void profilesChanged();

    void profileListChanged();

private:
    QString m_userName;
    QGeoCoordinate m_A;
    QGeoCoordinate m_B;
    QGeoCoordinate m_C;
    QList<QString> list;
    double m_angle;

    qreal m_direction;

    QVariantMap m_root_map;
    QVariantMap m_selectedProfile;

    QStringList m_profileList;
};

#endif // BACKEND_H


