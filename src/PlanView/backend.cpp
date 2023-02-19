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
    readJson();

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
    //int x = qgcApp()->toolbox()->
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


QStringList BackEnd::profileList()
{
    m_profileList.clear();
    for(QVariantMap::const_iterator iter = m_root_map.begin(); iter != m_root_map.end(); ++iter)
    {
      //qDebug() << iter.key() ;
      m_profileList.append(iter.key());
    }
    return m_profileList;
    emit profileListChanged();
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
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileTakeOffSpeed()->setRawValue(m_selectedProfile["takeOffSpeed"].toString());
}

void BackEnd::readJson ()
{
    QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/Profiles.json";
    qDebug()<<file_path;

    QFile file_obj(file_path);
    if(!file_obj.open(QIODevice::ReadOnly)){
        qDebug()<<"Failed to open "<<file_path;

        QJsonObject profilesList;
        QJsonObject defaultProfile;

        defaultProfile.insert("name",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->rawDefaultValue().toString());
        defaultProfile.insert("angle",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAngle()->rawDefaultValue().toString());
        defaultProfile.insert("alt",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->rawDefaultValue().toString());
        defaultProfile.insert("speed",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileSpeed()->rawDefaultValue().toString());
        defaultProfile.insert("takeOffSpeed",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileTakeOffSpeed()->rawDefaultValue().toString());

        profilesList.insert(qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->rawDefaultValue().toString(),defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Rapid Launcher");
        defaultProfile.insert("angle","45");
        defaultProfile.insert("alt","50");
        defaultProfile.insert("speed","6.9");
        defaultProfile.insert("takeOffSpeed","1.11");

        profilesList.insert("Rapid Launcher", defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Winch");
        defaultProfile.insert("angle","45");
        defaultProfile.insert("alt","60");
        defaultProfile.insert("speed","5.55");
        defaultProfile.insert("takeOffSpeed","1.11");

        profilesList.insert("Winch", defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Direct off sand");
        defaultProfile.insert("angle","30");
        defaultProfile.insert("alt","60");
        defaultProfile.insert("speed","6.9");
        defaultProfile.insert("takeOffSpeed","1.67");

        profilesList.insert("Direct off sand", defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Winch clipping traces");
        defaultProfile.insert("angle","45");
        defaultProfile.insert("alt","60");
        defaultProfile.insert("speed","5.55");
        defaultProfile.insert("takeOffSpeed","0.83");

        profilesList.insert("Winch clipping traces", defaultProfile);



        QJsonDocument doc (profilesList);
        QFile profiles(file_path);
        profiles.open(QIODevice::WriteOnly);
        profiles.write(doc.toJson());
        profiles.close();
    }
    file_obj.close();
    QFile file_obj1(file_path);
    if(!file_obj1.open(QIODevice::ReadOnly))
    {
        qDebug()<<"Failed to open "<<file_path;
        exit(505);
    }

    QTextStream file_text(&file_obj1);
    QString json_string;
    json_string = file_text.readAll();
    file_obj1.close();
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






}

void BackEnd::editProfile (const QString &profile)
{

   QString initialName = qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->rawValue().toString();
   QVariantMap swap;

   QJsonObject profilesList;
   QJsonObject currentProfile;

   for(QVariantMap::const_iterator iter = m_root_map.begin(); iter != m_root_map.end(); ++iter)
    {
       if (iter.key() != initialName)
       {
           swap.insert(iter.key(),iter.value());
       }
    }

   profilesList = QJsonObject::fromVariantMap(swap);


   currentProfile.insert("name", profile);
   currentProfile.insert("angle",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAngle()->rawValue().toString());
   currentProfile.insert("alt",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->rawValue().toString());
   currentProfile.insert("speed",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileSpeed()->rawValue().toString());
   currentProfile.insert("takeOffSpeed",qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileTakeOffSpeed()->rawValue().toString());

   profilesList.insert(profile,currentProfile);

   m_root_map = profilesList.toVariantMap();
   qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->setRawValue(profile);


   //qDebug()<< profilesList;
    writeJson(m_root_map);

   /*QJsonDocument doc = QJsonDocument::fromVariant(m_root_map);
   QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/Profiles.json";

   QFile profiles(file_path);
   profiles.open(QIODevice::WriteOnly);
   profiles.write(doc.toJson());
   profiles.close();
*/
}

void BackEnd::setNewProfile(const QString &profile)
{
    //QJsonObject profilesList;
    QJsonObject newProfile;

    newProfile.insert("name",qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileName()->rawValue().toString());
    newProfile.insert("angle",qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileAngle()->rawValue().toString());
    newProfile.insert("alt",qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileAlt()->rawValue().toString());
    newProfile.insert("speed",qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileSpeed()->rawValue().toString());
    newProfile.insert("takeOffSpeed",qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileTakeOffSpeed()->rawValue().toString());

    //profilesList.insert(qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileName()->rawValue().toString(),newProfile);
    m_root_map.insert(qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileName()->rawValue().toString(), newProfile);

    writeJson(m_root_map);
    setCurrentProfile(qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileName()->rawValue().toString());
    /*
    QJsonDocument doc = QJsonDocument::fromVariant(m_root_map);
    QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/Profiles.json";

    QFile profiles(file_path);
    profiles.open(QIODevice::WriteOnly);
    profiles.write(doc.toJson());
    profiles.close();*/

}

void BackEnd::deleteProfile(const QString &profile)
{
    QVariantMap swap;

    for(QVariantMap::const_iterator iter = m_root_map.begin(); iter != m_root_map.end(); ++iter)
     {
        if (iter.key() != profile)
        {
            swap.insert(iter.key(),iter.value());
        }
     }

    m_root_map = swap;
    setCurrentProfile(m_root_map.begin().key());
    writeJson(m_root_map);

}

bool BackEnd::dropPointSelected ()
{

    return m_dropPointSelected;
}

void BackEnd::setDropPointSelected (bool &dropPoint)
{
    if (dropPoint == m_dropPointSelected)
        return;

    m_dropPointSelected = dropPoint;

    emit dropPointSelectedChanged();
}

void BackEnd::writeJson(QVariantMap map)
{
    QJsonDocument doc = QJsonDocument::fromVariant(map);
    QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/Profiles.json";

    QFile profiles(file_path);
    profiles.open(QIODevice::WriteOnly);
    profiles.write(doc.toJson());
    profiles.close();
}
