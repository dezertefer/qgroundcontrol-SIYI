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

    QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/Profiles.json";
    qDebug()<<file_path;

    QFile file_obj(file_path);
    if(!file_obj.open(QIODevice::ReadOnly)){
        qDebug()<<"Failed to open "<<file_path;
        exit(1);
    }

    QTextStream file_text(&file_obj);
    QString json_string;
    json_string = file_text.readAll();
    file_obj.close();
    QByteArray json_bytes = json_string.toLocal8Bit();

    auto json_doc=QJsonDocument::fromJson(json_bytes);

    if(json_doc.isNull()){
        qDebug()<<"Failed to create JSON doc.";
        exit(2);
    }
    if(!json_doc.isObject()){
        qDebug()<<"JSON is not an object.";
        exit(3);
    }

    QJsonObject json_obj=json_doc.object();


    if(json_obj.isEmpty()){
        qDebug()<<"JSON object is empty.";
        exit(4);
    }


    QJsonObject root_obj = json_doc.object();
    QVariantMap root_map = root_obj.toVariantMap();
    m_root_map = root_map;


    for(QVariantMap::const_iterator iter = root_map.begin(); iter != root_map.end(); ++iter)
    {
      //qDebug() << iter.key() ;
      m_profileList.append(iter.key());
    }
}

BackEnd::BackEnd(QString path)
{
    //
}

QGeoCoordinate BackEnd::calculateC(QGeoCoordinate &A, QGeoCoordinate &B)
{
    QGeoCoordinate C;
    double direction = A.azimuthTo(B);
    double Balt = qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->rawValue().toDouble()-2.0;//B.altitude();
    double angle = qDegreesToRadians(qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAngle()->rawValue().toDouble());
    double distance = Balt/qTan(angle);
    Balt += 2.0;
    C = A.atDistanceAndAzimuth(distance,direction,Balt);
    m_C=C;
    m_direction = distance;
    qgcApp()->toolbox()->settingsManager()->appSettings()->defaultMissionItemAltitude()->setRawValue(Balt);
    return C;
    //CreateJson();
}

void BackEnd::CreateJson()
{
    //QString path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/test.json";
    //qDebug()<<path;
    /*QFile file_obj(path);
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
    qDebug()<< json_map["test"].toString();*/
    // list.append("one");
     //QString lol = list[0];
   //  setUserName(list[0]);

}


QString BackEnd::userName()
{

    return m_userName;
}

double BackEnd::angle()
{
    BackEnd::calculateC(m_A, m_B);
    //qgcApp()->toolbox()->settingsManager()->appSettings()->defaultMissionItemAltitude()->setRawValue(10);
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

QVariantMap BackEnd::profiles()
{
    return m_root_map;
}

QVariantMap BackEnd::profile()

{

    QVariantMap profile_map = m_root_map;
    return profile_map;

}

void BackEnd::setProfile(const QVariantMap &newProfile)
{
    m_root_map = newProfile;
}


QStringList BackEnd::profileList()
{
    return m_profileList;
}


void BackEnd::setCurrentProfile(const QString &profile)
{
    QVariantMap profile_map = m_root_map[profile].toMap();
    m_selectedProfile = profile_map;
    updateCurrentProfile(profile);
    //qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->setRawValue(profile);

    emit profileListChanged();
}

void BackEnd::updateCurrentProfile (QString profile)
{
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->setRawValue(m_selectedProfile["name"]);
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->setRawValue(m_selectedProfile["alt"].toDouble());
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileSpeed()->setRawValue(m_selectedProfile["speed"].toDouble());
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAngle()->setRawValue(m_selectedProfile["angle"].toDouble());
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileWhinch()->setRawValue(m_selectedProfile["whinch"].toString());
}
