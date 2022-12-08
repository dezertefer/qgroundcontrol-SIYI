#include "backend.h"
#include <QGeoCoordinate>

BackEnd::BackEnd(QObject *parent) :
    QObject(parent)
{

}

QGeoCoordinate BackEnd::calculateC(QGeoCoordinate &A, QGeoCoordinate &B)
{
    A.setLatitude(12);
    A.setLongitude(10);
    B.setLatitude(30);
    B.setLongitude(40);
    QGeoCoordinate C;
    qreal direction = A.distanceTo(B);
    m_direction = direction;
    return C;
}

QString BackEnd::userName()
{
    m_userName = QString::number(m_direction);
    return m_userName;
}

QString BackEnd::ALat()
{
    return m_ALat;
}

QString BackEnd::BLat()
{
    return m_BLat;
}
QString BackEnd::CLat()
{
    return m_CLat;
}


QString BackEnd::ALon()
{
    return m_ALon;
}

QString BackEnd::BLon()
{
    return m_BLon;
}


QString BackEnd::CLon()
{
    return m_CLon;
}


QString BackEnd::BAlt()
{
    return m_BAlt;
}

QString BackEnd::CAlt()
{
    return m_CAlt;
}


void BackEnd::setALat(const QString &ALat)
{
    if (ALat == m_ALat)
        return;

    m_ALat = ALat;
    double lat = m_ALat.toDouble();
    m_A.setLatitude(lat);

    emit ALatChanged();
}

void BackEnd::setALon(const QString &ALon)
{
    if (ALon == m_ALon)
        return;

    m_ALon = ALon;
    double lon = m_ALon.toDouble();
    m_A.setLongitude(lon);

    emit ALonChanged();
}


void BackEnd::setBLat(const QString &BLat)
{
    if (BLat == m_BLat)
        return;

    m_BLat = BLat;
    emit ALatChanged();
}

void BackEnd::setUserName(const QString &userName)
{
    if (userName == m_userName)
        return;

    m_userName = userName;
    emit userNameChanged();
}



void BackEnd::setAngle(int newAngle)
{
    if (m_angle == newAngle)
        return;
    m_angle = newAngle;
    emit angleChanged();
}
