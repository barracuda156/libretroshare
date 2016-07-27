#include "util/rsdir.h"
#include "hash_cache.h"

#define HASHSTORAGE_DEBUG 1

HashStorage::HashStorage(const std::string& save_file_name)
    : mFilePath(save_file_name), mHashMtx("Hash Storage mutex")
{
}

void HashStorage::data_tick()
{
    std::cerr << "Ticking hash thread." << std::endl;

    FileHashJob job;

    {
        RS_STACK_MUTEX(mHashMtx) ;

        if(mFilesToHash.empty())
            return ;

        job = mFilesToHash.begin()->second ;
    }

    std::cerr << "Hashing file " << job.full_path << "..." ; std::cerr.flush();

    RsFileHash hash;
    uint64_t size ;

    if(!RsDirUtil::getFileHash(job.full_path, hash,size, this))
    {
        std::cerr << "ERROR" << std::endl;
        return;
    }
    // update the hash storage


    // call the client

    job.client->hash_callback(job.client_param, job.full_path, hash, size);

    std::cerr << "done."<< std::endl;

    if(mFilesToHash.empty())
    {
        std::cerr << "Starting hashing thread." << std::endl;
        fullstop();
        std::cerr << "done." << std::endl;
    }
}

bool HashStorage::requestHash(const std::string& full_path,uint64_t size,time_t mod_time,RsFileHash& known_hash,HashStorageClient *c,uint32_t client_param)
{
    // check if the hash is up to date w.r.t. cache.

    RS_STACK_MUTEX(mHashMtx) ;

    time_t now = time(NULL) ;
    std::map<std::string,HashStorageInfo>::iterator it = mFiles.find(full_path) ;

    if(it != mFiles.end() && (uint64_t)mod_time == it->second.modf_stamp && size == it->second.size)
    {
        it->second.time_stamp = now ;
#ifdef HASHCACHE_DEBUG
        std::cerr << "Found in cache." << std::endl ;
#endif
        return true ;
    }

    // we need to schedule a re-hashing

    if(mFilesToHash.find(full_path) != mFilesToHash.end())
        return false ;

    FileHashJob job ;

    job.client = c ;
    job.client_param = client_param ;
    job.full_path = full_path ;

    mFilesToHash[full_path] = job;

    if(!isRunning())
    {
        std::cerr << "Starting hashing thread." << std::endl;
        start() ;
    }

    return false;
}

void HashStorage::clean()
{
#ifdef HASHSTORAGE_DEBUG
    std::cerr << "Cleaning HashStorage..." << std::endl ;
#endif
    time_t now = time(NULL) ;
    time_t duration = mMaxStorageDurationDays * 24 * 3600 ; // seconds

#ifdef HASHSTORAGE_DEBUG
    std::cerr << "cleaning hash cache." << std::endl ;
#endif

    for(std::map<std::string,HashStorageInfo>::iterator it(mFiles.begin());it!=mFiles.end();)
        if(it->second.time_stamp + duration < (uint64_t)now)
        {
#ifdef HASHSTORAGE_DEBUG
            std::cerr << "  Entry too old: " << it->first << ", ts=" << it->second.time_stamp << std::endl ;
#endif
            std::map<std::string,HashStorageInfo>::iterator tmp(it) ;
            ++tmp ;
            mFiles.erase(it) ;
            it=tmp ;
            mChanged = true ;
        }
        else
            ++it ;

#ifdef HASHSTORAGE_DEBUG
    std::cerr << "Done." << std::endl;
#endif
}

