#include "GeoserverSettings.h"
#include "QGCApplication.h"
#include "GeoserverSettings.h"
#include "QGCMapEngine.h"
#include "AppSettings.h"
#include "SettingsManager.h"

#include <QQmlEngine>
#include <QtQml>

DECLARE_SETTINGGROUP(Geoserver, "Geoserver")
{
    qmlRegisterUncreatableType<GeoserverSettings>("QGroundControl.SettingsManager", 1, 0, "GeoserverSettings", "Reference only");
}

DECLARE_SETTINGSFACT(GeoserverSettings, visualWMTSVersion)
DECLARE_SETTINGSFACT(GeoserverSettings, visualImageFormat)
DECLARE_SETTINGSFACT(GeoserverSettings, visualMapWorkspace)
DECLARE_SETTINGSFACT(GeoserverSettings, visualMapName)
DECLARE_SETTINGSFACT(GeoserverSettings, visualEPSGNumber)

DECLARE_SETTINGSFACT(GeoserverSettings, elevationWMTSVersion)
DECLARE_SETTINGSFACT(GeoserverSettings, elevationMapWorkspace)
DECLARE_SETTINGSFACT(GeoserverSettings, elevationMapName)
DECLARE_SETTINGSFACT(GeoserverSettings, elevationEPSGNumber)

