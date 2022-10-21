#include "QGeoserverTile.h"
#include "QGCMapEngine.h"

#include <QString>
#include <QByteArray>
#include <QHash>
#include <QDir>
#include <QDebug>

struct QGeoserverTile::QGeoserverTilePrivate
{
    int size;
    int x,y,z;
    QByteArray tile;
    QString format;
    QString type;
};

QGeoserverTile::QGeoserverTile()
    : m_d {new QGeoserverTilePrivate}
{
    m_d->type = "Geoserver Map";
}

QGeoserverTile::QGeoserverTile(const QGeoserverTile &copy)
    : QGeoserverTile()
{
    m_d->size = copy.getSize();
    m_d->x = copy.getX();
    m_d->y = copy.getY();
    m_d->z = copy.getZoom();
    m_d->tile = copy.getTile();
    m_d->format = copy.getFormat();
}

void QGeoserverTile::operator=(const QGeoserverTile &copy)
{
    m_d->size = copy.getSize();
    m_d->x = copy.getX();
    m_d->y = copy.getY();
    m_d->z = copy.getZoom();
    m_d->tile = copy.getTile();
    m_d->format = copy.getFormat();
}

QGeoserverTile::~QGeoserverTile()
{

}

int QGeoserverTile::getSize() const
{
    return m_d->size;
}

int QGeoserverTile::getX() const
{
    return m_d->x;
}

int QGeoserverTile::getY() const
{
    return m_d->y;
}

int QGeoserverTile::getZoom() const
{
    return m_d->z;
}

QByteArray QGeoserverTile::getTile() const
{
    return m_d->tile;
}

QString QGeoserverTile::getFormat() const
{
    return m_d->format;
}

QString QGeoserverTile::getTileHash() const
{
    return QGCMapEngine::getTileHash(m_d->type, m_d->x, m_d->y, m_d->z);
}

QString QGeoserverTile::getType() const
{
    return m_d->type;
}

void QGeoserverTile::setSize(int size)
{
    m_d->size = size;
}

void QGeoserverTile::setX(int x)
{
    m_d->x = x;
}

void QGeoserverTile::setY(int y)
{
    m_d->y = y;
}

void QGeoserverTile::setZoom(int zoom)
{
    m_d->z = zoom;
}

void QGeoserverTile::setTile(QByteArray data)
{
    m_d->tile = data;
}

void QGeoserverTile::setFormat(QString format)
{
    m_d->format = format;
}
