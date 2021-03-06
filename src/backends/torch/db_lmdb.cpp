#ifdef USE_LMDB
#include "db_lmdb.hpp"

#include <sys/stat.h>

#include <string>
#include <cstring>
#include <iostream>

namespace dd
{
  namespace db
  {

    void LMDB::Open(const std::string &source, Mode mode)
    {
      MDB_CHECK(mdb_env_create(&mdb_env_));
      if (mode == NEW)
        {
          int mk = mkdir(source.c_str(), 0744);
          if (mk != 0)
            {
              CHECK_EQ(mkdir(source.c_str(), 0744), 0)
                  << "mkdir " << source << " failed";
            }
        }
      int flags = 0;
      if (mode == READ)
        {
          flags = MDB_RDONLY | MDB_NOTLS | MDB_NORDAHEAD;
        }
      int rc = mdb_env_open(mdb_env_, source.c_str(), flags, 0664);
#ifndef ALLOW_LMDB_NOLOCK
      MDB_CHECK(rc);
#else
      if (rc == EACCES)
        {
          LOG(WARNING) << "Permission denied. Trying with MDB_NOLOCK ...";
          // Close and re-open environment handle
          mdb_env_close(mdb_env_);
          MDB_CHECK(mdb_env_create(&mdb_env_));
          // Try again with MDB_NOLOCK
          flags |= MDB_NOLOCK;
          MDB_CHECK(mdb_env_open(mdb_env_, source.c_str(), flags, 0664));
        }
      else
        {
          MDB_CHECK(rc);
        }
#endif
      LOG(INFO) << "Opened lmdb " << source;
    }

    LMDBCursor *LMDB::NewCursor()
    {
      MDB_txn *mdb_txn;
      MDB_cursor *mdb_cursor;
      MDB_CHECK(mdb_txn_begin(mdb_env_, NULL, MDB_RDONLY, &mdb_txn));
      MDB_CHECK(mdb_dbi_open(mdb_txn, NULL, 0, &mdb_dbi_));
      MDB_CHECK(mdb_cursor_open(mdb_txn, mdb_dbi_, &mdb_cursor));
      return new LMDBCursor(mdb_txn, mdb_cursor);
    }

    LMDBTransaction *LMDB::NewTransaction()
    {
      return new LMDBTransaction(mdb_env_);
    }

    int LMDB::Count()
    {
      MDB_stat stats;
      int err = mdb_env_stat(mdb_env_, &stats);
      if (err != 0)
        return -1;
      return stats.ms_entries;
    }

    void LMDB::Get(const std::string &key, std::string &data_val)
    {

      MDB_val data;
      MDB_txn *mdb_txn;
      MDB_CHECK(mdb_txn_begin(mdb_env_, NULL, MDB_RDONLY, &mdb_txn));
      MDB_dbi mdb_dbi;
      MDB_CHECK(mdb_dbi_open(mdb_txn, NULL, 0, &mdb_dbi));
      MDB_val mdb_key;
      mdb_key.mv_size = key.size();
      mdb_key.mv_data = const_cast<char *>(key.data());
      mdb_get(mdb_txn, mdb_dbi, &mdb_key, &data);
      char *data_raw = new char[data.mv_size + 1];
      memcpy(data_raw, data.mv_data, data.mv_size);
      data_raw[data.mv_size] = 0;
      data_val = std::string(data_raw, data.mv_size);
      delete[] data_raw;
      mdb_txn_commit(mdb_txn);
      mdb_dbi_close(mdb_env_, mdb_dbi);
    }

    void LMDB::Remove(const std::string &key)
    {
      MDB_txn *mdb_txn;
      MDB_CHECK(mdb_txn_begin(mdb_env_, NULL, 0, &mdb_txn));
      MDB_dbi mdb_dbi;
      MDB_CHECK(mdb_dbi_open(mdb_txn, NULL, 0, &mdb_dbi));
      MDB_val mdb_key;
      mdb_key.mv_size = key.size();
      mdb_key.mv_data = const_cast<char *>(key.data());
      mdb_del(mdb_txn, mdb_dbi, &mdb_key, NULL);
      mdb_txn_commit(mdb_txn);
      mdb_dbi_close(mdb_env_, mdb_dbi);
    }

    void LMDBTransaction::Put(const std::string &key, const std::string &value)
    {
      keys.push_back(key);
      values.push_back(value);
    }

    void LMDBTransaction::Commit()
    {
      MDB_dbi mdb_dbi;
      MDB_val mdb_key, mdb_data;
      MDB_txn *mdb_txn;

      // Initialize MDB variables
      MDB_CHECK(mdb_txn_begin(mdb_env_, NULL, 0, &mdb_txn));
      MDB_CHECK(mdb_dbi_open(mdb_txn, NULL, 0, &mdb_dbi));

      for (unsigned int i = 0; i < keys.size(); i++)
        {
          mdb_key.mv_size = keys[i].size();
          mdb_key.mv_data = const_cast<char *>(keys[i].data());
          mdb_data.mv_size = values[i].size();
          mdb_data.mv_data = const_cast<char *>(values[i].data());

          // Add data to the transaction
          int put_rc = mdb_put(mdb_txn, mdb_dbi, &mdb_key, &mdb_data, 0);
          if (put_rc == MDB_MAP_FULL)
            {
              // Out of memory - double the map size and retry
              mdb_txn_abort(mdb_txn);
              mdb_dbi_close(mdb_env_, mdb_dbi);
              DoubleMapSize();
              Commit();
              return;
            }
          // May have failed for some other reason
          MDB_CHECK(put_rc);
        }

      // Commit the transaction
      int commit_rc = mdb_txn_commit(mdb_txn);
      if (commit_rc == MDB_MAP_FULL)
        {
          // Out of memory - double the map size and retry
          mdb_dbi_close(mdb_env_, mdb_dbi);
          DoubleMapSize();
          Commit();
          return;
        }
      // May have failed for some other reason
      MDB_CHECK(commit_rc);

      // Cleanup after successful commit
      mdb_dbi_close(mdb_env_, mdb_dbi);
      keys.clear();
      values.clear();
    }

    void LMDBTransaction::DoubleMapSize()
    {
      struct MDB_envinfo current_info;
      MDB_CHECK(mdb_env_info(mdb_env_, &current_info));
      size_t new_size = current_info.me_mapsize * 2;
      DLOG(INFO) << "Doubling LMDB map size to " << (new_size >> 20)
                 << "MB ...";
      MDB_CHECK(mdb_env_set_mapsize(mdb_env_, new_size));
    }

  } // namespace db
} // namespace dd
#endif // USE_LMDB
