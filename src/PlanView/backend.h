#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QString>
#include <qqml.h>
#include <QGeoCoordinate>

class BackEnd : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)
    Q_PROPERTY(QString ALat READ ALat WRITE setALat NOTIFY ALatChanged)
    Q_PROPERTY(QString BLat READ BLat WRITE setBLat NOTIFY BLatChanged)
    Q_PROPERTY(QString ALon READ ALon WRITE setALon NOTIFY ALonChanged)
    Q_PROPERTY(QString BLon READ BLon WRITE setBLon NOTIFY BLonChanged)
    Q_PROPERTY(QString BAlt READ BAlt WRITE setBAlt NOTIFY BAltChanged)
    Q_PROPERTY(int angle READ angle WRITE setAngle NOTIFY angleChanged)
    Q_PROPERTY(QString CLat READ CLat WRITE setCLat NOTIFY CLatChanged)
    Q_PROPERTY(QString CLon READ CLon WRITE setCLon NOTIFY CLonChanged)
    Q_PROPERTY(QString CAlt READ CAlt WRITE setCAlt NOTIFY CAltChanged)

    QML_ELEMENT

public:
    explicit BackEnd(QObject *parent = nullptr);

    QString userName();
    QString ALat();
    QString ALon();

    QString BLat();
    QString BLon();
    QString BAlt();

    QString CLat();
    QString CLon();
    QString CAlt();


    int angle();

    QGeoCoordinate calculateC(QGeoCoordinate &A, QGeoCoordinate &B);

    void setALat(const QString &ALat);
    void setALon(const QString &ALon);

    void setBLat(const QString &BLat);
    void setBLon(const QString &BLon);
    void setBAlt(const QString &BAlt);

    void setCLat(const QString &CLat);
    void setCLon(const QString &CLon);
    void setCAlt(const QString &CAlt);

    void setUserName(const QString &userName);



    void setAngle(int newAngle);

signals:
    void userNameChanged();
    void ALatChanged ();
    void BLatChanged ();
    void BLonChanged();
    void ALonChanged();

    void BAltChanged();

    void CLatChanged();

    void CLonChanged();

    void CAltChanged();

    void angleChanged();

private:
    QString m_userName;
    QString m_ALat;
    QString m_BLat;

    QGeoCoordinate m_A;
    QGeoCoordinate m_B;
    QGeoCoordinate m_C;

    QString m_BLon;
    QString m_ALon;
    QString m_BAlt;
    QString m_CLat;
    QString m_CLon;
    QString m_CAlt;
    int m_angle;

    qreal m_direction;
};

#endif // BACKEND_H
