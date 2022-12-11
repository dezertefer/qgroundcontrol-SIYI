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

    QML_ELEMENT

public:
    explicit BackEnd(QObject *parent = nullptr);

    QString userName();

    QGeoCoordinate A();
    QGeoCoordinate B();
    QGeoCoordinate C();

    double angle();

    QGeoCoordinate calculateC(QGeoCoordinate &A, QGeoCoordinate &B);



    void setUserName(const QString &userName);



    void setAngle(double &newAngle);

    void setA(const QGeoCoordinate &newA);

    void setB(const QGeoCoordinate &newB);

    void setC(const QGeoCoordinate &newC);

signals:
    void userNameChanged();


    void angleChanged();

    void AChanged();

    void BChanged();

    void CChanged();

private:
    QString m_userName;
    QGeoCoordinate m_A;
    QGeoCoordinate m_B;
    QGeoCoordinate m_C;
    double m_angle;

    qreal m_direction;
};

#endif // BACKEND_H
