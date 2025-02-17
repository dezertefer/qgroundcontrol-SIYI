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
    Q_PROPERTY(QString newProfile WRITE setNewProfile NOTIFY newProfileChanged)
    Q_PROPERTY(QString deleteProfile WRITE deleteProfile NOTIFY deleteProfileChanged)
    Q_PROPERTY(QVariantMap profiles READ profiles  NOTIFY profilesChanged)
    Q_PROPERTY(QStringList profileList READ profileList NOTIFY profileListChanged)
    Q_PROPERTY(QString editProfile WRITE editProfile NOTIFY editProfileChanged)
    Q_PROPERTY(bool dropPointSelected READ dropPointSelected WRITE setDropPointSelected NOTIFY dropPointSelectedChanged)
    Q_PROPERTY(QVariantList dropPoints READ dropPoints WRITE setDropPoints NOTIFY dropPointsChanged)

    QML_ELEMENT

public:
    explicit BackEnd(QObject *parent = nullptr);
    BackEnd(QString path);
    bool dropPointSelected();
    void setDropPointSelected(bool &dropPoint);

    Q_INVOKABLE void addDropPoint(const QString &label, double lat, double lon);
    Q_INVOKABLE void removeDropPoint(int index);
    Q_INVOKABLE void changeLabel(int index, const QString &newLabel);
    Q_INVOKABLE void increaseCounter(int index);
    Q_INVOKABLE void changeRating(int index, int newRating);

    void saveToFile();
    QString userName();
    void CreateJson();
    QGeoCoordinate A();
    QGeoCoordinate B();
    QGeoCoordinate C();

    void editProfile(const QString &profile);
    void setNewProfile(const QString &profile);
    void deleteProfile(const QString &profile);

    void readJson();
    void readDropPoints();
    void writeJson(QVariantMap map);

    void updateCurrentProfile(QString profile);

    QStringList profileList();

    double angle();

    QGeoCoordinate calculateC(QGeoCoordinate &A, QGeoCoordinate &B);



    void setUserName(const QString &userName);
    void setCurrentProfile(const QString &profile);

    void setDropPoints(const QVariantList &dropPoints);

    void setAngle(double &newAngle);

    void setA(const QGeoCoordinate &newA);

    void setB(const QGeoCoordinate &newB);

    void setC(const QGeoCoordinate &newC);

    QVariantMap profiles();

    QVariantList dropPoints();


signals:
    void userNameChanged();


    void angleChanged();

    void AChanged();

    void BChanged();

    void CChanged();

    void editProfileChanged();

    void profilesChanged();

    void profileListChanged();

    void dropPointSelectedChanged();

    void dropPointsChanged();

private:
    QString m_userName;
    QGeoCoordinate m_A;
    QGeoCoordinate m_B;
    QGeoCoordinate m_C;
    QList<QString> list;
    double m_angle;

    bool m_dropPointSelected = false;

    qreal m_direction;

    QVariantMap m_root_map;
    QVariantMap m_selectedProfile;
    QVariantList m_dropPoints;

    QStringList m_profileList;
};

#endif // BACKEND_H


