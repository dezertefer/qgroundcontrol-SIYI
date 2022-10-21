#pragma once

#include <QList>
#include <memory>

class QString;
class QByteArray;
class QImage;
class QGeoserverTile;

class QGeoserverTileSet
{
public:
    enum State{
        DIRECTORY_PATH_IS_EMPTY,
        DIRECTORY_NOT_EXISTS,
        DIRECTORY_IS_EMPTY,
        DIRECTORY_IS_NOT_GEOSERVER_SET,
        TILES_SET_IS_EMPTY,
        OK
    };
    QGeoserverTileSet(QString dirPath); //TILE SET DIR PATH
    QGeoserverTileSet(const QGeoserverTileSet& copy);
    void operator=(const QGeoserverTileSet& copy);
    ~QGeoserverTileSet();

    void setID(quint64 id);
    void setTopleftLat(double lat);
    void setTopleftLon(double lon);
    void setBottomRightLat(double lat);
    void setBottomRightLon(double lon);
    QString getName() const;
    QString getType() const;
    quint64 getSetID() const;
    State getState() const;
    int getMinZoom() const;
    int getMaxZoom() const;
    int getTilesCount() const;
    double getTopleftLat() const;
    double getTopleftLon() const;
    double getBottomRightLat() const;
    double getBottomRightLon() const;
    QList<QGeoserverTile> getTiles() const;
    int getXByName(QString tileName) const;
    int getYByName(QString tileName) const;
    int getZoomByDirName(QString name) const;
    QString getTileFormatByName(QString tileName) const;
    QByteArray ImageToByteArray(QImage tileImage, QString format) const;

private:
    struct QGeoserverTileSetPrivate;
    std::unique_ptr<QGeoserverTileSetPrivate> m_d; //d-pointer
};

