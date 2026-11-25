/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


/**
 * @file
 *   @brief Map Tile Cache Worker Thread
 *
 *   @author Gus Grubba <gus@auterion.com>
 *
 */

#ifndef QGC_TILE_CACHE_WORKER_H
#define QGC_TILE_CACHE_WORKER_H

#include <QString>
#include <QThread>
#include <QQueue>
#include <QList>
#include <QMutex>
#include <QWaitCondition>
#include <QMutexLocker>
#include <QtSql/QSqlDatabase>
#include <QHostInfo>
#include <QCache>

#include "QGCLoggingCategory.h"

Q_DECLARE_LOGGING_CATEGORY(QGCTileCacheLog)

class QGCMapTask;
class QGCCachedTileSet;

struct QGCExternalTileCoverage
{
    double          topLat = 0.0;
    double          leftLon = 0.0;
    double          bottomLat = 0.0;
    double          rightLon = 0.0;
    int             minZoom = 0;
    int             maxZoom = 0;
    int             typeId = 0;
};

struct QGCExternalTileDatabase
{
    QString         path;
    QString         connectionName;
    QSqlDatabase    database;
    qint64          size = -1;
    int             fileDescriptor = -1;
    QList<QGCExternalTileCoverage> coverage;
};

//-----------------------------------------------------------------------------
class QGCCacheWorker : public QThread
{
    Q_OBJECT
public:
    QGCCacheWorker  ();
    ~QGCCacheWorker ();

    void    quit            ();
    bool    enqueueTask     (QGCMapTask* task);
    void    setDatabaseFile (const QString& path);

protected:
    void    run             ();

private slots:
    void        _lookupReady            (QHostInfo info);

private:
    void        _runTask                (QGCMapTask* task);

    void        _saveTile               (QGCMapTask* mtask);
    void        _getTile                (QGCMapTask* mtask);
    void        _getTileSets            (QGCMapTask* mtask);
    void        _createTileSet          (QGCMapTask* mtask);
    void        _getTileDownloadList    (QGCMapTask* mtask);
    void        _updateTileDownloadState(QGCMapTask* mtask);
    void        _deleteTileSet          (QGCMapTask* mtask);
    void        _clearDefaultTileSet    (QGCMapTask* mtask);
    void        _renameTileSet          (QGCMapTask* mtask);
    void        _resetCacheDatabase     (QGCMapTask* mtask);
    void        _pruneCache             (QGCMapTask* mtask);
    void        _exportSets             (QGCMapTask* mtask);
    void        _importSets             (QGCMapTask* mtask);
    bool        _testTask               (QGCMapTask* mtask);
    void        _testInternet           ();
    void        _deleteBingNoTileTiles  ();

    quint64     _findTile               (const QString hash);
    bool        _findTileSetID          (const QString name, quint64& setID);
    void        _updateSetTotals        (QGCCachedTileSet* set);
    bool        _init                   ();
    bool        _connectDB              ();
    void        _connectExternalDatabases();
    void        _disconnectExternalDatabases();
    bool        _externalMayContainTile(const QGCExternalTileDatabase& external,
                                        int typeId, int x, int y, int zoom) const;
    bool        _createDB               (QSqlDatabase& db, bool createDefault = true);
    void        _disconnectDB           ();
    quint64     _getDefaultTileSet      ();
    void        _updateTotals           ();
    void        _deleteTileSet          (qulonglong id);

signals:
    void        updateTotals            (quint32 totaltiles, quint64 totalsize, quint32 defaulttiles, quint64 defaultsize);
    void        internetStatus          (bool active);

private:
    QQueue<QGCMapTask*>             _taskQueue;
    QMutex                          _taskQueueMutex;
    QWaitCondition                  _waitc;
    QString                         _databasePath;
    QScopedPointer<QSqlDatabase>    _db;
    QList<QGCExternalTileDatabase>  _externalDatabases;
    QCache<QString, QByteArray>     _fallbackTileCache;
    std::atomic_bool                _valid;
    bool                            _failed;
    quint64                         _defaultSet;
    quint64                         _totalSize;
    quint32                         _totalCount;
    quint64                         _defaultSize;
    quint32                         _defaultCount;
    time_t                          _lastUpdate;
    int                             _updateTimeout;
    int                             _hostLookupID;
};

#endif // QGC_TILE_CACHE_WORKER_H
