#include "GeoServerMapProvider.h"
#include "QGCMapEngine.h"
#include <math.h>
#include <QDebug>
#include <QUrlQuery>

#include "SettingsManager.h"
#include "GeoserverSettings.h"
#include "QGCApplication.h"

const QString GeoserverMapProvider::m_serviceType = "WMTS";
const QString GeoserverMapProvider::m_requestType = "GetTile";

ServerProvider::ServerProvider(const QString& referrer, const QString &imageFormat, quint32 averageSize, QGeoMapType::MapStyle mapType, QObject *parent)
    : MapProvider(referrer, imageFormat, averageSize, mapType, parent)
{

}


GeoserverMapProvider::GeoserverMapProvider(QString geoserverURL, QObject *parent)
    : ServerProvider(geoserverURL, QStringLiteral("png"), 256, QGeoMapType::CustomMap, parent)
    , m_geoserverURL(geoserverURL)
    , m_wmtsVersion(V1_0_0)
    , m_EPSGNumber(900913)
    , m_imageFormat("png")
{

}

QString GeoserverMapProvider::wmtsVersionString()
{
    switch (m_wmtsVersion) {
    case V1_0_0: return "1.0.0";
    case V1_1_0: return "1.1.0";
    case V1_1_1: return "1.1.1";
    case V1_3_0: return "1.3.0";
    }
    return "1.0.0";
}

QNetworkRequest GeoserverMapProvider::getTileURL(const int x, const int y, const int zoom, QNetworkAccessManager *networkManager)
{
    //-- Build URL
    QNetworkRequest request;

    QUrl url = _getURL(x, y, zoom, networkManager);
    QUrlQuery query;

    if (url.isEmpty()) {
        return request;
    }

    //NEED REFACTORING
    query.addQueryItem("SERVICE",       m_serviceType);
    query.addQueryItem("VERSION",       wmtsVersionString());
    query.addQueryItem("REQUEST",       m_requestType);
    query.addQueryItem("FORMAT",        "image%2F" + m_imageFormat);
    query.addQueryItem("LAYER",         m_workspace+"%3A"+m_mapName);
    query.addQueryItem("TILEMATRIXSET", "EPSG%3A" + QString::number(m_EPSGNumber));
    query.addQueryItem("TILEMATRIX",    "EPSG%3A" + QString::number(m_EPSGNumber) + "%3A" + QString::number(zoom));
    query.addQueryItem("TILECOL",       QString::number(x));
    query.addQueryItem("TILEROW",       QString::number(y));

    url.setQuery(query.query());
    request.setUrl(QUrl(url));

    return request;
}

QString GeoserverMapProvider::_getURL(const int x, const int y, const int zoom, QNetworkAccessManager *networkManager)
{
    Q_UNUSED(networkManager)
    Q_UNUSED(x)
    Q_UNUSED(y)
    Q_UNUSED(zoom)

    GeoserverSettings *geoserverSettings = qgcApp()->toolbox()->settingsManager()->geoserverSettings();

    setWmtsVersion(GeoserverMapProvider::WMTSVersion(geoserverSettings->visualWMTSVersion()->rawValue().toInt()));
    setImageFormat(GeoserverMapProvider::ImageFormat(geoserverSettings->visualImageFormat()->rawValue().toInt()));
    setWorkspace(geoserverSettings->visualMapWorkspace()->rawValue().toString());
    setMapName(geoserverSettings->visualMapName()->rawValue().toString());
    setEPSGNumber(geoserverSettings->visualEPSGNumber()->rawValue().toString().toInt());

    QString urlString = m_geoserverURL;

    urlString += QString(m_geoserverURL.at(geoserverURL().length()-1) == '/' ? "" : "/")
              + "gwc/service/wmts";

    return urlString;
}

unsigned long long GeoserverMapProvider::getEPSGNumber() const
{
    return m_EPSGNumber;
}

void GeoserverMapProvider::setEPSGNumber(unsigned long long EPSGNumber)
{
    m_EPSGNumber = EPSGNumber;
}

QString GeoserverMapProvider::getImageFormat() const
{
    return m_imageFormat;
}

void GeoserverMapProvider::setImageFormat(const ImageFormat &imageFormat)
{
    switch (imageFormat) {
    case ImageFormat::png:
        m_imageFormat = "png";break;
    case ImageFormat::jpeg:
        m_imageFormat = "jpeg";break;
    default:
        m_imageFormat = "png";
    }
}

QString GeoserverMapProvider::getMapName() const
{
    return m_mapName;
}

void GeoserverMapProvider::setMapName(const QString &mapName)
{
    m_mapName = mapName;
}

QString GeoserverMapProvider::getWorkspace() const
{
    return m_workspace;
}

void GeoserverMapProvider::setWorkspace(const QString &workspace)
{
    m_workspace = workspace;
}

QString GeoserverMapProvider::geoserverURL() const
{
    return m_geoserverURL;
}

void GeoserverMapProvider::setGeoserverURL(const QString &geoserverURL)
{
    m_geoserverURL = geoserverURL;
}

GeoserverMapProvider::WMTSVersion GeoserverMapProvider::wmtsVersion() const
{
    return m_wmtsVersion;
}

void GeoserverMapProvider::setWmtsVersion(const WMTSVersion &wmsVersion)
{
    m_wmtsVersion = wmsVersion;
}
