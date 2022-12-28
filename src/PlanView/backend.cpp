#include "backend.h"
#include <QGeoCoordinate>
#include <QtMath>
#include "QGCApplication.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include <QtXml>
#include <QtCore>
#include <iostream>
#include <QIODevice>

BackEnd::BackEnd(QObject *parent) :
    QObject(parent)
{

}








void CreateJson()
{
    QString path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/test.json";
    QFile file_obj(path);
    if(!file_obj.open(QIODevice::ReadOnly)){
        qDebug()<<"Failed to open "<<path;
       // exit(1);
    }

    QTextStream file_text(&file_obj);
    QString json_string;
    json_string = file_text.readAll();
    file_obj.close();
    QByteArray json_bytes = json_string.toLocal8Bit();

    auto json_doc=QJsonDocument::fromJson(json_bytes);

    if(json_doc.isNull()){
        qDebug()<<"Failed to create JSON doc.";
        //exit(2);
    }
    if(!json_doc.isObject()){
        qDebug()<<"JSON is not an object.";
        //exit(3);
    }

    QJsonObject json_obj=json_doc.object();

    if(json_obj.isEmpty()){
        qDebug()<<"JSON object is empty.";
        //exit(4);
    }

    QVariantMap json_map = json_obj.toVariantMap();
    qDebug()<< json_map["altitude"].toString();
    qDebug()<< json_map["test"].toString();
}



QGeoCoordinate BackEnd::calculateC(QGeoCoordinate &A, QGeoCoordinate &B)
{
   // CreateJson();
    QGeoCoordinate C;
    double direction = A.azimuthTo(B);
    double Balt = 60;//B.altitude();
    double angle = qDegreesToRadians(45.0);
    double distance = Balt/qTan(angle);
    C = A.atDistanceAndAzimuth(distance,direction,Balt);
    m_C=C;
    m_direction = distance;
    return C;
}



QString BackEnd::userName()
{
    m_userName = QString::number(m_direction);
    return m_userName;
}



double BackEnd::angle()
{
    BackEnd::calculateC(m_A, m_B);
    qgcApp()->toolbox()->settingsManager()->appSettings()->defaultMissionItemAltitude()->setRawValue(10);
    return m_direction;
}

QGeoCoordinate BackEnd::A()
{
    return m_A;
}

QGeoCoordinate BackEnd::B()
{
    return m_B;
}

QGeoCoordinate BackEnd::C()
{
    return m_C;
}

void BackEnd::setUserName(const QString &userName)
{
    if (userName == m_userName)
        return;

    m_userName = userName;
    emit userNameChanged();
}

void BackEnd::setAngle(double &newAngle)
{
    if (m_angle == newAngle)
        return;
    m_angle = newAngle;
    emit angleChanged();
}

void BackEnd::setA(const QGeoCoordinate &newA)
{
    if (m_A == newA)
        return;
    m_A = newA;
    emit AChanged();
}

void BackEnd::setB(const QGeoCoordinate &newB)
{
    if (m_B == newB)
        return;
    m_B = newB;
    emit BChanged();
}

void BackEnd::setC(const QGeoCoordinate &newC)
{
    if (m_C == newC)
        return;
    m_C = newC;
    emit CChanged();
}
