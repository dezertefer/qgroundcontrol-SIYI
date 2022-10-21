#pragma once

#include "MapProvider.h"
#include <QObject>
#include <QVector>

class ServerProvider : public MapProvider
{
    Q_OBJECT
public:
  public:
    ServerProvider(const QString& referrer,const QString& imageFormat, quint32 averageSize,
                      QGeoMapType::MapStyle mapType, QObject* parent = nullptr);
};

class GeoserverMapProvider : public ServerProvider
{
    Q_OBJECT
public:
    GeoserverMapProvider(QString geoserverURL, QObject *parent = nullptr);

    enum WMTSVersion{
        V1_0_0,
        V1_1_0,
        V1_1_1,
        V1_3_0
    };

    enum ImageFormat{
        png,
        jpeg
    };

    QString wmtsVersionString();
    QString geoserverURL() const;
    void setGeoserverURL(const QString &geoserverURL);
    QString getWorkspace() const;
    void setWorkspace(const QString &workspace);
    QString getMapName() const;
    void setMapName(const QString &mapName);
    WMTSVersion wmtsVersion() const;
    void setWmtsVersion(const WMTSVersion &wmsVersion);
    QString getImageFormat() const;
    void setImageFormat(const ImageFormat &imageFormat);
    unsigned long long getEPSGNumber() const;
    void setEPSGNumber(unsigned long long EPSGNumber);

    virtual QNetworkRequest getTileURL(const int x, const int y, const int zoom, QNetworkAccessManager* networkManager) override;

protected:
    QString _getURL(const int x, const int y, const int zoom, QNetworkAccessManager* networkManager) override;
    static const QString m_serviceType;
    static const QString m_requestType;
    QString m_geoserverURL;
    QString m_workspace;
    QString m_mapName;
    WMTSVersion m_wmtsVersion;
    unsigned long long m_EPSGNumber;
    QString m_imageFormat;
};


