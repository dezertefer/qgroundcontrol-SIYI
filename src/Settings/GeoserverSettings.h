#pragma once

#include "SettingsGroup.h"

class GeoserverSettings : public SettingsGroup
{
    Q_OBJECT
public:
    GeoserverSettings(QObject *parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()
    DEFINE_SETTINGFACT(visualWMTSVersion)
    DEFINE_SETTINGFACT(visualImageFormat)
    DEFINE_SETTINGFACT(visualMapWorkspace)
    DEFINE_SETTINGFACT(visualMapName)
    DEFINE_SETTINGFACT(visualEPSGNumber)

    DEFINE_SETTINGFACT(elevationWMTSVersion)
    DEFINE_SETTINGFACT(elevationMapWorkspace)
    DEFINE_SETTINGFACT(elevationMapName)
    DEFINE_SETTINGFACT(elevationEPSGNumber)
};

