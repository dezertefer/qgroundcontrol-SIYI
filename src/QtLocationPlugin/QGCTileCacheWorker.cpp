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

#include "QGCMapEngine.h"
#include "QGCMapTileSet.h"
#include "AndroidDocumentUtils.h"

#include <QVariant>
#include <QtSql/QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

#include "time.h"

static const char*      kDefaultSet     = "Default Tile Set";
static const QString    kSession        = QStringLiteral("QGeoTileWorkerSession");
static const QString    kExportSession  = QStringLiteral("QGeoTileExportSession");
static const QString    kExternalSessionPrefix = QStringLiteral("QGeoTileExternalSession");
static const qint64     kExternalImportThreshold = 512LL * 1024LL * 1024LL;

namespace {

struct ExternalSourceHandle
{
    QString storedPath;
    QString databasePath;
    qint64 size = -1;
    int fileDescriptor = -1;
    bool databasePathIsUri = false;

    bool isValid() const { return !databasePath.isEmpty(); }
};

ExternalSourceHandle openExternalSource(const QString& requestedPath)
{
    ExternalSourceHandle source;
    source.storedPath = requestedPath;

    if(AndroidDocumentUtils::isContentUri(requestedPath)) {
        source.fileDescriptor = AndroidDocumentUtils::openReadOnlyFileDescriptor(
                    requestedPath, &source.size);
        if(source.fileDescriptor >= 0) {
            // Android's external-storage provider represents a real local file.
            // Prefer its direct path when scoped-storage rules allow it.
            const QString directPath =
                    AndroidDocumentUtils::directExternalStoragePath(requestedPath);
            QFile directFile(directPath);
            if(!directPath.isEmpty() && directFile.open(QIODevice::ReadOnly)) {
                source.size = directFile.size();
                directFile.close();
                AndroidDocumentUtils::closeFileDescriptor(source.fileDescriptor);
                source.fileDescriptor = -1;
                source.databasePath = QUrl::fromLocalFile(directPath)
                        .toString(QUrl::FullyEncoded)
                        + QStringLiteral("?mode=ro&immutable=1");
            } else {
                // SQLite needs URI mode and immutable=1 when it reopens an
                // already-authorized SAF descriptor. Immutable mode also keeps
                // it from attempting journal or lock files beside /proc/self/fd.
                source.databasePath = QStringLiteral(
                            "file:///proc/self/fd/%1?mode=ro&immutable=1")
                        .arg(source.fileDescriptor);
            }
            source.databasePathIsUri = true;
        }
        return source;
    }

    const QFileInfo file(requestedPath);
    if(!file.exists() || !file.isFile()) {
        return source;
    }

    source.storedPath = file.canonicalFilePath().isEmpty()
            ? file.absoluteFilePath()
            : file.canonicalFilePath();
    source.databasePath = source.storedPath;
    source.size = file.size();
    return source;
}

void closeExternalSource(ExternalSourceHandle& source)
{
    AndroidDocumentUtils::closeFileDescriptor(source.fileDescriptor);
    source.fileDescriptor = -1;
}

qint64 externalSourceSize(const QString& path)
{
    ExternalSourceHandle source = openExternalSource(path);
    const qint64 size = source.isValid() ? source.size : -1;
    closeExternalSource(source);
    return size;
}

bool isOfflineMapDebugType(const QString& type)
{
    return type.contains(QStringLiteral("Geoserver"), Qt::CaseInsensitive);
}

bool isOfflineMapDebugHash(const QString& hash)
{
    return isOfflineMapDebugType(getQGCMapEngine()->hashToType(hash));
}

}

QGC_LOGGING_CATEGORY(QGCTileCacheLog, "QGCTileCacheLog")

//-- Update intervals

#define LONG_TIMEOUT        5
#define SHORT_TIMEOUT       2

//-----------------------------------------------------------------------------
QGCCacheWorker::QGCCacheWorker()
    : _db(nullptr)
    , _valid(false)
    , _failed(false)
    , _defaultSet(UINT64_MAX)
    , _totalSize(0)
    , _totalCount(0)
    , _defaultSize(0)
    , _defaultCount(0)
    , _lastUpdate(0)
    , _updateTimeout(SHORT_TIMEOUT)
    , _hostLookupID(0)
{
}

//-----------------------------------------------------------------------------
QGCCacheWorker::~QGCCacheWorker()
{
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::setDatabaseFile(const QString& path)
{
    _databasePath = path;
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::quit()
{
    if(_hostLookupID) {
        QHostInfo::abortHostLookup(_hostLookupID);
    }
    QMutexLocker lock(&_taskQueueMutex);
    while(_taskQueue.count()) {
        QGCMapTask* task = _taskQueue.dequeue();
        delete task;
    }
    lock.unlock(); // don't need the lock any more
    if(this->isRunning()) {
        _waitc.wakeAll();
    }
}

//-----------------------------------------------------------------------------
bool
QGCCacheWorker::enqueueTask(QGCMapTask* task)
{
    //-- If not initialized, the only allowed task is Init
    if(!_valid && task->type() != QGCMapTask::taskInit) {
        task->setError("Database Not Initialized");
        task->deleteLater();
        return false;
    }
    QMutexLocker lock(&_taskQueueMutex);
    _taskQueue.enqueue(task);
    lock.unlock(); // don't need to hold the mutex any more
    if(this->isRunning()) {
        _waitc.wakeAll();
    } else {
        this->start(QThread::HighPriority);
    }
    return true;
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::run()
{
    if(!_valid && !_failed) {
        _init();
    }
    if(_valid) {
        _connectDB();
    }
    _deleteBingNoTileTiles();
    QMutexLocker lock(&_taskQueueMutex);
    while(true) {
        QGCMapTask* task;
        if(_taskQueue.count()) {
            task = _taskQueue.dequeue();

            // Don't need the lock while running the task.
            lock.unlock();
            _runTask(task);
            lock.relock();
            task->deleteLater();
            //-- Check for update timeout
            size_t count = static_cast<size_t>(_taskQueue.count());
            if(count > 100) {
                _updateTimeout = LONG_TIMEOUT;
            } else if(count < 25) {
                _updateTimeout = SHORT_TIMEOUT;
            }
            if(!count || (time(nullptr) - _lastUpdate > _updateTimeout)) {
                if(_valid) {
                    // _updateTotals() will emit a signal. Don't keep the lock
                    // while any slots process the signal.
                    lock.unlock();
                    _updateTotals();
                    lock.relock();
                }
            }
        } else {
            //-- Wait a bit before shutting things down
            unsigned long timeoutMilliseconds = 5000;
            _waitc.wait(lock.mutex(), timeoutMilliseconds);
            //-- If nothing to do, close db and leave thread
            if(!_taskQueue.count()) {
                break;
            }
        }
    }
    lock.unlock();
    _disconnectDB();
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_runTask(QGCMapTask *task)
{
    switch(task->type()) {
        case QGCMapTask::taskInit:
            return;
        case QGCMapTask::taskCacheTile:
            _saveTile(task);
            return;
        case QGCMapTask::taskFetchTile:
            _getTile(task);
            return;
        case QGCMapTask::taskFetchTileSets:
            _getTileSets(task);
            return;
        case QGCMapTask::taskCreateTileSet:
            _createTileSet(task);
            return;
        case QGCMapTask::taskGetTileDownloadList:
            _getTileDownloadList(task);
            return;
        case QGCMapTask::taskUpdateTileDownloadState:
            _updateTileDownloadState(task);
            return;
        case QGCMapTask::taskDeleteTileSet:
            _deleteTileSet(task);
            return;
        case QGCMapTask::taskClearDefaultTileSet:
            _clearDefaultTileSet(task);
            return;
        case QGCMapTask::taskRenameTileSet:
            _renameTileSet(task);
            return;
        case QGCMapTask::taskPruneCache:
            _pruneCache(task);
            return;
        case QGCMapTask::taskReset:
            _resetCacheDatabase(task);
            return;
        case QGCMapTask::taskExport:
            _exportSets(task);
            return;
        case QGCMapTask::taskImport:
            _importSets(task);
            return;
        case QGCMapTask::taskTestInternet:
            _testInternet();
            return;
    }
    qCWarning(QGCTileCacheLog) << "_runTask given unhandled task type" << task->type();
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_deleteBingNoTileTiles()
{
    QSettings settings;
    static const char* alreadyDoneKey = "_deleteBingNoTileTilesDone";

    if (settings.value(alreadyDoneKey, false).toBool()) {
        return;
    }
    settings.setValue(alreadyDoneKey, true);

    // Previously we would store these empty tile graphics in the cache. This prevented the ability to zoom beyong the level
    // of available tiles. So we need to remove only of these still hanging around to make higher zoom levels work.
    QFile file(":/res/BingNoTileBytes.dat");
    file.open(QFile::ReadOnly);
    QByteArray noTileBytes = file.readAll();
    file.close();

    QSqlQuery query(*_db);
    QString s;
    //-- Select tiles in default set only, sorted by oldest.
    s = QString("SELECT tileID, tile, hash FROM Tiles WHERE LENGTH(tile) = %1").arg(noTileBytes.count());
    QList<quint64> idsToDelete;
    if (query.exec(s)) {
        while(query.next()) {
            if (query.value(1).toByteArray() == noTileBytes) {
                idsToDelete.append(query.value(0).toULongLong());
                qCDebug(QGCTileCacheLog) << "_deleteBingNoTileTiles HASH:" << query.value(2).toString();
            }
        }
        for (const quint64 tileId: idsToDelete) {
            s = QString("DELETE FROM Tiles WHERE tileID = %1").arg(tileId);
            if (!query.exec(s)) {
                qCWarning(QGCTileCacheLog) << "Delete failed";
            }
        }
    } else {
        qCWarning(QGCTileCacheLog) << "_deleteBingNoTileTiles query failed";
    }
}

//-----------------------------------------------------------------------------
bool
QGCCacheWorker::_findTileSetID(const QString name, quint64& setID)
{
    QSqlQuery query(*_db);
    QString s = QString("SELECT setID FROM TileSets WHERE name = \"%1\"").arg(name);
    if(query.exec(s)) {
        if(query.next()) {
            setID = query.value(0).toULongLong();
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
quint64
QGCCacheWorker::_getDefaultTileSet()
{
    if(_defaultSet != UINT64_MAX)
        return _defaultSet;
    QSqlQuery query(*_db);
    QString s = QString("SELECT setID FROM TileSets WHERE defaultSet = 1");
    if(query.exec(s)) {
        if(query.next()) {
            _defaultSet = query.value(0).toULongLong();
            return _defaultSet;
        }
    }
    return 1L;
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_saveTile(QGCMapTask *mtask)
{
    if(_valid) {
        QGCSaveTileTask* task = static_cast<QGCSaveTileTask*>(mtask);
        QSqlQuery query(*_db);
        query.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
        query.addBindValue(task->tile()->hash());
        query.addBindValue(task->tile()->format());
        query.addBindValue(task->tile()->img());
        query.addBindValue(task->tile()->img().size());
        query.addBindValue(task->tile()->type());
        query.addBindValue(QDateTime::currentDateTime().toTime_t());
        if(query.exec()) {
            quint64 tileID = query.lastInsertId().toULongLong();
            quint64 setID = task->tile()->set() == UINT64_MAX ? _getDefaultTileSet() : task->tile()->set();
            QString s = QString("INSERT INTO SetTiles(tileID, setID) VALUES(%1, %2)").arg(tileID).arg(setID);
            query.prepare(s);
            if(!query.exec()) {
                qWarning() << "Map Cache SQL error (add tile into SetTiles):" << query.lastError().text();
            }
            qCDebug(QGCTileCacheLog) << "_saveTile() HASH:" << task->tile()->hash();
        } else {
            //-- Tile was already there.
            //   QtLocation some times requests the same tile twice in a row. The first is saved, the second is already there.
        }
    } else {
        qWarning() << "Map Cache SQL error (saveTile() open db):" << _db->lastError();
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_getTile(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    bool found = false;
    QGCFetchTileTask* task = static_cast<QGCFetchTileTask*>(mtask);
    const bool offlineMapDebug = isOfflineMapDebugHash(task->hash());
    if(offlineMapDebug) {
        qInfo().noquote() << "[OfflineMapDebug] cache lookup"
                          << "hash=" << task->hash()
                          << "database=" << _databasePath;
    }
    QSqlQuery query(*_db);
    query.prepare(QStringLiteral("SELECT tile, format, type FROM Tiles WHERE hash = ?"));
    query.addBindValue(task->hash());
    if(query.exec()) {
        if(query.next()) {
            const QByteArray ar = query.value(0).toByteArray();
            const QString format = query.value(1).toString();
            const int typeId = query.value(2).toInt();
            const QString type = getQGCMapEngine()->urlFactory()->getTypeFromId(typeId);
            qCDebug(QGCTileCacheLog) << "_getTile() (Found in DB) HASH:" << task->hash();
            if(offlineMapDebug) {
                qInfo().noquote() << "[OfflineMapDebug] cache HIT"
                                  << "hash=" << task->hash()
                                  << "source=local"
                                  << "typeId=" << typeId
                                  << "type=" << type
                                  << "format=" << format
                                  << "bytes=" << ar.size()
                                  << "header=" << ar.left(8).toHex();
            }
            task->setTileFetched(new QGCCacheTile(task->hash(), ar, format, type));
            found = true;
        }
    } else if(offlineMapDebug) {
        qInfo().noquote() << "[OfflineMapDebug] cache SQL ERROR"
                          << "hash=" << task->hash()
                          << "error=" << query.lastError().text();
    }
    if(!found) {
        for(QGCExternalTileDatabase& external : _externalDatabases) {
            QSqlQuery externalQuery(external.database);
            externalQuery.prepare(QStringLiteral("SELECT tile, format, type FROM Tiles WHERE hash = ?"));
            externalQuery.addBindValue(task->hash());
            if(!externalQuery.exec()) {
                if(offlineMapDebug) {
                    qInfo().noquote() << "[OfflineMapDebug] external cache SQL ERROR"
                                      << "hash=" << task->hash()
                                      << "path=" << external.path
                                      << "error=" << externalQuery.lastError().text();
                }
                continue;
            }
            if(!externalQuery.next()) {
                continue;
            }
            const QByteArray ar = externalQuery.value(0).toByteArray();
            const QString format = externalQuery.value(1).toString();
            const int typeId = externalQuery.value(2).toInt();
            const QString type = getQGCMapEngine()->urlFactory()->getTypeFromId(typeId);
            if(offlineMapDebug) {
                qInfo().noquote() << "[OfflineMapDebug] cache HIT"
                                  << "hash=" << task->hash()
                                  << "source=external"
                                  << "path=" << external.path
                                  << "typeId=" << typeId
                                  << "type=" << type
                                  << "format=" << format
                                  << "bytes=" << ar.size()
                                  << "header=" << ar.left(8).toHex();
            }
            task->setTileFetched(new QGCCacheTile(task->hash(), ar, format, type));
            found = true;
            break;
        }
    }
    if(!found) {
        qCDebug(QGCTileCacheLog) << "_getTile() (NOT in DB) HASH:" << task->hash();
        if(offlineMapDebug) {
            qInfo().noquote() << "[OfflineMapDebug] cache MISS"
                              << "hash=" << task->hash();
        }
        task->setError("Tile not in cache database");
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_getTileSets(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCFetchTileSetTask* task = static_cast<QGCFetchTileSetTask*>(mtask);
    QSqlQuery query(*_db);
    QString s = QString("SELECT * FROM TileSets ORDER BY defaultSet DESC, name ASC");
    qCDebug(QGCTileCacheLog) << "_getTileSets(): " << s;
    if(query.exec(s)) {
        while(query.next()) {
            QString name = query.value("name").toString();
            QGCCachedTileSet* set = new QGCCachedTileSet(name);
            set->setId(query.value("setID").toULongLong());
            set->setMapTypeStr(query.value("typeStr").toString());
            set->setTopleftLat(query.value("topleftLat").toDouble());
            set->setTopleftLon(query.value("topleftLon").toDouble());
            set->setBottomRightLat(query.value("bottomRightLat").toDouble());
            set->setBottomRightLon(query.value("bottomRightLon").toDouble());
            set->setMinZoom(query.value("minZoom").toInt());
            set->setMaxZoom(query.value("maxZoom").toInt());
            set->setType(getQGCMapEngine()->urlFactory()->getTypeFromId(query.value("type").toInt()));
            set->setTotalTileCount(query.value("numTiles").toUInt());
            set->setDefaultSet(query.value("defaultSet").toInt() != 0);
            set->setCreationDate(QDateTime::fromTime_t(query.value("date").toUInt()));
            _updateSetTotals(set);
            //-- Object created here must be moved to app thread to be used there
            set->moveToThread(QApplication::instance()->thread());
            task->tileSetFetched(set);
        }
    } else {
        task->setError("No tile set in database");
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_updateSetTotals(QGCCachedTileSet* set)
{
    if(set->defaultSet()) {
        _updateTotals();
        set->setSavedTileCount(_totalCount);
        set->setSavedTileSize(_totalSize);
        set->setTotalTileCount(_defaultCount);
        set->setTotalTileSize(_defaultSize);
        return;
    }
    QSqlQuery externalSetQuery(*_db);
    externalSetQuery.prepare(QStringLiteral("SELECT path FROM ExternalTileSets WHERE setID = ?"));
    externalSetQuery.addBindValue(QVariant::fromValue<qulonglong>(set->id()));
    if(externalSetQuery.exec() && externalSetQuery.next()) {
        const QString externalPath = externalSetQuery.value(0).toString();
        qint64 sourceSize = -1;
        for(const QGCExternalTileDatabase& external : _externalDatabases) {
            if(external.path == externalPath) {
                sourceSize = external.size;
                break;
            }
        }
        if(sourceSize < 0) {
            sourceSize = externalSourceSize(externalPath);
        }
        const bool available = sourceSize >= 0;
        const quint64 externalSize = available ? static_cast<quint64>(sourceSize) : 0;
        set->setSavedTileCount(available ? set->totalTileCount() : 0);
        set->setSavedTileSize(externalSize);
        set->setTotalTileSize(externalSize);
        set->setUniqueTileCount(set->totalTileCount());
        set->setUniqueTileSize(externalSize);
        return;
    }
    QSqlQuery subquery(*_db);
    QString sq = QString("SELECT COUNT(size), SUM(size) FROM Tiles A INNER JOIN SetTiles B on A.tileID = B.tileID WHERE B.setID = %1").arg(set->id());
    qCDebug(QGCTileCacheLog) << "_updateSetTotals(): " << sq;
    if(subquery.exec(sq)) {
        if(subquery.next()) {
            set->setSavedTileCount(subquery.value(0).toUInt());
            set->setSavedTileSize(subquery.value(1).toULongLong());
            qCDebug(QGCTileCacheLog) << "Set" << set->id() << "Totals:" << set->savedTileCount() << " " << set->savedTileSize() << "Expected: " << set->totalTileCount() << " " << set->totalTilesSize();
            //-- Update (estimated) size
            quint64 avg = getQGCMapEngine()->urlFactory()->averageSizeForType(set->type());
            if(set->totalTileCount() <= set->savedTileCount()) {
                //-- We're done so the saved size is the total size
                set->setTotalTileSize(set->savedTileSize());
            } else {
                //-- Otherwise we need to estimate it.
                if(set->savedTileCount() > 10 && set->savedTileSize()) {
                    avg = set->savedTileSize() / set->savedTileCount();
                }
                set->setTotalTileSize(avg * set->totalTileCount());
            }
            //-- Now figure out the count for tiles unique to this set
            quint32 ucount = 0;
            quint64 usize  = 0;
            sq = QString("SELECT COUNT(size), SUM(size) FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = %1 GROUP by A.tileID HAVING COUNT(A.tileID) = 1)").arg(set->id());
            if(subquery.exec(sq)) {
                if(subquery.next()) {
                    //-- This is only accurate when all tiles are downloaded
                    ucount = subquery.value(0).toUInt();
                    usize  = subquery.value(1).toULongLong();
                }
            }
            //-- If we haven't downloaded it all, estimate size of unique tiles
            quint32 expectedUcount = set->totalTileCount() - set->savedTileCount();
            if(!ucount) {
                usize = expectedUcount * avg;
            } else {
                expectedUcount = ucount;
            }
            set->setUniqueTileCount(expectedUcount);
            set->setUniqueTileSize(usize);
        }
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_updateTotals()
{
    QSqlQuery query(*_db);
    QString s;
    s = QString("SELECT COUNT(size), SUM(size) FROM Tiles");
    qCDebug(QGCTileCacheLog) << "_updateTotals(): " << s;
    if(query.exec(s)) {
        if(query.next()) {
            _totalCount = query.value(0).toUInt();
            _totalSize  = query.value(1).toULongLong();
        }
    }
    QSqlQuery externalQuery(*_db);
    if(externalQuery.exec(QStringLiteral(
            "SELECT COALESCE(SUM(TileSets.numTiles), 0) "
            "FROM TileSets INNER JOIN ExternalTileSets USING(setID)")) && externalQuery.next()) {
        _totalCount += externalQuery.value(0).toUInt();
    }
    if(externalQuery.exec(QStringLiteral("SELECT DISTINCT path FROM ExternalTileSets"))) {
        while(externalQuery.next()) {
            const QString externalPath = externalQuery.value(0).toString();
            qint64 sourceSize = -1;
            for(const QGCExternalTileDatabase& external : _externalDatabases) {
                if(external.path == externalPath) {
                    sourceSize = external.size;
                    break;
                }
            }
            if(sourceSize < 0) {
                sourceSize = externalSourceSize(externalPath);
            }
            if(sourceSize >= 0) {
                _totalSize += static_cast<quint64>(sourceSize);
            }
        }
    }
    s = QString("SELECT COUNT(size), SUM(size) FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = %1 GROUP by A.tileID HAVING COUNT(A.tileID) = 1)").arg(_getDefaultTileSet());
    qCDebug(QGCTileCacheLog) << "_updateTotals(): " << s;
    if(query.exec(s)) {
        if(query.next()) {
            _defaultCount = query.value(0).toUInt();
            _defaultSize  = query.value(1).toULongLong();
        }
    }
    emit updateTotals(_totalCount, _totalSize, _defaultCount, _defaultSize);
    _lastUpdate = time(nullptr);
}

//-----------------------------------------------------------------------------
quint64 QGCCacheWorker::_findTile(const QString hash)
{
    quint64 tileID = 0;
    QSqlQuery query(*_db);
    QString s = QString("SELECT tileID FROM Tiles WHERE hash = \"%1\"").arg(hash);
    if(query.exec(s)) {
        if(query.next()) {
            tileID = query.value(0).toULongLong();
        }
    }
    return tileID;
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_createTileSet(QGCMapTask *mtask)
{
    if(_valid) {
        //-- Create Tile Set
        quint32 actual_count = 0;
        QGCCreateTileSetTask* task = static_cast<QGCCreateTileSetTask*>(mtask);
        QSqlQuery query(*_db);
        query.prepare("INSERT INTO TileSets("
            "name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, date"
            ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        query.addBindValue(task->tileSet()->name());
        query.addBindValue(task->tileSet()->mapTypeStr());
        query.addBindValue(task->tileSet()->topleftLat());
        query.addBindValue(task->tileSet()->topleftLon());
        query.addBindValue(task->tileSet()->bottomRightLat());
        query.addBindValue(task->tileSet()->bottomRightLon());
        query.addBindValue(task->tileSet()->minZoom());
        query.addBindValue(task->tileSet()->maxZoom());
        query.addBindValue(getQGCMapEngine()->urlFactory()->getIdFromType(task->tileSet()->type()));
        query.addBindValue(task->tileSet()->totalTileCount());
        query.addBindValue(QDateTime::currentDateTime().toTime_t());
        if(!query.exec()) {
            qWarning() << "Map Cache SQL error (add tileSet into TileSets):" << query.lastError().text();
        } else {
            //-- Get just created (auto-incremented) setID
            quint64 setID = query.lastInsertId().toULongLong();
            task->tileSet()->setId(setID);
            //-- Prepare Download List
            quint64 tileCount = 0;
            _db->transaction();
            for(int z = task->tileSet()->minZoom(); z <= task->tileSet()->maxZoom(); z++) {
                QGCTileSet set = QGCMapEngine::getTileCount(z,
                    task->tileSet()->topleftLon(), task->tileSet()->topleftLat(),
                    task->tileSet()->bottomRightLon(), task->tileSet()->bottomRightLat(), task->tileSet()->type());
                tileCount += set.tileCount;
                QString type = task->tileSet()->type();
                for(int x = set.tileX0; x <= set.tileX1; x++) {
                    for(int y = set.tileY0; y <= set.tileY1; y++) {
                        //-- See if tile is already downloaded
                        QString hash = QGCMapEngine::getTileHash(type, x, y, z);
                        quint64 tileID = _findTile(hash);
                        if(!tileID) {
                            //-- Set to download
                            query.prepare("INSERT OR IGNORE INTO TilesDownload(setID, hash, type, x, y, z, state) VALUES(?, ?, ?, ?, ? ,? ,?)");
                            query.addBindValue(setID);
                            query.addBindValue(hash);
                            query.addBindValue(getQGCMapEngine()->urlFactory()->getIdFromType(type));
                            query.addBindValue(x);
                            query.addBindValue(y);
                            query.addBindValue(z);
                            query.addBindValue(0);
                            if(!query.exec()) {
                                qWarning() << "Map Cache SQL error (add tile into TilesDownload):" << query.lastError().text();
                                mtask->setError("Error creating tile set download list");
                                return;
                            } else
                                actual_count++;
                        } else {
                            //-- Tile already in the database. No need to dowload.
                            QString s = QString("INSERT OR IGNORE INTO SetTiles(tileID, setID) VALUES(%1, %2)").arg(tileID).arg(setID);
                            query.prepare(s);
                            if(!query.exec()) {
                                qWarning() << "Map Cache SQL error (add tile into SetTiles):" << query.lastError().text();
                            }
                            qCDebug(QGCTileCacheLog) << "_createTileSet() Already Cached HASH:" << hash;
                        }
                    }
                }
            }
            _db->commit();
            //-- Done
            _updateSetTotals(task->tileSet());
            task->setTileSetSaved();
            return;
        }
    }
    mtask->setError("Error saving tile set");
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_getTileDownloadList(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QList<QGCTile*> tiles;
    QGCGetTileDownloadListTask* task = static_cast<QGCGetTileDownloadListTask*>(mtask);
    QSqlQuery query(*_db);
    QString s = QString("SELECT hash, type, x, y, z FROM TilesDownload WHERE setID = %1 AND state = 0 LIMIT %2").arg(task->setID()).arg(task->count());
    if(query.exec(s)) {
        while(query.next()) {
            QGCTile* tile = new QGCTile;
            tile->setHash(query.value("hash").toString());
            tile->setType(getQGCMapEngine()->urlFactory()->getTypeFromId(query.value("type").toInt()));
            tile->setX(query.value("x").toInt());
            tile->setY(query.value("y").toInt());
            tile->setZ(query.value("z").toInt());
            tiles.append(tile);
        }
        for(int i = 0; i < tiles.size(); i++) {
            s = QString("UPDATE TilesDownload SET state = %1 WHERE setID = %2 and hash = \"%3\"").arg(static_cast<int>(QGCTile::StateDownloading)).arg(task->setID()).arg(tiles[i]->hash());
            if(!query.exec(s)) {
                qWarning() << "Map Cache SQL error (set TilesDownload state):" << query.lastError().text();
            }
        }
    }
    task->setTileListFetched(tiles);
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_updateTileDownloadState(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCUpdateTileDownloadStateTask* task = static_cast<QGCUpdateTileDownloadStateTask*>(mtask);
    QSqlQuery query(*_db);
    QString s;
    if(task->state() == QGCTile::StateComplete) {
        s = QString("DELETE FROM TilesDownload WHERE setID = %1 AND hash = \"%2\"").arg(task->setID()).arg(task->hash());
    } else {
        if(task->hash() == "*") {
            s = QString("UPDATE TilesDownload SET state = %1 WHERE setID = %2").arg(static_cast<int>(task->state())).arg(task->setID());
        } else {
            s = QString("UPDATE TilesDownload SET state = %1 WHERE setID = %2 AND hash = \"%3\"").arg(static_cast<int>(task->state())).arg(task->setID()).arg(task->hash());
        }
    }
    if(!query.exec(s)) {
        qWarning() << "QGCCacheWorker::_updateTileDownloadState() Error:" << query.lastError().text();
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_pruneCache(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCPruneCacheTask* task = static_cast<QGCPruneCacheTask*>(mtask);
    QSqlQuery query(*_db);
    QString s;
    //-- Select tiles in default set only, sorted by oldest.
    s = QString("SELECT tileID, size, hash FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A join SetTiles B on A.tileID = B.tileID WHERE B.setID = %1 GROUP by A.tileID HAVING COUNT(A.tileID) = 1) ORDER BY DATE ASC LIMIT 128").arg(_getDefaultTileSet());
    qint64 amount = (qint64)task->amount();
    QList<quint64> tlist;
    if(query.exec(s)) {
        while(query.next() && amount >= 0) {
            tlist << query.value(0).toULongLong();
            amount -= query.value(1).toULongLong();
            qCDebug(QGCTileCacheLog) << "_pruneCache() HASH:" << query.value(2).toString();
        }
        while(tlist.count()) {
            s = QString("DELETE FROM Tiles WHERE tileID = %1").arg(tlist[0]);
            tlist.removeFirst();
            if(!query.exec(s))
                break;
        }
        task->setPruned();
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_deleteTileSet(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCDeleteTileSetTask* task = static_cast<QGCDeleteTileSetTask*>(mtask);
    _deleteTileSet(task->setID());
    task->setTileSetDeleted();
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_deleteTileSet(qulonglong id)
{
    QSqlQuery query(*_db);
    QString s;
    query.prepare(QStringLiteral("DELETE FROM ExternalTileSets WHERE setID = ?"));
    query.addBindValue(QVariant::fromValue<qulonglong>(id));
    const bool removedExternalSet = query.exec() && query.numRowsAffected() > 0;
    //-- Only delete tiles unique to this set
    s = QString("DELETE FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A JOIN SetTiles B ON A.tileID = B.tileID WHERE B.setID = %1 GROUP BY A.tileID HAVING COUNT(A.tileID) = 1)").arg(id);
    query.exec(s);
    s = QString("DELETE FROM TilesDownload WHERE setID = %1").arg(id);
    query.exec(s);
    s = QString("DELETE FROM TileSets WHERE setID = %1").arg(id);
    query.exec(s);
    s = QString("DELETE FROM SetTiles WHERE setID = %1").arg(id);
    query.exec(s);
    if(removedExternalSet) {
        _disconnectExternalDatabases();
        _connectExternalDatabases();
    }
    _updateTotals();
}

void QGCCacheWorker::_clearDefaultTileSet(QGCMapTask* mtask)
{
    if (!_testTask(mtask)) {
        return;
    }

    QGCClearDefaultTileSetTask* task = static_cast<QGCClearDefaultTileSetTask*>(mtask);

    const quint64 defaultSetID = _getDefaultTileSet();

    QSqlQuery query(*_db);
    QString s;

    _db->transaction();

    // Delete only tiles that belong to the default set and are not shared
    // with any manually-created tile set.
    s = QString(
            "DELETE FROM Tiles "
            "WHERE tileID IN ("
            "SELECT A.tileID "
            "FROM SetTiles A "
            "JOIN SetTiles B ON A.tileID = B.tileID "
            "WHERE B.setID = %1 "
            "GROUP BY A.tileID "
            "HAVING COUNT(A.tileID) = 1"
            ")"
            ).arg(defaultSetID);

    if (!query.exec(s)) {
        _db->rollback();
        task->setError(QStringLiteral("Error clearing default tile cache: ") + query.lastError().text());
        return;
    }

    // Remove dangling references from SetTiles.
    // This keeps SetTiles clean after deleting default-only tiles.
    s = QString(
            "DELETE FROM SetTiles "
            "WHERE setID = %1 "
            "AND tileID NOT IN (SELECT tileID FROM Tiles)"
            ).arg(defaultSetID);

    if (!query.exec(s)) {
        _db->rollback();
        task->setError(QStringLiteral("Error cleaning default tile references: ") + query.lastError().text());
        return;
    }

    // Do not delete TileSets row for the default set.
    // Do not touch manually-created TileSets.
    // Do not touch TilesDownload for manual sets.

    if (!_db->commit()) {
        task->setError(QStringLiteral("Error committing default tile cache clear: ") + _db->lastError().text());
        return;
    }

    _updateTotals();

    task->setDefaultTileSetCleared();
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_renameTileSet(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCRenameTileSetTask* task = static_cast<QGCRenameTileSetTask*>(mtask);
    QSqlQuery query(*_db);
    QString s;
    s = QString("UPDATE TileSets SET name = \"%1\" WHERE setID = %2").arg(task->newName()).arg(task->setID());
    if(!query.exec(s)) {
        task->setError("Error renaming tile set");
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_resetCacheDatabase(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCResetTask* task = static_cast<QGCResetTask*>(mtask);
    _disconnectExternalDatabases();
    QSqlQuery query(*_db);
    QString s;
    s = QString("DROP TABLE Tiles");
    query.exec(s);
    s = QString("DROP TABLE TileSets");
    query.exec(s);
    s = QString("DROP TABLE SetTiles");
    query.exec(s);
    s = QString("DROP TABLE TilesDownload");
    query.exec(s);
    s = QString("DROP TABLE IF EXISTS ExternalTileSets");
    query.exec(s);
    _valid = _createDB(*_db);
    task->setResetCompleted();
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_importSets(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCImportTileTask* task = static_cast<QGCImportTileTask*>(mtask);
    ExternalSourceHandle importSource = openExternalSource(task->path());
    const bool contentDocument = AndroidDocumentUtils::isContentUri(task->path());
    qInfo().noquote() << "[OfflineMapDebug] import START"
                      << "path=" << importSource.storedPath
                      << "available=" << importSource.isValid()
                      << "bytes=" << importSource.size
                      << "contentDocument=" << contentDocument
                      << "replace=" << task->replace()
                      << "destination=" << _databasePath;
    if(!importSource.isValid()) {
        task->setError(contentDocument
                       ? QStringLiteral("The selected Android document could not be opened. Check that the SD card is available and select the file again.")
                       : QStringLiteral("The selected map database could not be opened."));
        task->setImportCompleted();
        return;
    }
    //-- Large packages must stay on their original storage. Copying a 50-100 GiB
    //   database into the application cache is both wasteful and often impossible
    //   on Android. Register a lightweight local tile-set record and query the
    //   selected SQLite database directly instead.
    //   Android Storage Access Framework documents must always stay in place:
    //   they are represented by content URIs and cannot be copied with QFile.
    const bool mountInPlace = contentDocument
            || (!task->replace() && importSource.size >= kExternalImportThreshold);
    if(mountInPlace) {
        const QString sourcePath = importSource.storedPath;
        const QString mountSession = QStringLiteral("%1_mount").arg(kExportSession);
        bool mounted = false;
        QString mountError;
        quint64 sourceTileCount = 0;
        int mountedSetCount = 0;
        qInfo().noquote() << "[OfflineMapDebug] import MOUNT START"
                          << "path=" << sourcePath
                          << "access=" << (importSource.fileDescriptor >= 0
                                               ? QStringLiteral("document-fd")
                                               : QStringLiteral("direct-path"))
                          << "sqliteSource=" << importSource.databasePath
                          << "thresholdBytes=" << kExternalImportThreshold;
        {
            QSqlDatabase dbImport = QSqlDatabase::addDatabase("QSQLITE", mountSession);
            dbImport.setDatabaseName(importSource.databasePath);
            dbImport.setConnectOptions(importSource.databasePathIsUri
                                       ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_OPEN_URI")
                                       : QStringLiteral("QSQLITE_OPEN_READONLY"));
            if(!dbImport.open()) {
                const QSqlError error = dbImport.lastError();
                mountError = QStringLiteral("%1 (native code %2)")
                        .arg(error.text(), error.nativeErrorCode());
            } else {
                QSqlQuery sourceQuery(dbImport);
                if(sourceQuery.exec(QStringLiteral("SELECT COUNT(tileID) FROM Tiles"))
                        && sourceQuery.next()) {
                    sourceTileCount = sourceQuery.value(0).toULongLong();
                }
                if(!sourceTileCount) {
                    mountError = QStringLiteral("External database contains no tiles");
                } else if(!sourceQuery.exec(QStringLiteral(
                        "SELECT * FROM TileSets ORDER BY defaultSet DESC, name ASC"))) {
                    mountError = sourceQuery.lastError().text();
                } else {
                    bool transactionOk = _db->transaction();
                    while(transactionOk && sourceQuery.next()) {
                        QString name = sourceQuery.value("name").toString();
                        const quint64 sourceSetID = sourceQuery.value("setID").toULongLong();

                        QSqlQuery alreadyMountedQuery(*_db);
                        alreadyMountedQuery.prepare(QStringLiteral(
                            "SELECT setID FROM ExternalTileSets WHERE sourceSetID = ? AND path = ?"));
                        alreadyMountedQuery.addBindValue(QVariant::fromValue<qulonglong>(sourceSetID));
                        alreadyMountedQuery.addBindValue(sourcePath);
                        if(alreadyMountedQuery.exec() && alreadyMountedQuery.next()) {
                            ++mountedSetCount;
                            continue;
                        }

                        quint64 existingSetID = 0;
                        if(_findTileSetID(name, existingSetID)) {
                            QSqlQuery existingQuery(*_db);
                            existingQuery.prepare(QStringLiteral(
                                "SELECT "
                                "(SELECT COUNT(*) FROM SetTiles WHERE setID = ?) + "
                                "(SELECT COUNT(*) FROM TilesDownload WHERE setID = ?) + "
                                "(SELECT COUNT(*) FROM ExternalTileSets WHERE setID = ?)"));
                            existingQuery.addBindValue(QVariant::fromValue<qulonglong>(existingSetID));
                            existingQuery.addBindValue(QVariant::fromValue<qulonglong>(existingSetID));
                            existingQuery.addBindValue(QVariant::fromValue<qulonglong>(existingSetID));
                            const bool emptyPlaceholder = existingQuery.exec()
                                    && existingQuery.next()
                                    && existingQuery.value(0).toULongLong() == 0;
                            if(emptyPlaceholder) {
                                QSqlQuery cleanupQuery(*_db);
                                cleanupQuery.prepare(QStringLiteral(
                                    "DELETE FROM TileSets WHERE setID = ? AND defaultSet = 0"));
                                cleanupQuery.addBindValue(QVariant::fromValue<qulonglong>(existingSetID));
                                transactionOk = cleanupQuery.exec();
                            } else {
                                int testCount = 0;
                                while(true) {
                                    const QString testName = QString::asprintf(
                                                "%s %02d", name.toLatin1().data(), ++testCount);
                                    quint64 unusedSetID = 0;
                                    if(!_findTileSetID(testName, unusedSetID) || testCount > 99) {
                                        name = testName;
                                        break;
                                    }
                                }
                            }
                        }
                        if(!transactionOk) {
                            break;
                        }

                        QSqlQuery insertQuery(*_db);
                        insertQuery.prepare(QStringLiteral(
                            "INSERT INTO TileSets("
                            "name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, "
                            "minZoom, maxZoom, type, numTiles, defaultSet, date"
                            ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?)"));
                        insertQuery.addBindValue(name);
                        insertQuery.addBindValue(sourceQuery.value("typeStr"));
                        insertQuery.addBindValue(sourceQuery.value("topleftLat"));
                        insertQuery.addBindValue(sourceQuery.value("topleftLon"));
                        insertQuery.addBindValue(sourceQuery.value("bottomRightLat"));
                        insertQuery.addBindValue(sourceQuery.value("bottomRightLon"));
                        insertQuery.addBindValue(sourceQuery.value("minZoom"));
                        insertQuery.addBindValue(sourceQuery.value("maxZoom"));
                        insertQuery.addBindValue(sourceQuery.value("type"));
                        insertQuery.addBindValue(sourceQuery.value("numTiles"));
                        insertQuery.addBindValue(QDateTime::currentDateTime().toTime_t());
                        if(!insertQuery.exec()) {
                            mountError = insertQuery.lastError().text();
                            transactionOk = false;
                            break;
                        }
                        const quint64 destinationSetID = insertQuery.lastInsertId().toULongLong();
                        QSqlQuery pathQuery(*_db);
                        pathQuery.prepare(QStringLiteral(
                            "INSERT INTO ExternalTileSets(setID, sourceSetID, path) VALUES(?, ?, ?)"));
                        pathQuery.addBindValue(QVariant::fromValue<qulonglong>(destinationSetID));
                        pathQuery.addBindValue(QVariant::fromValue<qulonglong>(sourceSetID));
                        pathQuery.addBindValue(sourcePath);
                        if(!pathQuery.exec()) {
                            mountError = pathQuery.lastError().text();
                            transactionOk = false;
                            break;
                        }
                        ++mountedSetCount;
                        qInfo().noquote() << "[OfflineMapDebug] import MOUNT SET"
                                          << "name=" << name
                                          << "sourceSetId=" << sourceSetID
                                          << "destinationSetId=" << destinationSetID
                                          << "path=" << sourcePath;
                    }
                    if(transactionOk && mountedSetCount > 0) {
                        mounted = _db->commit();
                        if(!mounted) {
                            mountError = _db->lastError().text();
                        }
                    } else {
                        _db->rollback();
                    }
                }
                dbImport.close();
            }
        }
        QSqlDatabase::removeDatabase(mountSession);
        closeExternalSource(importSource);
        if(mounted) {
            _connectExternalDatabases();
            task->setProgress(100);
            qInfo().noquote() << "[OfflineMapDebug] import MOUNT FINISHED"
                              << "path=" << sourcePath
                              << "sets=" << mountedSetCount
                              << "tiles=" << sourceTileCount;
        } else {
            qInfo().noquote() << "[OfflineMapDebug] import MOUNT FAILED"
                              << "path=" << sourcePath
                              << "error=" << mountError;
            task->setError(QStringLiteral("Error mounting external map database: %1").arg(mountError));
        }
        task->setImportCompleted();
        return;
    }
    closeExternalSource(importSource);
    //-- If replacing, simply copy over it
    if(task->replace()) {
        //-- Close and delete old database
        _disconnectDB();
        QFile file(_databasePath);
        const bool removed = !file.exists() || file.remove();
        //-- Copy given database
        const bool copied = removed && QFile::copy(task->path(), _databasePath);
        qInfo().noquote() << "[OfflineMapDebug] import REPLACE"
                          << "removedOld=" << removed
                          << "copied=" << copied
                          << "destination=" << _databasePath;
        if(!copied) {
            task->setError(QStringLiteral("Error copying imported database to map cache"));
        }
        task->setProgress(25);
        _init();
        if(_valid) {
            task->setProgress(50);
            _connectDB();
        }
        task->setProgress(100);
    } else {
        //-- Open imported set
        QSqlDatabase* dbImport = new QSqlDatabase(QSqlDatabase::addDatabase("QSQLITE", kExportSession));
        dbImport->setDatabaseName(task->path());
        dbImport->setConnectOptions("QSQLITE_ENABLE_SHARED_CACHE");
        if (dbImport->open()) {
            qInfo().noquote() << "[OfflineMapDebug] import database OPENED"
                              << "path=" << task->path();
            QSqlQuery query(*dbImport);
            //-- Prepare progress report
            quint64 tileCount = 0;
            quint64 currentCount = 0;
            int lastProgress = -1;
            QString s;
            s = QString("SELECT COUNT(tileID) FROM Tiles");
            if(query.exec(s)) {
                if(query.next()) {
                    //-- Total number of tiles in imported database
                    tileCount  = query.value(0).toULongLong();
                }
            }
            qInfo().noquote() << "[OfflineMapDebug] import source tile count=" << tileCount;
            if(tileCount) {
                //-- Iterate Tile Sets
                s = QString("SELECT * FROM TileSets ORDER BY defaultSet DESC, name ASC");
                if(query.exec(s)) {
                    while(query.next()) {
                        QString name            = query.value("name").toString();
                        quint64 setID           = query.value("setID").toULongLong();
                        QString mapType         = query.value("typeStr").toString();
                        double  topleftLat      = query.value("topleftLat").toDouble();
                        double  topleftLon      = query.value("topleftLon").toDouble();
                        double  bottomRightLat  = query.value("bottomRightLat").toDouble();
                        double  bottomRightLon  = query.value("bottomRightLon").toDouble();
                        int     minZoom         = query.value("minZoom").toInt();
                        int     maxZoom         = query.value("maxZoom").toInt();
                        int     type            = query.value("type").toInt();
                        quint32 numTiles        = query.value("numTiles").toUInt();
                        int     defaultSet      = query.value("defaultSet").toInt();
                        quint64 insertSetID     = _getDefaultTileSet();
                        qInfo().noquote() << "[OfflineMapDebug] import tile set"
                                          << "name=" << name
                                          << "sourceSetId=" << setID
                                          << "typeStr=" << mapType
                                          << "typeId=" << type
                                          << "bounds=" << topleftLat << topleftLon << bottomRightLat << bottomRightLon
                                          << "zoom=" << minZoom << ".." << maxZoom
                                          << "declaredTiles=" << numTiles
                                          << "defaultSet=" << defaultSet;
                        //-- If not default set, create new one
                        if(!defaultSet) {
                            //-- Check if we have this tile set already
                            if(_findTileSetID(name, insertSetID)) {
                                int testCount = 0;
                                //-- Set with this name already exists. Make name unique.
                                while (true) {
                                    auto testName = QString::asprintf("%s %02d", name.toLatin1().data(), ++testCount);
                                    if(!_findTileSetID(testName, insertSetID) || testCount > 99) {
                                        name = testName;
                                        break;
                                    }
                                }
                            }
                            //-- Create new set
                            QSqlQuery cQuery(*_db);
                            cQuery.prepare("INSERT INTO TileSets("
                                "name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, defaultSet, date"
                                ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
                            cQuery.addBindValue(name);
                            cQuery.addBindValue(mapType);
                            cQuery.addBindValue(topleftLat);
                            cQuery.addBindValue(topleftLon);
                            cQuery.addBindValue(bottomRightLat);
                            cQuery.addBindValue(bottomRightLon);
                            cQuery.addBindValue(minZoom);
                            cQuery.addBindValue(maxZoom);
                            cQuery.addBindValue(type);
                            cQuery.addBindValue(numTiles);
                            cQuery.addBindValue(defaultSet);
                            cQuery.addBindValue(QDateTime::currentDateTime().toTime_t());
                            if(!cQuery.exec()) {
                                task->setError("Error adding imported tile set to database");
                                break;
                            } else {
                                //-- Get just created (auto-incremented) setID
                                insertSetID = cQuery.lastInsertId().toULongLong();
                                qInfo().noquote() << "[OfflineMapDebug] import destination set"
                                                  << "name=" << name
                                                  << "destinationSetId=" << insertSetID;
                            }
                        }
                        //-- Find set tiles
                        QSqlQuery cQuery(*_db);
                        QSqlQuery subQuery(*dbImport);
                        QString sb = QString("SELECT * FROM Tiles WHERE tileID IN (SELECT A.tileID FROM SetTiles A JOIN SetTiles B ON A.tileID = B.tileID WHERE B.setID = %1 GROUP BY A.tileID HAVING COUNT(A.tileID) = 1)").arg(setID);
                        if(subQuery.exec(sb)) {
                            quint64 tilesFound = 0;
                            quint64 tilesSaved = 0;
                            _db->transaction();
                            while(subQuery.next()) {
                                tilesFound++;
                                QString hash    = subQuery.value("hash").toString();
                                QString format  = subQuery.value("format").toString();
                                QByteArray img  = subQuery.value("tile").toByteArray();
                                int type        = subQuery.value("type").toInt();
                                //-- Save tile
                                cQuery.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
                                cQuery.addBindValue(hash);
                                cQuery.addBindValue(format);
                                cQuery.addBindValue(img);
                                cQuery.addBindValue(img.size());
                                cQuery.addBindValue(type);
                                cQuery.addBindValue(QDateTime::currentDateTime().toTime_t());
                                if(cQuery.exec()) {
                                    tilesSaved++;
                                    quint64 importTileID = cQuery.lastInsertId().toULongLong();
                                    QString s = QString("INSERT INTO SetTiles(tileID, setID) VALUES(%1, %2)").arg(importTileID).arg(insertSetID);
                                    cQuery.prepare(s);
                                    cQuery.exec();
                                    currentCount++;
                                    if(tileCount) {
                                        int progress = (int)((double)currentCount / (double)tileCount * 100.0);
                                        //-- Avoid calling this if (int) progress hasn't changed.
                                        if(lastProgress != progress) {
                                            lastProgress = progress;
                                            task->setProgress(progress);
                                        }
                                    }
                                }
                            }
                            const bool committed = _db->commit();
                            qInfo().noquote() << "[OfflineMapDebug] import tile set RESULT"
                                              << "name=" << name
                                              << "destinationSetId=" << insertSetID
                                              << "tilesFound=" << tilesFound
                                              << "tilesSaved=" << tilesSaved
                                              << "duplicates=" << (tilesFound - tilesSaved)
                                              << "committed=" << committed;
                            if(tilesSaved) {
                                //-- Update tile count (if any added)
                                s = QString("SELECT COUNT(size) FROM Tiles A INNER JOIN SetTiles B on A.tileID = B.tileID WHERE B.setID = %1").arg(insertSetID);
                                if(cQuery.exec(s)) {
                                    if(cQuery.next()) {
                                        quint64 count  = cQuery.value(0).toULongLong();
                                        s = QString("UPDATE TileSets SET numTiles = %1 WHERE setID = %2").arg(count).arg(insertSetID);
                                        cQuery.exec(s);
                                    }
                                }
                            }
                            qint64 uniqueTiles = tilesFound - tilesSaved;
                            if((quint64)uniqueTiles < tileCount) {
                                tileCount -= uniqueTiles;
                            } else {
                                tileCount = 0;
                            }
                            //-- If there was nothing new in this set, remove it.
                            if(!tilesSaved && !defaultSet) {
                                qCDebug(QGCTileCacheLog) << "No unique tiles in" << name << "Removing it.";
                                _deleteTileSet(insertSetID);
                            }
                        }
                    }
                } else {
                    task->setError("No tile set in database");
                }
            }
            delete dbImport;
            QSqlDatabase::removeDatabase(kExportSession);
            if(!tileCount) {
                task->setError("No unique tiles in imported database");
            }
        } else {
            qInfo().noquote() << "[OfflineMapDebug] import database OPEN FAILED"
                              << "path=" << task->path()
                              << "error=" << dbImport->lastError().text();
            task->setError("Error opening import database");
        }
    }
    qInfo().noquote() << "[OfflineMapDebug] import FINISHED"
                      << "path=" << task->path();
    task->setImportCompleted();
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_exportSets(QGCMapTask* mtask)
{
    if(!_testTask(mtask)) {
        return;
    }
    QGCExportTileTask* task = static_cast<QGCExportTileTask*>(mtask);
    //-- Delete target if it exists
    QFile file(task->path());
    file.remove();
    //-- Create exported database
    QScopedPointer<QSqlDatabase> dbExport(new QSqlDatabase(QSqlDatabase::addDatabase("QSQLITE", kExportSession)));
    dbExport->setDatabaseName(task->path());
    dbExport->setConnectOptions("QSQLITE_ENABLE_SHARED_CACHE");
    if (dbExport->open()) {
        if(_createDB(*dbExport, false)) {
            //-- Prepare progress report
            quint64 tileCount = 0;
            quint64 currentCount = 0;
            for(int i = 0; i < task->sets().count(); i++) {
                QGCCachedTileSet* set = task->sets()[i];
                //-- Default set has no unique tiles
                if(set->defaultSet()) {
                    tileCount += set->totalTileCount();
                } else {
                    tileCount += set->uniqueTileCount();
                }
            }
            if(!tileCount) {
                tileCount = 1;
            }
            //-- Iterate sets to save
            for(int i = 0; i < task->sets().count(); i++) {
                QGCCachedTileSet* set = task->sets()[i];
                //-- Create Tile Exported Set
                QSqlQuery exportQuery(*dbExport);
                exportQuery.prepare("INSERT INTO TileSets("
                    "name, typeStr, topleftLat, topleftLon, bottomRightLat, bottomRightLon, minZoom, maxZoom, type, numTiles, defaultSet, date"
                    ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
                exportQuery.addBindValue(set->name());
                exportQuery.addBindValue(set->mapTypeStr());
                exportQuery.addBindValue(set->topleftLat());
                exportQuery.addBindValue(set->topleftLon());
                exportQuery.addBindValue(set->bottomRightLat());
                exportQuery.addBindValue(set->bottomRightLon());
                exportQuery.addBindValue(set->minZoom());
                exportQuery.addBindValue(set->maxZoom());
                exportQuery.addBindValue(getQGCMapEngine()->urlFactory()->getIdFromType(set->type()));
                exportQuery.addBindValue(set->totalTileCount());
                exportQuery.addBindValue(set->defaultSet());
                exportQuery.addBindValue(QDateTime::currentDateTime().toTime_t());
                if(!exportQuery.exec()) {
                    task->setError("Error adding tile set to exported database");
                    break;
                } else {
                    //-- Get just created (auto-incremented) setID
                    quint64 exportSetID = exportQuery.lastInsertId().toULongLong();
                    //-- Find set tiles
                    QString s = QString("SELECT * FROM SetTiles WHERE setID = %1").arg(set->id());
                    QSqlQuery query(*_db);
                    if(query.exec(s)) {
                        dbExport->transaction();
                        while(query.next()) {
                            quint64 tileID = query.value("tileID").toULongLong();
                            //-- Get tile
                            QString s = QString("SELECT * FROM Tiles WHERE tileID = \"%1\"").arg(tileID);
                            QSqlQuery subQuery(*_db);
                            if(subQuery.exec(s)) {
                                if(subQuery.next()) {
                                    QString hash    = subQuery.value("hash").toString();
                                    QString format  = subQuery.value("format").toString();
                                    QByteArray img  = subQuery.value("tile").toByteArray();
                                    int type        = subQuery.value("type").toInt();
                                    //-- Save tile
                                    exportQuery.prepare("INSERT INTO Tiles(hash, format, tile, size, type, date) VALUES(?, ?, ?, ?, ?, ?)");
                                    exportQuery.addBindValue(hash);
                                    exportQuery.addBindValue(format);
                                    exportQuery.addBindValue(img);
                                    exportQuery.addBindValue(img.size());
                                    exportQuery.addBindValue(type);
                                    exportQuery.addBindValue(QDateTime::currentDateTime().toTime_t());
                                    if(exportQuery.exec()) {
                                        quint64 exportTileID = exportQuery.lastInsertId().toULongLong();
                                        QString s = QString("INSERT INTO SetTiles(tileID, setID) VALUES(%1, %2)").arg(exportTileID).arg(exportSetID);
                                        exportQuery.prepare(s);
                                        exportQuery.exec();
                                        currentCount++;
                                        task->setProgress((int)((double)currentCount / (double)tileCount * 100.0));
                                    }
                                }
                            }
                        }
                    }
                    dbExport->commit();
                }
            }
        } else {
            task->setError("Error creating export database");
        }
    } else {
        qCritical() << "Map Cache SQL error (create export database):" << dbExport->lastError();
        task->setError("Error opening export database");
    }
    dbExport.reset();
    QSqlDatabase::removeDatabase(kExportSession);
    task->setExportCompleted();
}

//-----------------------------------------------------------------------------
bool QGCCacheWorker::_testTask(QGCMapTask* mtask)
{
    if(!_valid) {
        mtask->setError("No Cache Database");
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
bool
QGCCacheWorker::_init()
{
    _failed = false;
    if(!_databasePath.isEmpty()) {
        qCDebug(QGCTileCacheLog) << "Mapping cache directory:" << _databasePath;
        //-- Initialize Database
        if (_connectDB()) {
            _valid = _createDB(*_db);
            if(!_valid) {
                _failed = true;
            }
        } else {
            qCritical() << "Map Cache SQL error (init() open db):" << _db->lastError();
            _failed = true;
        }
        _disconnectDB();
    } else {
        qCritical() << "Could not find suitable cache directory.";
        _failed = true;
    }
    _testInternet();
    return _failed;
}

//-----------------------------------------------------------------------------
bool
QGCCacheWorker::_connectDB()
{
    _db.reset(new QSqlDatabase(QSqlDatabase::addDatabase("QSQLITE", kSession)));
    _db->setDatabaseName(_databasePath);
    _db->setConnectOptions("QSQLITE_ENABLE_SHARED_CACHE");
    _valid = _db->open();
    if(_valid) {
        _connectExternalDatabases();
    }
    return _valid;
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_connectExternalDatabases()
{
    _disconnectExternalDatabases();
    if(!_db || !_db->isOpen()) {
        return;
    }
    QSqlQuery query(*_db);
    if(!query.exec(QStringLiteral("SELECT DISTINCT path FROM ExternalTileSets ORDER BY path"))) {
        // The table does not exist during the first connection to a new cache.
        return;
    }
    int index = 0;
    while(query.next()) {
        const QString path = query.value(0).toString();
        ExternalSourceHandle source = openExternalSource(path);
        if(!source.isValid()) {
            qWarning().noquote() << "External offline map is unavailable:" << path;
            continue;
        }
        const QString connectionName = QStringLiteral("%1_%2")
                .arg(kExternalSessionPrefix)
                .arg(++index);
        bool opened = false;
        QString openError;
        {
            QSqlDatabase externalDb = QSqlDatabase::addDatabase("QSQLITE", connectionName);
            externalDb.setDatabaseName(source.databasePath);
            externalDb.setConnectOptions(source.databasePathIsUri
                                         ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_OPEN_URI")
                                         : QStringLiteral("QSQLITE_OPEN_READONLY"));
            opened = externalDb.open();
            if(opened) {
                QGCExternalTileDatabase external;
                external.path = path;
                external.connectionName = connectionName;
                external.database = externalDb;
                external.size = source.size;
                external.fileDescriptor = source.fileDescriptor;
                source.fileDescriptor = -1;
                _externalDatabases.append(external);
                qInfo().noquote() << "[OfflineMapDebug] external database MOUNTED"
                                  << "path=" << path
                                  << "bytes=" << external.size
                                  << "contentDocument=" << AndroidDocumentUtils::isContentUri(path);
            } else {
                const QSqlError error = externalDb.lastError();
                openError = QStringLiteral("%1 (native code %2)")
                        .arg(error.text(), error.nativeErrorCode());
            }
        }
        if(!opened) {
            QSqlDatabase::removeDatabase(connectionName);
            qWarning().noquote() << "External offline map could not be opened:"
                                 << path
                                 << "sqliteSource=" << source.databasePath
                                 << "error=" << openError;
        }
        closeExternalSource(source);
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_disconnectExternalDatabases()
{
    QStringList connectionNames;
    QList<int> fileDescriptors;
    for(QGCExternalTileDatabase& external : _externalDatabases) {
        connectionNames.append(external.connectionName);
        fileDescriptors.append(external.fileDescriptor);
        external.database.close();
        external.database = QSqlDatabase();
    }
    _externalDatabases.clear();
    for(const QString& connectionName : connectionNames) {
        QSqlDatabase::removeDatabase(connectionName);
    }
    for(const int fileDescriptor : fileDescriptors) {
        AndroidDocumentUtils::closeFileDescriptor(fileDescriptor);
    }
}

//-----------------------------------------------------------------------------
bool
QGCCacheWorker::_createDB(QSqlDatabase& db, bool createDefault)
{
    bool res = false;
    QSqlQuery query(db);
    if(!query.exec(
        "CREATE TABLE IF NOT EXISTS Tiles ("
        "tileID INTEGER PRIMARY KEY NOT NULL, "
        "hash TEXT NOT NULL UNIQUE, "
        "format TEXT NOT NULL, "
        "tile BLOB NULL, "
        "size INTEGER, "
        "type INTEGER, "
        "date INTEGER DEFAULT 0)"))
    {
        qWarning() << "Map Cache SQL error (create Tiles db):" << query.lastError().text();
    } else {
        query.exec("CREATE INDEX IF NOT EXISTS hash ON Tiles ( hash, size, type ) ");
             
        if(!query.exec(
            "CREATE TABLE IF NOT EXISTS TileSets ("
            "setID INTEGER PRIMARY KEY NOT NULL, "
            "name TEXT NOT NULL UNIQUE, "
            "typeStr TEXT, "
            "topleftLat REAL DEFAULT 0.0, "
            "topleftLon REAL DEFAULT 0.0, "
            "bottomRightLat REAL DEFAULT 0.0, "
            "bottomRightLon REAL DEFAULT 0.0, "
            "minZoom INTEGER DEFAULT 3, "
            "maxZoom INTEGER DEFAULT 3, "
            "type INTEGER DEFAULT -1, "
            "numTiles INTEGER DEFAULT 0, "
            "defaultSet INTEGER DEFAULT 0, "
            "date INTEGER DEFAULT 0)"))
        {
            qWarning() << "Map Cache SQL error (create TileSets db):" << query.lastError().text();
        } else {
            if(!query.exec(
                "CREATE TABLE IF NOT EXISTS SetTiles ("
                "setID INTEGER, "
                "tileID INTEGER)"))
            {
                qWarning() << "Map Cache SQL error (create SetTiles db):" << query.lastError().text();
            } else {
                if(!query.exec(
                    "CREATE TABLE IF NOT EXISTS TilesDownload ("
                    "setID INTEGER, "
                    "hash TEXT NOT NULL UNIQUE, "
                    "type INTEGER, "
                    "x INTEGER, "
                    "y INTEGER, "
                    "z INTEGER, "
                    "state INTEGER DEFAULT 0)"))
                {
                    qWarning() << "Map Cache SQL error (create TilesDownload db):" << query.lastError().text();
                } else {
                    //-- Database it ready for use
                    res = true;
                }
            }
        }
    }
    if(res && !query.exec(
        "CREATE TABLE IF NOT EXISTS ExternalTileSets ("
        "setID INTEGER PRIMARY KEY NOT NULL, "
        "sourceSetID INTEGER NOT NULL, "
        "path TEXT NOT NULL)"))
    {
        qWarning() << "Map Cache SQL error (create ExternalTileSets db):" << query.lastError().text();
        res = false;
    }
    if(res) {
        query.exec("CREATE UNIQUE INDEX IF NOT EXISTS externalTileSetPath "
                   "ON ExternalTileSets(path, sourceSetID)");
    }
    //-- Create default tile set
    if(res && createDefault) {
        QString s = QString("SELECT name FROM TileSets WHERE name = \"%1\"").arg(kDefaultSet);
        if(query.exec(s)) {
            if(!query.next()) {
                query.prepare("INSERT INTO TileSets(name, defaultSet, date) VALUES(?, ?, ?)");
                query.addBindValue(kDefaultSet);
                query.addBindValue(1);
                query.addBindValue(QDateTime::currentDateTime().toTime_t());
                if(!query.exec()) {
                    qWarning() << "Map Cache SQL error (Creating default tile set):" << db.lastError();
                    res = false;
                }
            }
        } else {
            qWarning() << "Map Cache SQL error (Looking for default tile set):" << db.lastError();
        }
    }
    if(!res) {
        QFile file(_databasePath);
        file.remove();
    }
    return res;
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_disconnectDB()
{
    _disconnectExternalDatabases();
    if (_db) {
        _db.reset();
        QSqlDatabase::removeDatabase(kSession);
    }
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_testInternet()
{
    /*
        To test if you have Internet connection, the code tests a connection to
        8.8.8.8:53 (google DNS). It appears that some routers are now blocking TCP
        connections to port 53. So instead, we use a TCP connection to "github.com"
        (80). On exit, if the look up for "github.com" is under way, a call to abort
        the lookup is made. This abort call on Android has no effect, and the code
        blocks for a full minute. So to work around the issue, we continue a direct
        TCP connection to 8.8.8.8:53 on Android and do the lookup/connect on the
        other platforms.
    */
#if defined(__android__)
    QTcpSocket socket;
    socket.connectToHost("8.8.8.8", 53);
    if (socket.waitForConnected(2000)) {
        qCDebug(QGCTileCacheLog) << "Yes Internet Access";
        emit internetStatus(true);
        return;
    }
    qWarning() << "No Internet Access";
    emit internetStatus(false);
#else
    if(!_hostLookupID) {
        _hostLookupID = QHostInfo::lookupHost("www.github.com", this, &QGCCacheWorker::_lookupReady);
    }
#endif
}

//-----------------------------------------------------------------------------
void
QGCCacheWorker::_lookupReady(QHostInfo info)
{
#if defined(__android__)
    Q_UNUSED(info);
#else
    _hostLookupID = 0;
    if(info.error() == QHostInfo::NoError && info.addresses().size()) {
        auto socket = new QTcpSocket();
        QNetworkProxy tempProxy;
        tempProxy.setType(QNetworkProxy::DefaultProxy);
        socket->setProxy(tempProxy);
        socket->connectToHost(info.addresses().first(), 80);
        connect(socket, &QTcpSocket::connected, this, [this, socket] {
            qCDebug(QGCTileCacheLog) << "Yes Internet Access";
            emit internetStatus(true);
            socket->deleteLater();
        });
        connect(socket, &QAbstractSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
            qCDebug(QGCTileCacheLog) << "No internet connection, reason:" << error;
            emit internetStatus(false);
            socket->deleteLater();
        });
    } else {
        qDebug(QGCTileCacheLog) << "No Internet Access";
        emit internetStatus(false);
    }
#endif
}
