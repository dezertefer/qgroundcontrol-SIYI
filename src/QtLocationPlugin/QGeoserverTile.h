#pragma once

#include <memory>

class QString;
class QByteArray;
class QGeoserverTile
{
public:
    enum State{
        DIRECTORY_NOT_EXISTS,
        DIRECTORY_IS_EMPTY,
        OK
    };

    QGeoserverTile();
    QGeoserverTile(const QGeoserverTile& copy);
    void operator=(const QGeoserverTile& copy);
    ~QGeoserverTile();
    int getSize() const;
    int getX() const;
    int getY() const;
    int getZoom() const;
    QString getType() const;
    QByteArray getTile() const;
    QString getFormat() const;
    QString getTileHash() const;

    void setSize(int size);
    void setX(int x);
    void setY(int y);
    void setZoom(int zoom);
    void setTile(QByteArray data);
    void setFormat(QString format);

private:
    struct QGeoserverTilePrivate;
    std::unique_ptr<QGeoserverTilePrivate> m_d; //d-pointer 0
};

