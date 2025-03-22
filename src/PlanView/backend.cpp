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
    readDropPoints();
}

BackEnd::BackEnd(QString path)
{
    //
}

QGeoCoordinate BackEnd::calculateC(QGeoCoordinate &A, QGeoCoordinate &B)
{
    // QGeoCoordinate C;
    // double direction = A.azimuthTo(B);
    // double Balt = qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->rawValue().toDouble()-2.0;//B.altitude();
    // double angle = qDegreesToRadians(qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAngle()->rawValue().toDouble());
    // double distance = Balt/qTan(angle);
    // double horizontalDistance = A.distanceTo(B);
    // double altitudeDifference = B.altitude() - A.altitude() + 2.0;
    // double distance3D = sqrt(pow(horizontalDistance, 2) + pow(altitudeDifference, 2));
    // Balt += 2.0;
    // if(distance3D < qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileCableLength()->rawValue().toDouble()){
    //     C = A.atDistanceAndAzimuth(distance,direction,Balt);
    //     m_C=C;
    //     m_direction = distance;
    //     m_distance3D = distance3D;
    //     qgcApp()->toolbox()->settingsManager()->appSettings()->defaultMissionItemAltitude()->setRawValue(Balt);
    // }else{

    // }

    // //qgcApp()->toolbox()->settingsManager()->geoserverSettings()->visualEPSGNumber()->
    // return C;
    // //CreateJson();

    // Get key settings and points
    double cableLength = qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileCableLength()->rawValue().toDouble();
    double takeoffHoverAltitude = 2.0;  // F is A with altitude A.altitude() + 2m
    //double Balt = qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->rawValue().toDouble()-2.0;

    // Compute initial desired point C using your initial angle logic.
    double initialAngle = qDegreesToRadians(qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAngle()->rawValue().toDouble());
    double initialAltitudeForC = qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileAlt()->rawValue().toDouble();
    double horizontalDistanceForC = (initialAltitudeForC - takeoffHoverAltitude) / qTan(initialAngle);
    qDebug()<<"HORIZONTAL DISTANCE FOR C:"<< horizontalDistanceForC;
    double direction = A.azimuthTo(B);
    QGeoCoordinate initialC = A.atDistanceAndAzimuth(horizontalDistanceForC, direction, initialAltitudeForC);

    // Compute 3D distance from A to initialC.
    double verticalDiff = initialC.altitude() - A.altitude() + 2;
    double currentDistance3D = sqrt(pow(horizontalDistanceForC, 2) + pow(verticalDiff, 2));
    QGeoCoordinate finalPoint;

    qDebug()<<"CABLE:"<< cableLength;
    qDebug()<<"3D DISTANCE:"<< currentDistance3D;
    if(currentDistance3D < cableLength){
            //double desiredVerticalDiff = B.altitude() - A.altitude()+2;
            // Ensure the cable can cover the vertical difference.
            // Calculate required horizontal distance so that the 3D distance equals the cable length.
            double requiredHorizontalDistance = sqrt(cableLength * cableLength - (verticalDiff) * (verticalDiff));
            qDebug()<<"requiredHorizontalDistance:"<< requiredHorizontalDistance;
            finalPoint = A.atDistanceAndAzimuth(requiredHorizontalDistance, direction, initialAltitudeForC);
            qDebug()<<"C LATITUDE:"<<initialC.latitude();
            qDebug()<<"D LATITUDE:"<<finalPoint.latitude();
            m_D = finalPoint;
            m_C = finalPoint;
            // In this case, finalPoint becomes both C and D.
    }else{
            double scaleFactor = cableLength / currentDistance3D;
            double adjustedHorizontalDistance = horizontalDistanceForC * scaleFactor;
            double adjustedAltitude = verticalDiff * scaleFactor; // this altitude will be lower than B's altitude.
            finalPoint = A.atDistanceAndAzimuth(adjustedHorizontalDistance, direction, adjustedAltitude);
            qDebug()<<"LATITUDE:"<<finalPoint.latitude();
            m_D = finalPoint;
            m_C = initialC;
    }

    // Check if point B is too close (e.g., perhaps the horizontal distance A->B is less than some minimum)
    //double minRequiredDistance = cableLength;  // define based on your application


    // Determine the final point (for both C and D)

    // if (cableLength < currentDistance3D) {
    //     // CASE 1: Cable is shorter than initial route A->C.
    //     // Compute the reachable point along the same vector that exactly matches the cable length.
    //     double scaleFactor = cableLength / currentDistance3D;
    //     double adjustedHorizontalDistance = horizontalDistanceForC * scaleFactor;
    //     double adjustedAltitude = A.altitude() + verticalDiff * scaleFactor; // this altitude will be lower than B's altitude.
    //     finalPoint = A.atDistanceAndAzimuth(adjustedHorizontalDistance, direction, adjustedAltitude);
    //     qDebug()<<"LATITUDE:"<<finalPoint.latitude();
    //     m_D = finalPoint;
    // } else {
    //     // CASE 2: Cable is longer than initial A->C route.
    //     // Sacrifice the initial angle and adjust so that final altitude equals B's altitude.

    //     double desiredVerticalDiff = B.altitude() - A.altitude()+2;
    //     // Ensure the cable can cover the vertical difference.
    //     // Calculate required horizontal distance so that the 3D distance equals the cable length.
    //     double requiredHorizontalDistance = sqrt(cableLength * cableLength - (desiredVerticalDiff) * (desiredVerticalDiff));
    //     finalPoint = A.atDistanceAndAzimuth(requiredHorizontalDistance, direction, Balt);
    //     qDebug()<<"LATITUDE:"<<finalPoint.latitude();
    //     m_D = finalPoint;
    //     // In this case, finalPoint becomes both C and D.
    // }
    //finalPoint = initialC;

    // Save finalPoint as C and D.
    //m_C = initialC;
    m_direction = finalPoint.distanceTo(A);  // horizontal distance traveled
    m_distance3D = cableLength;                // exactly the cable length used
    qgcApp()->toolbox()->settingsManager()->appSettings()->defaultMissionItemAltitude()->setRawValue(m_C.altitude());

    // finalPoint is used for point D (and possibly C, if they match).
    return m_C;
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
    //qgcApp()->toolbox()->settingsManager()->flightMapSettings()->mapProvider()->
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

QGeoCoordinate BackEnd::D()
{
    return m_D;
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

void BackEnd::setD(const QGeoCoordinate &newD)
{
    if (m_D == newD)
        return;
    m_D = newD;
    emit DChanged();
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
    qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileCableLength()->setRawValue(m_selectedProfile["cableLength"].toDouble());
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
        defaultProfile.insert("cableLength", qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileCableLength()->rawValue().toString());

        profilesList.insert(qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileName()->rawDefaultValue().toString(),defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Rapid Launcher");
        defaultProfile.insert("angle","45");
        defaultProfile.insert("alt","50");
        defaultProfile.insert("speed","6.9");
        defaultProfile.insert("takeOffSpeed","1.11");
        defaultProfile.insert("cableLength", "70.0");

        profilesList.insert("Rapid Launcher", defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Winch");
        defaultProfile.insert("angle","45");
        defaultProfile.insert("alt","60");
        defaultProfile.insert("speed","5.55");
        defaultProfile.insert("takeOffSpeed","1.11");
        defaultProfile.insert("cableLength", "70.0");

        profilesList.insert("Winch", defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Direct off sand");
        defaultProfile.insert("angle","30");
        defaultProfile.insert("alt","60");
        defaultProfile.insert("speed","6.9");
        defaultProfile.insert("takeOffSpeed","1.67");
        defaultProfile.insert("cableLength", "70.0");

        profilesList.insert("Direct off sand", defaultProfile);

        defaultProfile = QJsonObject();

        defaultProfile.insert("name","Winch clipping traces");
        defaultProfile.insert("angle","45");
        defaultProfile.insert("alt","60");
        defaultProfile.insert("speed","5.55");
        defaultProfile.insert("takeOffSpeed","0.83");
        defaultProfile.insert("cableLength", "70.0");

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


void BackEnd::readDropPoints()
{
    QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/DropPoints.json";
    QFile file(file_path);
    if (file.open(QIODevice::ReadOnly))        {
        QByteArray data = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            QJsonArray dropPointsArray = doc.array();
            QVariantList dropPointList;
            for (const QJsonValue &value : dropPointsArray) {
                if (value.isObject()) {
                    QJsonObject dropPointObj = value.toObject();
                    QVariantMap dropPointMap;
                    dropPointMap["date"] = dropPointObj["date"].toString();
                    dropPointMap["rating"] = dropPointObj["rating"].toInt();
                    dropPointMap["lat"] = dropPointObj["lat"].toDouble();
                    dropPointMap["lon"] = dropPointObj["lon"].toDouble();
                    dropPointMap["label"] = dropPointObj["label"].toString();
                    dropPointMap["counter"] = dropPointObj["counter"].toInt();
                    // Display the drop point in the console
                    qDebug() << "Drop Point:";
                    qDebug() << "Date:" << dropPointMap["date"].toString();
                    qDebug() << "Rating:" << dropPointMap["rating"].toInt();
                    qDebug() << "Latitude:" << dropPointMap["lat"].toDouble();
                    qDebug() << "Longitude:" << dropPointMap["lon"].toDouble();
                    qDebug() << "Label:" << dropPointMap["label"].toString();
                    qDebug() << "Counter:" << dropPointMap["counter"].toInt();
                    qDebug() << "-------------------------";

                    dropPointList.append(dropPointMap);
                }
            }
            m_dropPoints = dropPointList;
            emit dropPointsChanged();
        }
        file.close();        }
    else {
        qDebug() << "Failed to open the file!";
    }

}

QVariantList BackEnd::dropPoints(){
    return m_dropPoints;
}

void BackEnd::setDropPoints(const QVariantList &dropPoints){
    if(m_dropPoints!=dropPoints){
        m_dropPoints = dropPoints;
        emit dropPointsChanged();
    }
}

void BackEnd::addDropPoint(const QString &label, double lat, double lon) {
    const double maxDistance = 50.0;  // 50 meters
    QGeoCoordinate newDropPointCoord(lat, lon);

    // Check if the new drop point is within 50 meters of any existing drop point
    for (int i = 0; i < m_dropPoints.size(); ++i) {
        QVariantMap pointMap = m_dropPoints[i].toMap();
        double existingLat = pointMap["lat"].toDouble();
        double existingLon = pointMap["lon"].toDouble();
        QGeoCoordinate existingDropPointCoord(existingLat, existingLon);

        double distance = newDropPointCoord.distanceTo(existingDropPointCoord);

        if (distance <= maxDistance) {
            // If within 50 meters, increment the counter of the existing drop point
            int counter = pointMap["counter"].toInt();
            pointMap["counter"] = counter + 1;
            m_dropPoints[i] = pointMap;  // Update the drop point in the list
            emit dropPointsChanged();
            saveToFile();
            return;  // No need to add a new drop point, exit early
        }
    }


    // If no close drop point was found, add the new one
    QVariantMap newDropPoint;
    newDropPoint["date"] = QDateTime::currentDateTime().toString();
    newDropPoint["rating"] = 0;  // default rating
    newDropPoint["lat"] = lat;
    newDropPoint["lon"] = lon;
    newDropPoint["label"] = label;
    newDropPoint["counter"] = 1;  // New point, so counter starts at 1

    m_dropPoints.append(newDropPoint);
    emit dropPointsChanged();
    saveToFile();
}

void BackEnd::removeDropPoint(int index) {
    if (index >= 0 && index < m_dropPoints.size()) {
        m_dropPoints.removeAt(index);
        emit dropPointsChanged();
        saveToFile();
    }
}

void BackEnd::changeLabel(int index, const QString &newLabel) {
    if (index >= 0 && index < m_dropPoints.size()) {
        m_dropPoints[index].toMap()["label"] = newLabel;
        emit dropPointsChanged();
        saveToFile();
    }
}

void BackEnd::increaseCounter(int index) {
    if (index >= 0 && index < m_dropPoints.size()) {
        int counter = m_dropPoints[index].toMap()["counter"].toInt();
        m_dropPoints[index].toMap()["counter"] = counter + 1;
        emit dropPointsChanged();
        saveToFile();
    }
}

void BackEnd::changeRating(int index, int newRating) {
    if (index >= 0 && index < m_dropPoints.size()) {
        qDebug() << newRating;
        m_dropPoints[index].toMap()["rating"] = newRating;
        emit dropPointsChanged();
        saveToFile();
    }
}

void BackEnd::saveToFile() {
    QString file_path = qgcApp()->toolbox()->settingsManager()->appSettings()->profileDirectorySavePath() + "/DropPoints.json";
    QFile file(file_path);

    if (file.open(QIODevice::WriteOnly)) {
        QJsonArray dropPointsArray;
        for (const QVariant &point : m_dropPoints) {
            QVariantMap pointMap = point.toMap();
            QJsonObject dropPointObj;
            dropPointObj["date"] = pointMap["date"].toString();
            dropPointObj["rating"] = pointMap["rating"].toInt();
            dropPointObj["lat"] = pointMap["lat"].toDouble();
            dropPointObj["lon"] = pointMap["lon"].toDouble();
            dropPointObj["label"] = pointMap["label"].toString();
            dropPointObj["counter"] = pointMap["counter"].toInt();
            dropPointsArray.append(dropPointObj);
        }

        QJsonDocument doc(dropPointsArray);
        file.write(doc.toJson());
        file.close();
    } else {
        qDebug() << "Failed to open file for saving!";
    }
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
   currentProfile.insert("cableLength", qgcApp()->toolbox()->settingsManager()->planViewSettings()->currentProfileCableLength()->rawValue().toString());

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
    newProfile.insert("cableLength", qgcApp()->toolbox()->settingsManager()->planViewSettings()->newProfileCableLength()->rawValue().toString());

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
