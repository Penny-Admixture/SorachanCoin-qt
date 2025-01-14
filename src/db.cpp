// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2021 The SorachanCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <db.h>
#include <net.h>
#include <util.h>
#include <main.h>
#include <ui_interface.h>
#include <init.h>
#include <file_operate/fs.h>
#include <boost/filesystem/fstream.hpp>
#include <util/time.h>

#ifndef WIN32
# include "sys/stat.h"
#endif

CCriticalSection CDBEnv::cs_db;
CCriticalSection CLevelDBEnv::cs_leveldb;
CCriticalSection CSqliteDBEnv::cs_sqlite;

static CCriticalSection cs_w_update;
static unsigned int nWalletDBUpdated = 0;
void dbparam::IncWalletUpdate() {
    LOCK(cs_w_update);
    ++nWalletDBUpdated;
}

unsigned int dbparam::GetWalletUpdate() {
    LOCK(cs_w_update);
    return nWalletDBUpdated;
}

// SorachanCoin: CDB only use wallet.dat
bool dbparam::IsChainFile(std::string strFile) {
    //debugcs::instance() << "dbparam::InChainFile: " << strFile.c_str() << debugcs::endl();
    //util::Sleep(5000);
    assert(strFile != "blkindex.dat");
    return (strFile == "blkindex.dat");
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CDBEnv class
//////////////////////////////////////////////////////////////////////////////////////////////

void CDBEnv::EnvShutdown() {
    LOCK(cs_db);
    if (! fDbEnvInit)
        return;

    fDbEnvInit = false;
    int ret = dbenv.close(0);
    if (ret != 0)
        logging::LogPrintf("EnvShutdown exception: %s (%d)\n", DbEnv::strerror(ret), ret);
    if (! fMockDb) {
        int ret = DbEnv(0).remove(pathEnv.string().c_str(), 0);
        if(ret > 0)
            throw std::runtime_error("EnvShutdown database remove failure");
    }
}

CDBEnv::CDBEnv() : fDetachDB(false), fDbEnvInit(false), dbenv(DB_CXX_NO_EXCEPTIONS) {}
CDBEnv::~CDBEnv() {
    EnvShutdown();
}

void CDBEnv::Close() {
    EnvShutdown();
}
bool CDBEnv::TxnCheckPoint(uint32_t kbyte, uint32_t min) {
    LOCK(cs_db);
    return dbenv.txn_checkpoint(kbyte, min, 0)==0;
}
bool CDBEnv::Remove(const std::string &strFile) {
    LOCK(cs_db);
    Db db(&dbenv, 0);
    return db.remove(strFile.c_str(), nullptr, 0)==0;
}
bool CDBEnv::Rename(const std::string &strFileRes, const std::string &strFile) {
    LOCK(cs_db);
    Db db(&dbenv, 0);
    return db.rename(strFileRes.c_str(), nullptr, strFile.c_str(), 0)==0;
}
bool CDBEnv::DbRename(const std::string &filename, const std::string &newFilename) {
    LOCK(cs_db);
    return dbenv.dbrename(nullptr, filename.c_str(), nullptr, newFilename.c_str(), DB_AUTO_COMMIT)==0;
}

bool CDBEnv::Open(fs::path pathEnv_) {
    LOCK(cs_db);
    if (fDbEnvInit)
        return true;
    if (args_bool::fShutdown)
        return false;

    // create directory and db.log
    pathEnv = pathEnv_;
    const fs::path pathDataDir = pathEnv;
    const fs::path pathLogDir = pathDataDir / "database";
    if(! fsbridge::dir_create(pathLogDir))
        return false;

    const fs::path pathErrorFile = pathDataDir / "db.log";
    logging::LogPrintf("dbenv.open LogDir=%s ErrorFile=%s\n", pathLogDir.string().c_str(), pathErrorFile.string().c_str());

    unsigned int nEnvFlags = 0;
    if (map_arg::GetBoolArg("-privdb", true))
        nEnvFlags |= DB_PRIVATE;

    int nDbCache = map_arg::GetArgInt("-dbcache", dbcache_size);
    dbenv.set_lg_dir(pathLogDir.string().c_str());
    dbenv.set_cachesize(nDbCache / 1024, (nDbCache % 1024) * 1048576, 1);
    dbenv.set_lg_bsize(1048576);
    dbenv.set_lg_max(10485760);

    // Bugfix: Bump lk_max_locks default to 537000, to safely handle reorgs with up to 5 blocks reversed
    // dbenv.set_lk_max_locks(10000);
    dbenv.set_lk_max_locks(537000);

    dbenv.set_lk_max_objects(10000);
    dbenv.set_errfile(fopen(pathErrorFile.string().c_str(), "a")); /// debug
    dbenv.set_flags(DB_AUTO_COMMIT, 1);
    dbenv.set_flags(DB_TXN_WRITE_NOSYNC, 1);

#ifdef DB_LOG_AUTO_REMOVE
    dbenv.log_set_config(DB_LOG_AUTO_REMOVE, 1);
#endif

    int ret = dbenv.open(pathEnv.string().c_str(),
        DB_CREATE |
        DB_INIT_LOCK |
        DB_INIT_LOG |
        DB_INIT_MPOOL |
        DB_INIT_TXN |
        DB_THREAD |
        DB_RECOVER |
        nEnvFlags,
        S_IRUSR | S_IWUSR);
    if (ret != 0) {
        return logging::error("CDB() : error %s (%d) opening database environment", DbEnv::strerror(ret), ret);
    }

    fDbEnvInit = true;
    //fMockDb = false;

    return true;
}

/*
void CDBEnv::MakeMock()
{
    if (fDbEnvInit)
        throw std::runtime_error("CDBEnv::MakeMock(): already initialized");
    if (args_bool::fShutdown)
        throw std::runtime_error("CDBEnv::MakeMock(): during shutdown");

    logging::LogPrintf("CDBEnv::MakeMock()\n");

    dbenv.set_cachesize(1, 0, 1);
    dbenv.set_lg_bsize(10485760 * 4);
    dbenv.set_lg_max(10485760);
    dbenv.set_lk_max_locks(10000);
    dbenv.set_lk_max_objects(10000);
    dbenv.set_flags(DB_AUTO_COMMIT, 1);
#ifdef DB_LOG_IN_MEMORY
    dbenv.log_set_config(DB_LOG_IN_MEMORY, args_bool::fUseMemoryLog ? 1 : 0);
#endif
    int ret = dbenv.open(nullptr,
        DB_CREATE |
        DB_INIT_LOCK |
        DB_INIT_LOG |
        DB_INIT_MPOOL |
        DB_INIT_TXN |
        DB_THREAD |
        DB_PRIVATE,
        S_IRUSR | S_IWUSR);
    if (ret > 0)
        throw std::runtime_error(tfm::format("CDBEnv::MakeMock(): error %d opening database environment", ret));

    fDbEnvInit = true;
    fMockDb = true;
}
*/

CDBEnv::VerifyResult CDBEnv::Verify(std::string strFile, bool (*recoverFunc)(std::string strFile, bool fOnlyKeys))
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);

    Db db(&dbenv, 0);
    int result = db.verify(strFile.c_str(), nullptr, nullptr, 0);
    if (result == 0)
        return VERIFY_OK;
    else if (recoverFunc == nullptr)
        return RECOVER_FAIL;

    // Try to recover:
    bool fRecovered = (*recoverFunc)(strFile, false);
    return (fRecovered ? RECOVER_OK : RECOVER_FAIL);
}

bool CDBEnv::Salvage(std::string strFile, bool fAggressive, std::vector<CDBEnv::KeyValPair> &vResult)
{
    LOCK(cs_db);
    assert(mapFileUseCount.count(strFile) == 0);

    u_int32_t flags = DB_SALVAGE;
    if (fAggressive) { flags |= DB_AGGRESSIVE; }

    std::stringstream strDump;

    Db db(&dbenv, 0);
    int result = db.verify(strFile.c_str(), nullptr, &strDump, flags);
    if (result != 0) {
        logging::LogPrintf("ERROR: db salvage failed\n");
        return false;
    }

    //
    // Format of bdb dump is ascii lines:
    // header lines...
    // HEADER=END
    // hexadecimal key
    // hexadecimal value
    // ... repeated
    // DATA=END
    //

    std::string strLine;
    while (!strDump.eof() && strLine != "HEADER=END") {
        std::getline(strDump, strLine); // Skip past header
    }

    std::string keyHex, valueHex;
    while (!strDump.eof() && keyHex != "DATA=END") {
        std::getline(strDump, keyHex);
        if (keyHex != "DATA=END") {
            std::getline(strDump, valueHex);
            vResult.push_back(std::make_pair(strenc::ParseHex(keyHex), strenc::ParseHex(valueHex)));
        }
    }

    return (result == 0);
}


void CDBEnv::CheckpointLSN(std::string strFile)
{
    dbenv.txn_checkpoint(0, 0, 0);
    if (fMockDb) {
        return;
    }
    dbenv.lsn_reset(strFile.c_str(), 0);
}

std::unique_ptr<Db> CDBEnv::TempCreate(DbTxn *txnid, const std::string &strFile, unsigned int nFlags) {
    std::unique_ptr<Db> pdb(new(std::nothrow) Db(&dbenv, 0));
    if(pdb) {
        int ret = pdb->open(txnid,                // Txn pointer
                            strFile.c_str(),      // Filename
                            "main",               // Logical db name
                            DB_BTREE,             // Database type
                            nFlags,               // Flags
                            0);
        if(ret > 0) {
            pdb.reset(); // release and nullptr
            assert(pdb.get()==nullptr);
        }
    }
    return std::move(pdb);
}

Db *CDBEnv::Create(const std::string &strFile, unsigned int nFlags)
{
    LOCK(cs_db);
    if (! Open(iofs::GetDataDir())) {
        if(args_bool::fShutdown)
            return nullptr;
        else
            throw std::runtime_error("CDBEnv::bitdb : failed to open file");
    }

    IncUseCount(strFile);
    Db *&pdb = getDb(strFile);
    if (pdb == nullptr) {
        pdb = new(std::nothrow) Db(&dbenv, 0);
        if (pdb == nullptr)
            throw std::runtime_error("CDB() : failed to allocate memory");

        /*
        bool fMockDb = IsMock();
        if (fMockDb) {
            DbMpoolFile *mpf = pdb->get_mpf();
            int ret = mpf->set_flags(DB_MPOOL_NOFILE, 1);
            if (ret != 0)
                throw std::runtime_error(tfm::format("CDB() : failed to configure for no temp file backing for database %s", strFile.c_str()));
        }
        */

        for (int cc = 0; cc < retry_counter; ++cc) {
            int ret = pdb->open(nullptr,             // Txn pointer
                fMockDb ? nullptr : strFile.c_str(), // Filename
                "main",                              // Logical db name
                DB_BTREE,                            // Database type
                nFlags,                              // Flags
                0);

            if (ret != 0) {
                if(cc < retry_counter - 1) {
                    util::Sleep(1000);
                    continue;
                }
                delete pdb;
                pdb = nullptr;
                DecUseCount(strFile);
                //strFile.clear();
                throw std::runtime_error(tfm::format("CDB() : can't open database file %s, error %d", strFile.c_str(), ret));
            } else {
                break;
            }
        }

        //setDb(strFile, pdb);
    }
    return pdb;
}

void CDBEnv::CloseDb(const std::string &strFile)
{
    {
        LOCK(cs_db);
        if (mapDb[strFile] != nullptr) {
            //
            // Close the database handle
            //
            Db *pdb = mapDb[strFile];
            pdb->close(0);
            delete pdb;
            mapDb[strFile] = nullptr;
        }
    }
}

bool CDBEnv::RemoveDb(const std::string &strFile)
{
    CloseDb(strFile);

    LOCK(cs_db);
    int rc = dbenv.dbremove(nullptr, strFile.c_str(), nullptr, DB_AUTO_COMMIT);
    return (rc == 0);
}

DbTxn *CDBEnv::TxnBegin(int flags /*= DB_TXN_WRITE_NOSYNC*/) {
    DbTxn *ptxn = nullptr;
    int ret = dbenv.txn_begin(nullptr, &ptxn, flags);
    if (!ptxn || ret != 0)
        return nullptr;

    return ptxn;
}

void CDBEnv::Flush(bool fShutdown)
{
    LOCK(cs_db);
    const int64_t nStart = util::GetTimeMillis();

    // Flush log data to the actual data file on all files that are not in use
    logging::LogPrintf("Flush(%s)%s\n", args_bool::fShutdown ? "true" : "false", fDbEnvInit ? "" : " db not started");
    if (! fDbEnvInit) {
        return;
    }

    {
        LOCK(cs_db);
        std::map<std::string, int>::iterator mi = mapFileUseCount.begin();
        while (mi != mapFileUseCount.end())
        {
            std::string strFile = (*mi).first;
            int nRefCount = (*mi).second;
            logging::LogPrintf("%s refcount=%d\n", strFile.c_str(), nRefCount);
            if (nRefCount == 0) {
                // Move log data to the dat file
                CloseDb(strFile);
                logging::LogPrintf("%s checkpoint\n", strFile.c_str());
                dbenv.txn_checkpoint(0, 0, 0);
                if (!dbparam::IsChainFile(strFile) || fDetachDB) {
                    logging::LogPrintf("%s detach\n", strFile.c_str());
                    if (!fMockDb) {
                        dbenv.lsn_reset(strFile.c_str(), 0);
                    }
                }
                logging::LogPrintf("%s closed\n", strFile.c_str());
                mapFileUseCount.erase(mi++);
            } else {
                ++mi;
            }
        }

        logging::LogPrintf("DBFlush(%s)%s ended %15" PRId64 "ms\n", args_bool::fShutdown ? "true" : "false", fDbEnvInit ? "" : " db not started", util::GetTimeMillis() - nStart);
        if (args_bool::fShutdown) {
            char **listp;
            if (mapFileUseCount.empty()) {
                dbenv.log_archive(&listp, DB_ARCH_REMOVE);
                Close();
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CLevelDBEnv class
//////////////////////////////////////////////////////////////////////////////////////////////

CLevelDBEnv::CLevelDBEnv(std::vector<std::string> instIn) : fLevelDbEnvInit(false), instance(instIn) {
    LOCK(cs_leveldb);
    this->options = CLevelDBEnv::GetOptions();
}

CLevelDBEnv::~CLevelDBEnv() {
    EnvShutdown();
}

void CLevelDBEnv::EnvShutdown() {
    LOCK(cs_leveldb);
    if(! fLevelDbEnvInit)
        return;

    for(auto &ite: lobj) {
        delete ite.second->ptxdb;
        delete ite.second;
    }
    lobj.clear();

    delete options.block_cache;
    options.block_cache = nullptr;

    delete options.filter_policy;
    options.filter_policy = nullptr;

    debugcs::instance() << "CLevelDBEnv::EnvShutdown() global instance all delete" << debugcs::endl();
}

leveldb::Options CLevelDBEnv::GetOptions() {
    LOCK(cs_leveldb);
    leveldb::Options options;
    const int nCacheSizeMB = map_arg::GetArgInt("-dbcache", IDBEnv::dbcache_size);

    options.block_cache = leveldb::NewLRUCache(nCacheSizeMB * 1048576);
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    if(!options.block_cache || !options.filter_policy)
        throw std::runtime_error("leveldb GetOptions(): failure");

    options.create_if_missing = true;
    return options;
}

bool CLevelDBEnv::Open(fs::path pathEnv_) {
    LOCK(cs_leveldb);
    if (fLevelDbEnvInit)
        return true;
    if (args_bool::fShutdown)
        return false;

    pathEnv = pathEnv_;
    for(size_t i=0; i<instance.size(); ++i) {
        // First time init.
        fs::path directory = pathEnv_ / instance[i];

        if(! fsbridge::dir_create(directory))
            throw std::runtime_error("CLevelDBEnv::Open(): dir create failure");

        leveldb_object *ptarget = new (std::nothrow) leveldb_object;
        if(! ptarget)
            throw std::runtime_error("CLevelDBEnv::Open(): out of memory");

        logging::LogPrintf("Opening LevelDB in %s\n", directory.string().c_str());
        leveldb::Status status = leveldb::DB::Open(this->options, directory.string(), &ptarget->ptxdb);
        if (! status.ok())
            throw std::runtime_error(tfm::format("CLevelDBEnv::Open(): error opening database environment %s", status.ToString().c_str()));

        lobj.insert(std::make_pair(instance[i], ptarget));
    }

    fLevelDbEnvInit = true;
    return true;
}

void CLevelDBEnv::Close() {
    Flush(args_bool::fShutdown);
    EnvShutdown();
}

bool CLevelDBEnv::Flush(const std::string &strDb) {
    LOCK2(cs_leveldb, lobj[strDb]->cs_ldb);
    CloseDb(strDb);
    fs::path directory = pathEnv / strDb;
    leveldb::Status status = leveldb::DB::Open(this->options, directory.string(), &lobj[strDb]->ptxdb);
    if(! status.ok())
        throw std::runtime_error(tfm::format("CLevelDBEnv::Flush(): error opening database environment %s", status.ToString().c_str()));
    return true;
}

void CLevelDBEnv::Flush(bool fShutdown) {
    (void)fShutdown;
    LOCK(cs_leveldb);
    for(auto &ite: lobj) {
        if(! Flush(ite.first))
            return;
    }
}

void CLevelDBEnv::CloseDb(const std::string &strDb) {
    LOCK(cs_leveldb);
    delete lobj[strDb]->ptxdb;
    lobj[strDb]->ptxdb = nullptr;
}

bool CLevelDBEnv::RemoveDb(const std::string &strDb) {
    LOCK(cs_leveldb);
    CloseDb(strDb);
    lobj.erase(strDb);
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CSqliteDBEnv class
//////////////////////////////////////////////////////////////////////////////////////////////

CSqliteDBEnv::CSqliteDBEnv(std::vector<std::string> instIn) : instance(instIn) {}

CSqliteDBEnv::~CSqliteDBEnv() {
    EnvShutdown();
}

void CSqliteDBEnv::EnvShutdown() {
    LOCK(cs_sqlite);
    for(const auto &ite: sqlobj) {
        if(ite.second->psql) {
            ::sqlite3_close(ite.second->psql);
        }
        delete ite.second;
    }
    sqlobj.clear();
}

bool CSqliteDBEnv::Open(fs::path pathEnv_) {
    LOCK(cs_sqlite);
    pathEnv = pathEnv_;
    for(const auto &ite: instance) {
        fs::path path_ = pathEnv_ / ite;
        sqlite_object *sobj = new(std::nothrow) sqlite_object;
        if(! sobj) {
            EnvShutdown();
            throw std::runtime_error("CSqliteDBEnv::Open memory allocate failure");
        }
        if(::sqlite3_open(path_.string().c_str(), &sobj->psql)!=SQLITE_OK) {
            EnvShutdown();
            throw std::runtime_error("CSqliteDBEnv::Open Sqlite Object open failure");
        }

        sqlobj.insert(std::make_pair(ite, sobj));
        if(is_table_exists(ite, std::string("key_value"))==false) {
            const std::string sql_cmd("create table key_value (key blob primary key, value blob not null);"); // sql const object: no necessary placeholder
            if(! sql(ite, sql_cmd)) {
                EnvShutdown();
                throw std::runtime_error("CSqliteDBEnv::Open Sqlite key_value table create failure");
            }
        }
    }
    return true;
}

bool CSqliteDBEnv::is_table_exists(const std::string &strFile, const std::string &table_name) {
    table_check tc(table_name);
    const std::string sql_cmd("select name from sqlite_master where type='table';"); // sql const object: no necessary placeholder
    char *error;
    bool ret = (::sqlite3_exec(sqlobj[strFile]->psql, sql_cmd.c_str(), m_tablenamecheck_callback, &tc, &error)==SQLITE_OK);
    return ret && tc.exists;
}

void CSqliteDBEnv::Close() {
    LOCK(cs_sqlite);
    Flush(args_bool::fShutdown);
    EnvShutdown();
}

bool CSqliteDBEnv::Flush(const std::string &strFile) {
    LOCK2(cs_sqlite, sqlobj[strFile]->cs_sql);
    ::sqlite3_close(sqlobj[strFile]->psql);

    fs::path strPath = pathEnv / strFile;
    if(::sqlite3_open(strPath.string().c_str(), &sqlobj[strFile]->psql)!=SQLITE_OK) {
        EnvShutdown();
        throw std::runtime_error("CSqliteDBEnv::Flush Sqlite Object open failure");
    }
    return true;
}

void CSqliteDBEnv::Flush(bool fShutdown) {
    (void)fShutdown;
    LOCK(cs_sqlite);
    for(const auto &ite: sqlobj) {
        if(! Flush(ite.first))
            return;
    }
}

void CSqliteDBEnv::CloseDb(const std::string &strFile) {
    LOCK(cs_sqlite);
    Flush(strFile);
    ::sqlite3_close(sqlobj[strFile]->psql);
    sqlobj[strFile]->psql = nullptr;
}

bool CSqliteDBEnv::RemoveDb(const std::string &strFile) {
    LOCK(cs_sqlite);
    CloseDb(strFile);
    sqlobj.erase(strFile);
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CDB class
//////////////////////////////////////////////////////////////////////////////////////////////

CDB::CDB(const char *pszFile, const char *pszMode/*="r+"*/) : pdb(nullptr), activeTxn(nullptr) {
    LOCK(CDBEnv::cs_db);
    if (pszFile == nullptr)
        return;

    fReadOnly = (!::strchr(pszMode, '+') && !::strchr(pszMode, 'w'));
    bool fCreate = ::strchr(pszMode, 'c') != nullptr;
    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;

    {
        LOCK(CDBEnv::cs_db);
        strFile = pszFile;
        pdb = CDBEnv::get_instance().Create(strFile, nFlags);
        if (fCreate && !Exists(std::string("version"))) {
            bool fTmp = fReadOnly;
            fReadOnly = false;
            WriteVersion(version::CLIENT_VERSION);
            fReadOnly = fTmp;
        }
    }
}

CDB::~CDB() {
    Close();
}

void CDB::Close() {
    LOCK(CDBEnv::cs_db);
    if (! pdb)
        return;
    if (activeTxn)
        activeTxn->abort();

    activeTxn = nullptr;
    pdb = nullptr;

    // Flush database activity from memory pool to disk log
    unsigned int nMinutes = 0;
    if (fReadOnly)
        nMinutes = 1;
    //if (dbparam::IsChainFile(strFile))
    //    nMinutes = 2;
    //if (dbparam::IsChainFile(strFile) && block_notify<HASH>::IsInitialBlockDownload())
    //    nMinutes = 5;

    CDBEnv::get_instance().TxnCheckPoint(nMinutes ? map_arg::GetArgUInt("-dblogsize", 100) * 1024 : 0, nMinutes);
    CDBEnv::get_instance().DecUseCount(strFile);
}

// fFlags: DB_SET_RANGE, DB_NEXT, DB_NEXT, ...
int IDB::ReadAtCursor(const DbIterator &pcursor, CDataStream &ssKey, CDataStream &ssValue, unsigned int fFlags /*= DB_NEXT*/) {
    auto ldb = [&]() {
        //if (fFlags == DB_SET || fFlags == DB_SET_RANGE || fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE) {
            // no statement
        //}
        //if (fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE) {
            // no statement
        //}

        leveldb::Iterator *ite = (leveldb::Iterator *)pcursor;
        if(ite->Valid()==false)
            return DB_NOTFOUND;

        ssKey.SetType(SER_DISK);
        ssKey.clear();
        ssKey.write((char *)ite->key().data(), ite->key().size());
        ssValue.SetType(SER_DISK);
        ssValue.clear();
        ssValue.write((char *)ite->value().data(), ite->value().size());

        cleanse::OPENSSL_cleanse(const_cast<char *>(ite->key().data()), ite->key().size());
        cleanse::OPENSSL_cleanse(const_cast<char *>(ite->value().data()), ite->value().size());
        ite->Next();
        return ite->Valid() ? 0: DB_NOTFOUND;
    };
    auto cdb = [&]() {
        Dbt datKey;
        if (fFlags == DB_SET || fFlags == DB_SET_RANGE || fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE) {
            datKey.set_data(&ssKey[0]);
            datKey.set_size((uint32_t)ssKey.size());
        }

        Dbt datValue;
        if (fFlags == DB_GET_BOTH || fFlags == DB_GET_BOTH_RANGE) {
            datValue.set_data(&ssValue[0]);
            datValue.set_size((uint32_t)ssValue.size());
        }

        datKey.set_flags(DB_DBT_MALLOC);
        datValue.set_flags(DB_DBT_MALLOC);
        int ret = ((Dbc *)pcursor)->get(&datKey, &datValue, fFlags);
        if (ret != 0)
            return ret;
        else if (datKey.get_data() == nullptr || datValue.get_data() == nullptr)
            return 99999;

        // Convert to streams
        ssKey.SetType(SER_DISK);
        ssKey.clear();
        ssKey.write((char *)datKey.get_data(), datKey.get_size());
        ssValue.SetType(SER_DISK);
        ssValue.clear();
        ssValue.write((char *)datValue.get_data(), datValue.get_size());

        // Clear and free memory
        cleanse::OPENSSL_cleanse(datKey.get_data(), datKey.get_size());
        cleanse::OPENSSL_cleanse(datValue.get_data(), datValue.get_size());
        ::free(datKey.get_data());
        ::free(datValue.get_data());
        return 0;
    };
    auto sqldb = [&]() {
        return 99999;
    };
    LOCK(pcursor.get_cs());
    return pcursor.is_leveldb() ? ldb(): cdb();
}

bool CDB::TxnBegin() {
    LOCK(CDBEnv::cs_db);
    if (!pdb || activeTxn)
        return false;

    DbTxn *ptxn = CDBEnv::get_instance().TxnBegin();
    if (! ptxn)
        return false;

    activeTxn = ptxn;
    return true;
}

bool CDB::TxnCommit() {
    LOCK(CDBEnv::cs_db);
    if (!pdb || !activeTxn)
        return false;

    int ret = activeTxn->commit(0);
    activeTxn = nullptr;
    return (ret == 0);
}

bool CDB::TxnAbort() {
    LOCK(CDBEnv::cs_db);
    if (!pdb || !activeTxn)
        return false;

    int ret = activeTxn->abort();
    activeTxn = nullptr;
    return (ret == 0);
}

bool CDB::ReadVersion(int &nVersion) {
    LOCK(CDBEnv::cs_db);
    nVersion = 0;
    return Read(std::string("version"), nVersion);
}

bool CDB::WriteVersion(int nVersion) {
    LOCK(CDBEnv::cs_db);
    return Write(std::string("version"), nVersion);
}

/*
Dbc *CDB::GetCursor() {
    LOCK(CDBEnv::cs_db);
    if (! pdb)
        return nullptr;

    Dbc *pcursor = nullptr;
    int ret = pdb->cursor(nullptr, &pcursor, 0);
    if (ret != 0)
        return nullptr;

    return pcursor;
}
*/

IDB::DbIterator CDB::GetIteCursor() {
    LOCK(CDBEnv::cs_db);
    if (! pdb)
        return std::move(DbIterator());

    Dbc *pcursor = nullptr;
    int ret = pdb->cursor(nullptr, &pcursor, 0);
    if (ret != 0)
        pcursor = nullptr;

    return std::move(DbIterator(std::move(pcursor), &CDBEnv::cs_db));
}

bool CDB::Rewrite(const std::string &strFile, const char *pszSkip/* = nullptr */)
{
    while (! args_bool::fShutdown)
    {
        {
            LOCK(CDBEnv::cs_db);
            if (!CDBEnv::get_instance().ExistsFileCount(strFile) || CDBEnv::get_instance().GetFileCount(strFile)==0) {
                // Flush log data to the dat file
                CDBEnv::get_instance().CloseDb(strFile);
                CDBEnv::get_instance().CheckpointLSN(strFile);
                CDBEnv::get_instance().EraseFileCount(strFile);

                bool fSuccess = true;
                logging::LogPrintf("Rewriting %s...\n", strFile.c_str());
                std::string strFileRes = strFile + ".rewrite";

                { // surround usage of db with extra {}
                    CDB db(strFile.c_str(), "r");
                    std::unique_ptr<Db> pdbCopy = CDBEnv::get_instance().TempCreate(nullptr, strFileRes, DB_CREATE);
                    if(pdbCopy.get() == nullptr) {
                        logging::LogPrintf("Cannot create database file %s\n", strFileRes.c_str());
                        fSuccess = false;
                    }

                    //Dbc *pcursor = db.GetCursor();
                    IDB::DbIterator ite = db.GetIteCursor();
                    //if (pcursor) {
                    if (ite.is_ok()) {
                        while (fSuccess)
                        {
                            CDataStream ssKey(SER_DISK, version::CLIENT_VERSION);
                            CDataStream ssValue(SER_DISK, version::CLIENT_VERSION);
                            //int ret = db.ReadAtCursor(pcursor, ssKey, ssValue, DB_NEXT);
                            int ret = db.ReadAtCursor(ite, ssKey, ssValue, DB_NEXT);
                            if (ret == DB_NOTFOUND) {
                                //pcursor->close();
                                break;
                            } else if (ret != 0) {
                                //pcursor->close();
                                fSuccess = false;
                                break;
                            }

                            if (pszSkip != nullptr) {
                                size_t pszSkipLen = strlen(pszSkip);
                                if (::strncmp(&ssKey[0], pszSkip, std::min(ssKey.size(), pszSkipLen)) == 0) {
                                    continue;
                                }
                            }

                            if (::strncmp(&ssKey[0], "\x07version", 8) == 0) {
                                // Update version:
                                ssValue.clear();
                                ssValue << version::CLIENT_VERSION;
                            }

                            Dbt datKey(&ssKey[0], ssKey.size());
                            Dbt datValue(&ssValue[0], ssValue.size());
                            int ret2 = pdbCopy->put(nullptr, &datKey, &datValue, DB_NOOVERWRITE);
                            if (ret2 > 0) {
                                fSuccess = false;
                            }
                        }
                    }
                    if (fSuccess) {
                        db.Close();
                        CDBEnv::get_instance().CloseDb(strFile);
                        if (pdbCopy->close(0)) {
                            fSuccess = false;
                        }
                    }
                }
                if (fSuccess) {
                    fSuccess = CDBEnv::get_instance().Remove(strFile);
                    fSuccess = CDBEnv::get_instance().Rename(strFileRes, strFile);
                }
                if (! fSuccess) {
                    logging::LogPrintf("Rewriting of %s FAILED!\n", strFileRes.c_str());
                }
                return fSuccess;
            }
        }
        util::Sleep(100);
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CLevelDB class
//////////////////////////////////////////////////////////////////////////////////////////////

namespace {
class CBatchScanner final : public leveldb::WriteBatch::Handler
{
    CBatchScanner(const CBatchScanner &)=delete;
    CBatchScanner(CBatchScanner &&)=delete;
    CBatchScanner &operator=(const CBatchScanner &)=delete;
    CBatchScanner &operator=(CBatchScanner &&)=delete;
public:
    std::string needle;
    bool *deleted;
    std::string *foundValue;
    bool foundEntry;

    CBatchScanner() : foundEntry(false) {}

    void Put(const leveldb::Slice &key, const leveldb::Slice &value) {
        if (key.ToString() == needle) {
            foundEntry = true;
            *deleted = false;
            *foundValue = value.ToString();
        }
    }

    void Delete(const leveldb::Slice &key) {
        if (key.ToString() == needle) {
            foundEntry = true;
            *deleted = true;
        }
    }
};
} // namespace

bool CLevelDB::ScanBatch(const CDBStream &key, std::string *value, bool *deleted) const {
    LOCK(cs_db);
    assert(this->activeBatch);

    *deleted = false;

    CBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    leveldb::Status status = this->activeBatch->Iterate(&scanner);
    if (! status.ok()) {
        throw std::runtime_error(status.ToString());
    }
    return scanner.foundEntry;
}

CLevelDB::CLevelDB(const std::string &strDb, const char *pszMode /*="r+"*/, bool fSecureIn /*= false*/) :
    pdb(CLevelDBEnv::get_instance().get_ptxdb(strDb)), cs_db(CLevelDBEnv::get_instance().get_rcs(strDb)), fReadOnly(true), p(nullptr) {
    assert(pszMode);
    fSecure = fSecureIn;

    this->activeBatch = nullptr;
    fReadOnly = (!::strchr(pszMode, '+') && !::strchr(pszMode, 'w'));
}

CLevelDB::~CLevelDB() {
    Close();
}

IDB::DbIterator CLevelDB::GetIteCursor() {
    LOCK(cs_db);
    leveldb::Iterator *p = pdb->NewIterator(leveldb::ReadOptions());
    if(! p)
        throw std::runtime_error("CLevelDB::GetIteCursor memory allocate failure");
    p->SeekToFirst();
    return std::move(DbIterator(std::move(p), &cs_db));
}

void CLevelDB::Close() {
    LOCK(cs_db);
    delete this->activeBatch;
    this->activeBatch = nullptr;

    // p is no necessary delete. because delete by const_iterator.
    //debugcs::instance() << "CLevelDB::Close()" << debugcs::endl();
}

bool CLevelDB::TxnBegin() {
    LOCK(cs_db);
    assert(fSecure==false);
    assert(! this->activeBatch);
    this->activeBatch = new(std::nothrow) leveldb::WriteBatch();
    if (! this->activeBatch) {
        throw std::runtime_error("LevelDB : WriteBatch failed to allocate memory");
        return false;
    }
    return true;
}

bool CLevelDB::TxnCommit() {
    LOCK(cs_db);
    assert(fSecure==false);
    assert(this->activeBatch);

    leveldb::Status status = pdb->Write(leveldb::WriteOptions(), activeBatch);
    delete this->activeBatch;
    this->activeBatch = nullptr;

    if (! status.ok()) {
        logging::LogPrintf("LevelDB batch commit failure: %s\n", status.ToString().c_str());
        return false;
    }
    return true;
}

bool CLevelDB::TxnAbort() {
    LOCK(cs_db);
    assert(fSecure==false);
    delete this->activeBatch;
    this->activeBatch = nullptr;
    return true;
}

bool CLevelDB::ReadVersion(int &nVersion) {
    LOCK(cs_db);
    nVersion = 0;
    return Read(std::string("version"), nVersion);
}

bool CLevelDB::WriteVersion(int nVersion) {
    LOCK(cs_db);
    return Write(std::string("version"), nVersion);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CSqliteDB class
//////////////////////////////////////////////////////////////////////////////////////////////

CSqliteDB::CSqliteDB(const std::string &strFile, const char *pszMode /*= "r+"*/, bool fSecureIn /*= false*/) :
    pdb(CSqliteDBEnv::get_instance().get_psqldb(strFile)), cs_db(CSqliteDBEnv::get_instance().get_rcs(strFile)) {
    fSecure = fSecureIn;

    fReadOnly = (!::strchr(pszMode, '+') && !::strchr(pszMode, 'w'));
}
CSqliteDB::~CSqliteDB() {
    Close();
}

IDB::DbIterator CSqliteDB::GetIteCursor() {

    return std::move(IDB::DbIterator());
}

void CSqliteDB::Close() {}

bool CSqliteDB::TxnBegin() {

    return false;
}

bool CSqliteDB::TxnCommit() {

    return false;
}

bool CSqliteDB::TxnAbort() {

    return false;
}

bool CSqliteDB::ReadVersion(int &nVersion) {
    LOCK(cs_db);
    nVersion = 0;
    return Read(std::string("version"), nVersion);
}

bool CSqliteDB::WriteVersion(int nVersion) {
    LOCK(cs_db);
    return Write(std::string("version"), nVersion);
}
