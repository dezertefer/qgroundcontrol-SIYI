/****************************************************************************
**
** Copyright (C) 2013 Aaron McCarthy <mccarthy.aaron@gmail.com>
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtLocation module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
** 2015.4.4
** Adapted for use with QGroundControl
**
** Gus Grubba <gus@auterion.com>
**
****************************************************************************/

#include "QGCMapEngine.h"
#include "QGeoMapReplyQGC.h"
#include "QGeoTileFetcherQGC.h"

#include <QtLocation/private/qgeotilespec_p.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include "TerrainTile.h"

namespace {

constexpr qint64 kOfflineMapNetworkBackoffMsecs = 30000;
constexpr int kOfflineMapMaxNetworkRequests = 32;
qint64 offlineMapNetworkBackoffUntilMsecs = 0;

bool isOfflineMapDebugTile(const QGeoTileSpec& spec)
{
    const QString type = getQGCMapEngine()->urlFactory()->getTypeFromId(spec.mapId());
    return type.contains(QStringLiteral("Geoserver"), Qt::CaseInsensitive);
}

qint64 offlineMapNetworkBackoffRemainingMsecs()
{
    const qint64 remaining = offlineMapNetworkBackoffUntilMsecs - QDateTime::currentMSecsSinceEpoch();
    return remaining > 0 ? remaining : 0;
}

bool shouldBackoffOfflineMapNetworkError(QNetworkReply::NetworkError error)
{
    switch(error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return true;
    default:
        return false;
    }
}

void startOfflineMapNetworkBackoff(const QGeoTileSpec& spec, const char* reason)
{
    if(!isOfflineMapDebugTile(spec)) {
        return;
    }

    offlineMapNetworkBackoffUntilMsecs = QDateTime::currentMSecsSinceEpoch() + kOfflineMapNetworkBackoffMsecs;
    qInfo().noquote() << "[OfflineMapDebug] network BACKOFF"
                      << "reason=" << reason
                      << "durationMs=" << kOfflineMapNetworkBackoffMsecs;
}

void logOfflineMapTile(const char* event, const QGeoTileSpec& spec, const QNetworkRequest& request)
{
    if(!isOfflineMapDebugTile(spec)) {
        return;
    }

    const QString type = getQGCMapEngine()->urlFactory()->getTypeFromId(spec.mapId());
    qInfo().noquote() << "[OfflineMapDebug]" << event
                      << "type=" << type
                      << "mapId=" << spec.mapId()
                      << "z/x/y=" << QStringLiteral("%1/%2/%3").arg(spec.zoom()).arg(spec.x()).arg(spec.y())
                      << "hash=" << QGCMapEngine::getTileHash(type, spec.x(), spec.y(), spec.zoom())
                      << "url=" << request.url().toString();
}

}

int         QGeoTiledMapReplyQGC::_requestCount = 0;
QByteArray  QGeoTiledMapReplyQGC::_bingNoTileImage;

//-----------------------------------------------------------------------------
QGeoTiledMapReplyQGC::QGeoTiledMapReplyQGC(QNetworkAccessManager *networkManager, const QNetworkRequest &request, const QGeoTileSpec &spec, QObject *parent)
    : QGeoTiledMapReply(spec, parent)
    , _reply(nullptr)
    , _request(request)
    , _networkManager(networkManager)
{
    logOfflineMapTile("tile OPEN", spec, request);
    if (_bingNoTileImage.count() == 0) {
        QFile file(":/res/BingNoTileBytes.dat");
        file.open(QFile::ReadOnly);
        _bingNoTileImage = file.readAll();
        file.close();
    }
    if(_request.url().isEmpty()) {
        if(!_badMapbox.size()) {
            QFile b(":/res/notile.png");
            if(b.open(QFile::ReadOnly))
                _badMapbox = b.readAll();
        }
        setMapImageData(_badMapbox);
        setMapImageFormat("png");
        setFinished(true);
        setCached(false);
    } else {
        QGCFetchTileTask* task = getQGCMapEngine()->createFetchTileTask(getQGCMapEngine()->urlFactory()->getTypeFromId(spec.mapId()), spec.x(), spec.y(), spec.zoom());
        connect(task, &QGCFetchTileTask::tileFetched, this, &QGeoTiledMapReplyQGC::cacheReply);
        connect(task, &QGCMapTask::error, this, &QGeoTiledMapReplyQGC::cacheError);
        getQGCMapEngine()->addTask(task);
    }
}

//-----------------------------------------------------------------------------
QGeoTiledMapReplyQGC::~QGeoTiledMapReplyQGC()
{
    _clearReply();
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::_clearReply()
{
    _timer.stop();
    if (_reply) {
        QNetworkReply* reply = _reply.data();
        _reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->deleteLater();
        if(_requestCount > 0) {
            _requestCount--;
        }
    }
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::abort()
{
    if(isOfflineMapDebugTile(tileSpec())) {
        logOfflineMapTile("tile ABORT", tileSpec(), _request);
    }

    // QNetworkReply::abort() can enter Qt's HTTP backend after its internal
    // request has already been removed. Disconnect and defer deletion instead.
    _clearReply();
    emit aborted();
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::networkReplyFinished()
{
    _timer.stop();
    logOfflineMapTile("network FINISHED", tileSpec(), _request);
    if (!_reply) {
        emit aborted();
        return;
    }
    if (_reply->error() != QNetworkReply::NoError) {
        _clearReply();
        emit aborted();
        return;
    }

    if(isOfflineMapDebugTile(tileSpec())) {
        offlineMapNetworkBackoffUntilMsecs = 0;
    }
    QByteArray a = _reply->readAll();
    UrlFactory* urlFactory = getQGCMapEngine()->urlFactory();
    QString format = urlFactory->getImageFormat(tileSpec().mapId(), a);
    if(isOfflineMapDebugTile(tileSpec())) {
        qInfo().noquote() << "[OfflineMapDebug] network PAYLOAD"
                          << "z/x/y=" << QStringLiteral("%1/%2/%3").arg(tileSpec().zoom()).arg(tileSpec().x()).arg(tileSpec().y())
                          << "format=" << format
                          << "bytes=" << a.size()
                          << "header=" << a.left(8).toHex();
    }
    //-- Test for a specialized, elevation data (not map tile)
    if( getQGCMapEngine()->urlFactory()->isElevation(tileSpec().mapId())){
        a = TerrainTile::serializeFromAirMapJson(a);
        //-- Cache it if valid
        if(!a.isEmpty()) {
            getQGCMapEngine()->cacheTile(
                getQGCMapEngine()->urlFactory()->getTypeFromId(
                    tileSpec().mapId()),
                tileSpec().x(), tileSpec().y(), tileSpec().zoom(), a, format);
        }
        emit terrainDone(a, QNetworkReply::NoError);
    } else {
        MapProvider* mapProvider = urlFactory->getMapProviderFromId(tileSpec().mapId());
        if (mapProvider && mapProvider->_isBingProvider() && a.size() && _bingNoTileImage.size() && a == _bingNoTileImage) {
            // Bing doesn't return an error if you request a tile above supported zoom level
            // It instead returns an image of a missing tile graphic. We need to detect that
            // and error out so Qt will deal with zooming correctly even if it doesn't have the tile.
            // This allows us to zoom up to level 23 even though the tiles don't actually exist
            setError(QGeoTiledMapReply::CommunicationError, "Bing tile above zoom level");
        } else {
            //-- This is a map tile. Process and cache it if valid.
            setMapImageData(a);
            if(!format.isEmpty()) {
                setMapImageFormat(format);
                getQGCMapEngine()->cacheTile(getQGCMapEngine()->urlFactory()->getTypeFromId(tileSpec().mapId()), tileSpec().x(), tileSpec().y(), tileSpec().zoom(), a, format);
            }
        }
        setFinished(true);
    }
    _clearReply();
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::networkReplyError(QNetworkReply::NetworkError error)
{
    _timer.stop();
    if(isOfflineMapDebugTile(tileSpec())) {
        qInfo().noquote() << "[OfflineMapDebug] network ERROR"
                          << "z/x/y=" << QStringLiteral("%1/%2/%3").arg(tileSpec().zoom()).arg(tileSpec().x()).arg(tileSpec().y())
                          << "code=" << error
                          << "message=" << (_reply ? _reply->errorString() : QStringLiteral("reply already cleared"));
    }
    if (!_reply) {
        return;
    }
    if(shouldBackoffOfflineMapNetworkError(error)) {
        startOfflineMapNetworkBackoff(tileSpec(), "network error");
    }
    //-- Test for a specialized, elevation data (not map tile)
    if( getQGCMapEngine()->urlFactory()->isElevation(tileSpec().mapId())){
        emit terrainDone(QByteArray(), error);
    } else {
        //-- Regular map tile
        if (error != QNetworkReply::OperationCanceledError) {
            qWarning() << "Fetch tile error:" << _reply->errorString();
            setError(QGeoTiledMapReply::CommunicationError, _reply->errorString());
        }
        setFinished(true);
    }
    _clearReply();
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::cacheError(QGCMapTask::TaskType type, QString errorString)
{
    if(isOfflineMapDebugTile(tileSpec())) {
        qInfo().noquote() << "[OfflineMapDebug] cache reply MISS"
                          << "z/x/y=" << QStringLiteral("%1/%2/%3").arg(tileSpec().zoom()).arg(tileSpec().x()).arg(tileSpec().y())
                          << "error=" << errorString
                          << "internetActive=" << getQGCMapEngine()->isInternetActive();
    }
    if(!getQGCMapEngine()->isInternetActive()) {
        if( getQGCMapEngine()->urlFactory()->isElevation(tileSpec().mapId())){
            emit terrainDone(QByteArray(), QNetworkReply::NetworkSessionFailedError);
        } else {
            setError(QGeoTiledMapReply::CommunicationError, "Network not available");
            setFinished(true);
        }
    } else {
        if(type != QGCMapTask::taskFetchTile) {
            qWarning() << "QGeoTiledMapReplyQGC::cacheError() for wrong task";
        }

        if(isOfflineMapDebugTile(tileSpec())) {
            const qint64 backoffRemaining = offlineMapNetworkBackoffRemainingMsecs();
            if(backoffRemaining > 0) {
                qInfo().noquote() << "[OfflineMapDebug] network SKIPPED"
                                  << "reason=backoff"
                                  << "remainingMs=" << backoffRemaining;
                setError(QGeoTiledMapReply::CommunicationError, "GeoServer temporarily unavailable");
                setFinished(true);
                return;
            }
            if(_requestCount >= kOfflineMapMaxNetworkRequests) {
                qInfo().noquote() << "[OfflineMapDebug] network SKIPPED"
                                  << "reason=request-limit"
                                  << "pending=" << _requestCount;
                setError(QGeoTiledMapReply::CommunicationError, "Too many pending GeoServer tile requests");
                setFinished(true);
                return;
            }
        }

        //-- Tile not in cache. Get it off the Internet.
#if !defined(__mobile__)
        QNetworkProxy proxy = _networkManager->proxy();
        QNetworkProxy tProxy;
        tProxy.setType(QNetworkProxy::DefaultProxy);
        _networkManager->setProxy(tProxy);
#endif
        _reply = _networkManager->get(_request);
        logOfflineMapTile("network FALLBACK", tileSpec(), _request);
        _reply->setParent(nullptr);
        connect(_reply, &QNetworkReply::finished, this, &QGeoTiledMapReplyQGC::networkReplyFinished);
        connect(_reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkReplyError(QNetworkReply::NetworkError)));
#if !defined(__mobile__)
        _networkManager->setProxy(proxy);
#endif
        //- Wait for an answer up to 10 seconds
        connect(&_timer, &QTimer::timeout, this, &QGeoTiledMapReplyQGC::timeout);
        _timer.setSingleShot(true);
        _timer.start(10000);
        _requestCount++;
    }
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::cacheReply(QGCCacheTile* tile)
{
    if(isOfflineMapDebugTile(tileSpec())) {
        qInfo().noquote() << "[OfflineMapDebug] cache reply HIT"
                          << "z/x/y=" << QStringLiteral("%1/%2/%3").arg(tileSpec().zoom()).arg(tileSpec().x()).arg(tileSpec().y())
                          << "hash=" << tile->hash()
                          << "type=" << tile->type()
                          << "format=" << tile->format()
                          << "bytes=" << tile->img().size()
                          << "header=" << tile->img().left(8).toHex();
    }
    //-- Test for a specialized, elevation data (not map tile)
    if( getQGCMapEngine()->urlFactory()->isElevation(tileSpec().mapId())){
        emit terrainDone(tile->img(), QNetworkReply::NoError);
    } else {
        //-- Regular map tile
        setMapImageData(tile->img());
        setMapImageFormat(tile->format());
        setFinished(true);
        setCached(true);
    }
    tile->deleteLater();
}

//-----------------------------------------------------------------------------
void
QGeoTiledMapReplyQGC::timeout()
{
    logOfflineMapTile("network TIMEOUT", tileSpec(), _request);
    startOfflineMapNetworkBackoff(tileSpec(), "timeout");

    // Do not call QNetworkReply::abort() here. The reply QObject can still be
    // alive while Qt has already released its internal HTTP request state.
    _clearReply();
    emit aborted();
}
