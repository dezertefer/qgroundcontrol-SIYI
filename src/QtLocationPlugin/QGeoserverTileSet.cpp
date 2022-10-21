#include "QGeoserverTileSet.h"

#include <QDir>
#include <QImage>
#include <QBuffer>
#include <QDebug>

#include "QGeoserverTile.h"

struct QGeoserverTileSet::QGeoserverTileSetPrivate
{
    QString name;
    QString type;
    quint64 setID;
    State state;
    int minZoom;
    int maxZoom;
    int tilesCount;
    double topleftLat;
    double topleftLon;
    double bottomRightLat;
    double bottomRightLon;
    QList<QGeoserverTile> tiles;
};

QGeoserverTileSet::QGeoserverTileSet(QString dirPath)
    : m_d {new QGeoserverTileSetPrivate}
{
    QDir tilesSetDir(dirPath);

    m_d->state = OK;
    m_d->type = "Geoserver Map";
    m_d->minZoom = 1;
    m_d->maxZoom = 1;
    m_d->tilesCount = 0;
    m_d->name = tilesSetDir.dirName();

    if (dirPath.isEmpty())
    {
        m_d->state = DIRECTORY_PATH_IS_EMPTY;
        return;
    }

    if (!tilesSetDir.exists())
    {
        m_d->state = DIRECTORY_NOT_EXISTS;
        return;
    }

    if (tilesSetDir.isEmpty())
    {
        m_d->state = DIRECTORY_IS_EMPTY;
        return;
    }

    if (tilesSetDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).length() == 0)
    {
        m_d->state = DIRECTORY_IS_NOT_GEOSERVER_SET;
        return;
    }

    for (QString tilesDirName : tilesSetDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        QDir tilesDir(tilesSetDir.path() + "/" + tilesDirName);
        int zoom = getZoomByDirName(tilesDirName);

        if (zoom > 0 && zoom < m_d->minZoom)
            m_d->minZoom = zoom;
        if (zoom > m_d->maxZoom)
            m_d->maxZoom = zoom;

        for (QString tilesSubDirName : tilesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            QDir tilesSubDir(tilesDir.path() + "/" + tilesSubDirName);

            for (QString tileName : tilesSubDir.entryList(QDir::Files))
            {
                QString tileFormat = getTileFormatByName(tileName);
                QImage tileImage(tilesSubDir.path() + '/' + tileName);
                QByteArray tileData = ImageToByteArray(tileImage, tileFormat);

                QGeoserverTile tile;
                tile.setX(getXByName(tileName));
                tile.setY(getYByName(tileName));
                tile.setSize(tileData.size());
                tile.setTile(tileData);
                tile.setZoom(zoom);
                tile.setFormat(tileFormat);

                m_d->tilesCount++;
                m_d->tiles.append(tile);
            }
        }
    }

    if (m_d->tiles.length() == 0)
    {
        m_d->state = TILES_SET_IS_EMPTY;
        return;
    }
}

QGeoserverTileSet::QGeoserverTileSet(const QGeoserverTileSet &copy)
    : m_d{new QGeoserverTileSetPrivate}
{
    m_d->name           = copy.getName          ();
    m_d->type           = copy.getType          ();
    m_d->setID          = copy.getSetID         ();
    m_d->state          = copy.getState         ();
    m_d->minZoom        = copy.getMinZoom       ();
    m_d->maxZoom        = copy.getMaxZoom       ();
    m_d->tilesCount     = copy.getTilesCount    ();
    m_d->topleftLat     = copy.getTopleftLat    ();
    m_d->topleftLon     = copy.getTopleftLon    ();
    m_d->bottomRightLat = copy.getBottomRightLat();
    m_d->bottomRightLon = copy.getBottomRightLon();
    m_d->tiles          = copy.getTiles         ();
}

void QGeoserverTileSet::operator=(const QGeoserverTileSet &copy)
{
    m_d->name           = copy.getName          ();
    m_d->type           = copy.getType          ();
    m_d->setID          = copy.getSetID         ();
    m_d->state          = copy.getState         ();
    m_d->minZoom        = copy.getMinZoom       ();
    m_d->maxZoom        = copy.getMaxZoom       ();
    m_d->tilesCount     = copy.getTilesCount    ();
    m_d->topleftLat     = copy.getTopleftLat    ();
    m_d->topleftLon     = copy.getTopleftLon    ();
    m_d->bottomRightLat = copy.getBottomRightLat();
    m_d->bottomRightLon = copy.getBottomRightLon();
    m_d->tiles          = copy.getTiles         ();
}

QGeoserverTileSet::~QGeoserverTileSet()
{

}

void QGeoserverTileSet::setID(quint64 id)
{
    m_d->setID = id;
}

void QGeoserverTileSet::setTopleftLat(double lat)
{
    m_d->topleftLat = lat;
}

void QGeoserverTileSet::setTopleftLon(double lon)
{
    m_d->topleftLon = lon;
}

void QGeoserverTileSet::setBottomRightLat(double lat)
{
    m_d->bottomRightLat = lat;
}

void QGeoserverTileSet::setBottomRightLon(double lon)
{
    m_d->bottomRightLon = lon;
}

QString QGeoserverTileSet::getName() const
{
    return m_d->name;
}

QString QGeoserverTileSet::getType() const
{
    return m_d->type;
}

quint64 QGeoserverTileSet::getSetID() const
{
    return m_d->setID;
}

QGeoserverTileSet::State QGeoserverTileSet::getState() const
{
    return m_d->state;
}

int QGeoserverTileSet::getMinZoom() const
{
    return m_d->minZoom;
}

int QGeoserverTileSet::getMaxZoom() const
{
    return m_d->maxZoom;
}

int QGeoserverTileSet::getTilesCount() const
{
    return m_d->tilesCount;
}

double QGeoserverTileSet::getTopleftLat() const
{
    return m_d->topleftLat;
}

double QGeoserverTileSet::getTopleftLon() const
{
    return m_d->topleftLon;
}

double QGeoserverTileSet::getBottomRightLat() const
{
    return m_d->bottomRightLat;
}

double QGeoserverTileSet::getBottomRightLon() const
{
    return m_d->bottomRightLon;
}

QList<QGeoserverTile> QGeoserverTileSet::getTiles() const
{
    return m_d->tiles;
}

int QGeoserverTileSet::getXByName(QString tileName) const
{
    int underSlashIndex = tileName.indexOf('_');
    QString result = tileName.left(underSlashIndex);
    return result.toInt();
}

int QGeoserverTileSet::getYByName(QString tileName) const
{
    int underSlashIndex = tileName.indexOf('_');
    int dotIndex = tileName.lastIndexOf('.');
    QString result = tileName.mid(underSlashIndex+1, dotIndex-underSlashIndex-1);
    return result.toInt();
}

int QGeoserverTileSet::getZoomByDirName(QString name) const
{
    QString strZoom = "";
    strZoom += name.at(name.length()-2);
    strZoom += name.at(name.length()-1);

    return strZoom.toInt();
}

QString QGeoserverTileSet::getTileFormatByName(QString tileName) const
{
    int dotIndex = tileName.lastIndexOf('.');
    QString result = tileName.mid(dotIndex+1);
    return result;
}

QByteArray QGeoserverTileSet::ImageToByteArray(QImage tileImage, QString format) const
{
    QByteArray tileData;
    QBuffer buffer(&tileData);
    buffer.open(QIODevice::WriteOnly);
    tileImage.save(&buffer, format.toUpper().toUtf8());
    return tileData;
}

