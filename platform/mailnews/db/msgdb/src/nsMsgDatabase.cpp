/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// this file implements the nsMsgDatabase interface using the MDB Interface.

#include "nscore.h"
#include "msgCore.h"
#include "nsMailDatabase.h"
#include "nsDBFolderInfo.h"
#include "nsMsgKeySet.h"
#include "nsMsgThread.h"
#include "nsIMsgSearchTerm.h"
#include "nsMsgBaseCID.h"
#include "nsMorkCID.h"
#include "nsIMdbFactoryFactory.h"
#include "mozilla/Logging.h"
#include "prprf.h"
#include "nsMsgDBCID.h"
#include "nsILocale.h"
#include "nsMsgMimeCID.h"
#include "nsILocaleService.h"
#include "nsMsgFolderFlags.h"
#include "nsIMsgAccountManager.h"
#include "nsIMsgDBView.h"
#include "nsIMsgFolderCache.h"
#include "nsIMsgFolderCacheElement.h"
#include "MailNewsTypes2.h"
#include "nsMsgUtils.h"
#include "nsMsgKeyArray.h"
#include "nsIMutableArray.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsMemory.h"
#include "nsICollation.h"
#include "nsCollationCID.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsArrayEnumerator.h"
#include "nsIMemoryReporter.h"
#include "mozilla/mailnews/MimeHeaderParser.h"
#include "mozilla/mailnews/Services.h"

using namespace mozilla::mailnews;
using namespace mozilla;

#if defined(DEBUG_sspitzer_) || defined(DEBUG_seth_)
#define DEBUG_MSGKEYSET 1
#endif

#define MSG_HASH_SIZE 512

// This will be used on discovery, since we don't know total.
const int32_t kMaxHdrsInCache = 512;

// special keys
static const nsMsgKey kAllMsgHdrsTableKey = 1;
static const nsMsgKey kTableKeyForThreadOne = 0xfffffffe;
static const nsMsgKey kAllThreadsTableKey = 0xfffffffd;
static const nsMsgKey kFirstPseudoKey = 0xfffffff0;
static const nsMsgKey kIdStartOfFake = 0xffffff80;
static const nsMsgKey kForceReparseKey = 0xfffffff0;

static PRLogModuleInfo* DBLog;

PRTime nsMsgDatabase::gLastUseTime;

NS_IMPL_ISUPPORTS(nsMsgDBService, nsIMsgDBService)

nsMsgDBService::nsMsgDBService()
{
  DBLog = PR_NewLogModule("MSGDB");
}


nsMsgDBService::~nsMsgDBService()
{
#ifdef DEBUG
  // If you hit this warning, it means that some code is holding onto
  // a db at shutdown.
  NS_WARNING_ASSERTION(!m_dbCache.Length(), "some msg dbs left open");
  for (uint32_t i = 0; i < m_dbCache.Length(); i++)
  {
    nsMsgDatabase* pMessageDB = m_dbCache.ElementAt(i);
    if (pMessageDB)
      printf("db left open %s\n", (const char *) pMessageDB->m_dbName.get());
  }
#endif
}

NS_IMETHODIMP nsMsgDBService::OpenFolderDB(nsIMsgFolder *aFolder,
                                           bool aLeaveInvalidDB,
                                           nsIMsgDatabase **_retval)
{
  NS_ENSURE_ARG(aFolder);
  nsCOMPtr<nsIMsgIncomingServer> incomingServer;
  nsresult rv = aFolder->GetServer(getter_AddRefs(incomingServer));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> summaryFilePath;
  rv = aFolder->GetSummaryFile(getter_AddRefs(summaryFilePath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMsgDatabase *cacheDB = FindInCache(summaryFilePath);
  if (cacheDB)
  {
    // this db could have ended up in the folder cache w/o an m_folder pointer via
    // OpenMailDBFromFile. If so, take this chance to fix the folder.
    if (!cacheDB->m_folder)
      cacheDB->m_folder = aFolder;
    cacheDB->RememberLastUseTime();
    *_retval = cacheDB; // FindInCache already addRefed.
    // if m_thumb is set, someone is asynchronously opening the db. But our
    // caller wants to synchronously open it, so just do it.
    if (cacheDB->m_thumb)
      return cacheDB->Open(this, summaryFilePath, false, aLeaveInvalidDB);
    return NS_OK;
  }

  nsCString localDatabaseType;
  incomingServer->GetLocalDatabaseType(localDatabaseType);
  nsAutoCString dbContractID(NS_MSGDB_CONTRACTID);
  dbContractID.Append(localDatabaseType.get());
  nsCOMPtr <nsIMsgDatabase> msgDB = do_CreateInstance(dbContractID.get(), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Don't try to create the database yet--let the createNewDB call do that.
  nsMsgDatabase *msgDatabase = static_cast<nsMsgDatabase *>(msgDB.get());
  msgDatabase->m_folder = aFolder;
  rv = msgDatabase->Open(this, summaryFilePath, false, aLeaveInvalidDB);
  if (NS_FAILED(rv) && rv != NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE)
    return rv;

  NS_ADDREF(*_retval = msgDB);

  if (NS_FAILED(rv))
  {
#ifdef DEBUG
    // Doing these checks for debug only as we don't want to report certain
    // errors in debug mode, but in release mode we wouldn't report them either

    // These errors are expected.
    if (rv == NS_MSG_ERROR_FOLDER_SUMMARY_MISSING ||
        rv == NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE)
      return rv;

    // If it isn't one of the expected errors, throw a warning.
    NS_ENSURE_SUCCESS(rv, rv);
#endif
    return rv;
  }

  FinishDBOpen(aFolder, msgDatabase);
  return rv;
}

NS_IMETHODIMP nsMsgDBService::AsyncOpenFolderDB(nsIMsgFolder *aFolder,
                                                bool aLeaveInvalidDB,
                                                nsIMsgDatabase **_retval)
{
  NS_ENSURE_ARG(aFolder);

  nsCOMPtr <nsIFile> summaryFilePath;
  nsresult rv = aFolder->GetSummaryFile(getter_AddRefs(summaryFilePath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMsgDatabase *cacheDB = FindInCache(summaryFilePath);
  if (cacheDB)
  {
    // this db could have ended up in the folder cache w/o an m_folder pointer via
    // OpenMailDBFromFile. If so, take this chance to fix the folder.
    if (!cacheDB->m_folder)
      cacheDB->m_folder = aFolder;
    *_retval = cacheDB; // FindInCache already addRefed.
    // We don't care if an other consumer is thumbing the store. In that
    // case, they'll both thumb the store.
    return NS_OK;
  }

  nsCOMPtr <nsIMsgIncomingServer> incomingServer;
  rv = aFolder->GetServer(getter_AddRefs(incomingServer));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString localDatabaseType;
  incomingServer->GetLocalDatabaseType(localDatabaseType);
  nsAutoCString dbContractID(NS_MSGDB_CONTRACTID);
  dbContractID.Append(localDatabaseType.get());
  nsCOMPtr <nsIMsgDatabase> msgDB = do_CreateInstance(dbContractID.get(), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsMsgDatabase *msgDatabase = static_cast<nsMsgDatabase *>(msgDB.get());
  rv = msgDatabase->OpenInternal(this, summaryFilePath, false, aLeaveInvalidDB,
                                 false /* open asynchronously */);

  NS_ADDREF(*_retval = msgDB);
  msgDatabase->m_folder = aFolder;

  if (NS_FAILED(rv))
  {
#ifdef DEBUG
    // Doing these checks for debug only as we don't want to report certain
    // errors in debug mode, but in release mode we wouldn't report them either

    // These errors are expected.
    if (rv == NS_MSG_ERROR_FOLDER_SUMMARY_MISSING ||
        rv == NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE)
      return rv;

    // If it isn't one of the expected errors, throw a warning.
    NS_ENSURE_SUCCESS(rv, rv);
#endif
    return rv;
  }

  FinishDBOpen(aFolder, msgDatabase);
  return rv;
}

NS_IMETHODIMP nsMsgDBService::OpenMore(nsIMsgDatabase *aDB,
                                       uint32_t aTimeHint,
                                       bool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  nsMsgDatabase *msgDatabase = static_cast<nsMsgDatabase *>(aDB);
  NS_ENSURE_TRUE(msgDatabase, NS_ERROR_INVALID_ARG);
  // Check if this db has been opened.
  if (!msgDatabase->m_thumb)
  {
    *_retval = true;
    return NS_OK;
  }
  nsresult rv;
  *_retval = false;
  PRIntervalTime startTime = PR_IntervalNow();
  do
  {
    mdb_count outTotal;    // total somethings to do in operation
    mdb_count outCurrent;  // subportion of total completed so far
    mdb_bool outDone = false;      // is operation finished?
    mdb_bool outBroken;     // is operation irreparably dead and broken?
    rv = msgDatabase->m_thumb->DoMore(msgDatabase->m_mdbEnv,
                                       &outTotal, &outCurrent, &outDone,
                                       &outBroken);
    if (NS_FAILED(rv))
      break;
    if (outDone)
    {
      nsCOMPtr<nsIMdbFactory> mdbFactory;
      msgDatabase->GetMDBFactory(getter_AddRefs(mdbFactory));
      NS_ENSURE_TRUE(mdbFactory, NS_ERROR_FAILURE);
      rv = mdbFactory->ThumbToOpenStore(msgDatabase->m_mdbEnv, msgDatabase->m_thumb, &msgDatabase->m_mdbStore);
      msgDatabase->m_thumb = nullptr;
      nsCOMPtr<nsIFile> folderPath;
      (void) msgDatabase->m_folder->GetFilePath(getter_AddRefs(folderPath));
      nsCOMPtr <nsIFile> summaryFile;
      (void) GetSummaryFileLocation(folderPath, getter_AddRefs(summaryFile));

      if (NS_SUCCEEDED(rv))
        rv = (msgDatabase->m_mdbStore) ? msgDatabase->InitExistingDB() : NS_ERROR_FAILURE;
      if (NS_SUCCEEDED(rv))
        rv = msgDatabase->CheckForErrors(rv, false, this, summaryFile);

      FinishDBOpen(msgDatabase->m_folder, msgDatabase);
      break;
    }
  }
  while (PR_IntervalToMilliseconds(PR_IntervalNow() - startTime) <= aTimeHint);
  *_retval = !msgDatabase->m_thumb;
  return rv;
}

/**
 * When a db is opened, we need to hook up any pending listeners for 
 * that db, and notify them.
 */
void nsMsgDBService::HookupPendingListeners(nsIMsgDatabase *db,
                                            nsIMsgFolder *folder)
{
  for (int32_t listenerIndex = 0;
       listenerIndex < m_foldersPendingListeners.Count(); listenerIndex++)
  {
  //  check if we have a pending listener on this db, and if so, add it.
    if (m_foldersPendingListeners[listenerIndex] == folder)
    {
      db->AddListener(m_pendingListeners.ObjectAt(listenerIndex));
      m_pendingListeners.ObjectAt(listenerIndex)->OnEvent(db, "DBOpened");
    }
  }
}

void nsMsgDBService::FinishDBOpen(nsIMsgFolder *aFolder, nsMsgDatabase *aMsgDB)
{
  uint32_t folderFlags;
  aFolder->GetFlags(&folderFlags);

  if (! (folderFlags & nsMsgFolderFlags::Virtual) &&
      aMsgDB->m_mdbAllMsgHeadersTable)
  {
    mdb_count numHdrsInTable = 0;
    int32_t numMessages;
    aMsgDB->m_mdbAllMsgHeadersTable->GetCount(aMsgDB->GetEnv(),
                                              &numHdrsInTable);
    aMsgDB->m_dbFolderInfo->GetNumMessages(&numMessages);
    if (numMessages != (int32_t) numHdrsInTable)
      aMsgDB->SyncCounts();
  }
  HookupPendingListeners(aMsgDB, aFolder);
  aMsgDB->RememberLastUseTime();
}

//----------------------------------------------------------------------
// FindInCache - this addrefs the db it finds.
//----------------------------------------------------------------------
nsMsgDatabase* nsMsgDBService::FindInCache(nsIFile *dbName)
{
  for (uint32_t i = 0; i < m_dbCache.Length(); i++)
  {
    nsMsgDatabase* pMessageDB = m_dbCache[i];
    if (pMessageDB->MatchDbName(dbName))
    {
      if (pMessageDB->m_mdbStore)  // don't return db without store
      {
        NS_ADDREF(pMessageDB);
        return pMessageDB;
      }
    }
  }
  return nullptr;
}

// This method is called when the caller is trying to create a db without
// having a corresponding nsIMsgFolder object.  This happens in a few
// situations, including imap folder discovery, compacting local folders,
// and copying local folders.
NS_IMETHODIMP nsMsgDBService::OpenMailDBFromFile(nsIFile *aFolderName,
                                                 nsIMsgFolder *aFolder,
                                                 bool aCreate,
                                                 bool aLeaveInvalidDB,
                                                 nsIMsgDatabase** pMessageDB)
{
  if (!aFolderName)
    return NS_ERROR_NULL_POINTER;

  nsCOMPtr <nsIFile>  dbPath;
  nsresult rv = GetSummaryFileLocation(aFolderName, getter_AddRefs(dbPath));
  NS_ENSURE_SUCCESS(rv, rv);

  *pMessageDB = FindInCache(dbPath);
  if (*pMessageDB)
    return NS_OK;

  RefPtr<nsMailDatabase> msgDB = new nsMailDatabase;
  NS_ENSURE_TRUE(msgDB, NS_ERROR_OUT_OF_MEMORY);
  rv = msgDB->Open(this, dbPath, aCreate, aLeaveInvalidDB);
  if (rv == NS_ERROR_FILE_TARGET_DOES_NOT_EXIST)
    return rv;
  NS_IF_ADDREF(*pMessageDB = msgDB);
  if (aCreate && msgDB && rv == NS_MSG_ERROR_FOLDER_SUMMARY_MISSING)
    rv = NS_OK;
  if (NS_SUCCEEDED(rv))
    msgDB->m_folder = aFolder;
  return rv;
}

NS_IMETHODIMP nsMsgDBService::CreateNewDB(nsIMsgFolder *aFolder,
                                          nsIMsgDatabase **_retval)
{
  NS_ENSURE_ARG(aFolder);
  
  nsCOMPtr <nsIMsgIncomingServer> incomingServer;
  nsresult rv = aFolder->GetServer(getter_AddRefs(incomingServer));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> summaryFilePath;
  rv = aFolder->GetSummaryFile(getter_AddRefs(summaryFilePath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString localDatabaseType;
  incomingServer->GetLocalDatabaseType(localDatabaseType);
  nsAutoCString dbContractID(NS_MSGDB_CONTRACTID);
  dbContractID.Append(localDatabaseType.get());
  
  nsCOMPtr <nsIMsgDatabase> msgDB = do_CreateInstance(dbContractID.get(), &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsMsgDatabase *msgDatabase = static_cast<nsMsgDatabase *>(msgDB.get());

  msgDatabase->m_folder = aFolder;
  rv = msgDatabase->Open(this, summaryFilePath, true, true);

  // We are trying to create a new database, but that implies that it did not
  // already exist. Open returns NS_MSG_ERROR_FOLDER_SUMMARY_MISSING for the
  // successful creation of a new database. But if it existed for some
  // reason, then we would get rv = NS_OK instead. That is a "failure"
  // from our perspective, so we want to return a failure since we are not
  // returning a valid database object.
  NS_ENSURE_TRUE(rv == NS_MSG_ERROR_FOLDER_SUMMARY_MISSING,
                 NS_SUCCEEDED(rv) ? NS_ERROR_FILE_ALREADY_EXISTS : rv);

  NS_ADDREF(*_retval = msgDB);

  HookupPendingListeners(msgDB, aFolder);

  msgDatabase->RememberLastUseTime();

  return NS_OK;
}

/* void registerPendingListener (in nsIMsgFolder aFolder, in nsIDBChangeListener aListener); */
NS_IMETHODIMP nsMsgDBService::RegisterPendingListener(nsIMsgFolder *aFolder, nsIDBChangeListener *aListener)
{
  // need to make sure we don't hold onto these forever. Maybe a shutdown listener?
  // if there is a db open on this folder already, we should register the listener.
  m_foldersPendingListeners.AppendObject(aFolder);
  m_pendingListeners.AppendObject(aListener);
  nsCOMPtr <nsIMsgDatabase> openDB;
  CachedDBForFolder(aFolder, getter_AddRefs(openDB));
  if (openDB)
    openDB->AddListener(aListener);
  return NS_OK;
}

/* void unregisterPendingListener (in nsIDBChangeListener aListener); */
NS_IMETHODIMP nsMsgDBService::UnregisterPendingListener(nsIDBChangeListener *aListener)
{
  int32_t listenerIndex = m_pendingListeners.IndexOfObject(aListener);
  if (listenerIndex != -1)
  {
    nsCOMPtr<nsIMsgDatabase> msgDB;
    CachedDBForFolder(m_foldersPendingListeners[listenerIndex], getter_AddRefs(msgDB));
    if (msgDB)
      msgDB->RemoveListener(aListener);
    m_foldersPendingListeners.RemoveObjectAt(listenerIndex);
    m_pendingListeners.RemoveObjectAt(listenerIndex);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsMsgDBService::CachedDBForFolder(nsIMsgFolder *aFolder, nsIMsgDatabase **aRetDB)
{
  NS_ENSURE_ARG_POINTER(aFolder);
  NS_ENSURE_ARG_POINTER(aRetDB);

  nsCOMPtr<nsIFile> summaryFilePath;
  nsresult rv = aFolder->GetSummaryFile(getter_AddRefs(summaryFilePath));
  NS_ENSURE_SUCCESS(rv, rv);

  *aRetDB = FindInCache(summaryFilePath);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBService::ForceFolderDBClosed(nsIMsgFolder *aFolder)
{
  nsCOMPtr<nsIMsgDatabase> mailDB;
  nsresult rv = CachedDBForFolder(aFolder, getter_AddRefs(mailDB));
  if (mailDB)
  {
    mailDB->ForceClosed();
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBService::GetOpenDBs(nsIArray **aOpenDBs)
{
  NS_ENSURE_ARG_POINTER(aOpenDBs);
  nsresult rv;
  nsCOMPtr<nsIMutableArray> openDBs(do_CreateInstance(NS_ARRAY_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  for (uint32_t i = 0; i < m_dbCache.Length(); i++)
    openDBs->AppendElement(m_dbCache[i], false);

  openDBs.forget(aOpenDBs);
  return NS_OK;
}

static bool gGotGlobalPrefs = false;
static bool gThreadWithoutRe = true;
static bool gStrictThreading = false;
static bool gCorrectThreading = false;

void nsMsgDatabase::GetGlobalPrefs()
{
  if (!gGotGlobalPrefs)
  {
    GetBoolPref("mail.thread_without_re", &gThreadWithoutRe);
    GetBoolPref("mail.strict_threading", &gStrictThreading);
    GetBoolPref("mail.correct_threading", &gCorrectThreading);
    gGotGlobalPrefs = true;
  }
}

nsresult nsMsgDatabase::AddHdrToCache(nsIMsgDBHdr *hdr, nsMsgKey key) // do we want key? We could get it from hdr
{
  if (m_bCacheHeaders)
  {
    if (!m_cachedHeaders)
      m_cachedHeaders = new PLDHashTable(&gMsgDBHashTableOps, sizeof(struct MsgHdrHashElement), m_cacheSize);
    if (m_cachedHeaders)
    {
      if (key == nsMsgKey_None)
        hdr->GetMessageKey(&key);
      if (m_cachedHeaders->EntryCount() > m_cacheSize)
        ClearHdrCache(true);
      PLDHashEntryHdr *entry = m_cachedHeaders->Add((void *)(uintptr_t) key, mozilla::fallible);
      if (!entry)
        return NS_ERROR_OUT_OF_MEMORY; // XXX out of memory

      MsgHdrHashElement* element = static_cast<MsgHdrHashElement*>(entry);
      element->mHdr = hdr;
      element->mKey = key;
      NS_ADDREF(hdr);     // make the cache hold onto the header
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;
}


NS_IMETHODIMP nsMsgDatabase::SetMsgHdrCacheSize(uint32_t aSize)
{
  m_cacheSize = aSize;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetMsgHdrCacheSize(uint32_t *aSize)
{
  NS_ENSURE_ARG_POINTER(aSize);
  *aSize = m_cacheSize;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetLastUseTime(PRTime *aTime)
{
  NS_ENSURE_ARG_POINTER(aTime);
  *aTime = m_lastUseTime;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::SetLastUseTime(PRTime aTime)
{
  gLastUseTime = m_lastUseTime = aTime;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetDatabaseSize(int64_t *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv;
  nsCOMPtr<nsIFile> summaryFilePath = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = summaryFilePath->InitWithNativePath(m_dbName);
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists;
  rv = summaryFilePath->Exists(&exists);
  if (NS_SUCCEEDED(rv))
  {
    if (exists)
      rv = summaryFilePath->GetFileSize(_retval);
    else
      *_retval = 0;
  }

  return rv;
}

NS_IMETHODIMP nsMsgDatabase::ClearCachedHdrs()
{
  ClearCachedObjects(false);
#ifdef DEBUG_bienvenu1
  if (mRefCnt > 1)
  {
    NS_ASSERTION(false, "");
    printf("someone's holding onto db - refs = %ld\n", mRefCnt);
  }
#endif
  return NS_OK;
}

void nsMsgDatabase::ClearEnumerators()
{
  // clear out existing enumerators
  nsTArray<nsMsgDBEnumerator *> copyEnumerators;
  copyEnumerators.SwapElements(m_enumerators);

  uint32_t numEnums = copyEnumerators.Length();
  for (uint32_t i = 0; i < numEnums; i++)
    copyEnumerators[i]->Clear();
}

nsMsgThread *nsMsgDatabase::FindExistingThread(nsMsgKey threadId)
{
  uint32_t numThreads = m_threads.Length();
  for (uint32_t i = 0; i < numThreads; i++)
    if (m_threads[i]->m_threadKey == threadId)
      return m_threads[i];

  return nullptr;
}

void nsMsgDatabase::ClearThreads()
{
  // clear out existing threads
  nsTArray<nsMsgThread *> copyThreads;
  copyThreads.SwapElements(m_threads);

  uint32_t numThreads = copyThreads.Length();
  for (uint32_t i = 0; i < numThreads; i++)
    copyThreads[i]->Clear();
}

void nsMsgDatabase::ClearCachedObjects(bool dbGoingAway)
{
  ClearHdrCache(false);
#ifdef DEBUG_DavidBienvenu
  if (m_headersInUse && m_headersInUse->EntryCount() > 0)
  {
    NS_ASSERTION(false, "leaking headers");
    printf("leaking %d headers in %s\n", m_headersInUse->EntryCount(), (const char *) m_dbName);
  }
#endif
  m_cachedThread = nullptr;
  m_cachedThreadId = nsMsgKey_None;
  // We should only clear the use hdr cache when the db is going away, or we could
  // end up with multiple copies of the same logical msg hdr, which will lead to
  // ref-counting problems.
  if (dbGoingAway)
  {
    ClearUseHdrCache();
    ClearThreads();
  }
  m_thumb = nullptr;
}

nsresult nsMsgDatabase::ClearHdrCache(bool reInit)
{
  if (m_cachedHeaders)
  {
    // save this away in case we renter this code.
    PLDHashTable  *saveCachedHeaders = m_cachedHeaders;
    m_cachedHeaders = nullptr;
    for (auto iter = saveCachedHeaders->Iter(); !iter.Done(); iter.Next()) {
      auto element = static_cast<MsgHdrHashElement*>(iter.Get());
      if (element)
        NS_IF_RELEASE(element->mHdr);
    }

    if (reInit)
    {
      saveCachedHeaders->ClearAndPrepareForLength(m_cacheSize);
      m_cachedHeaders = saveCachedHeaders;
    }
    else
    {
      delete saveCachedHeaders;
    }
  }
  return NS_OK;
}

nsresult nsMsgDatabase::RemoveHdrFromCache(nsIMsgDBHdr *hdr, nsMsgKey key)
{
  if (m_cachedHeaders)
  {
    if (key == nsMsgKey_None)
      hdr->GetMessageKey(&key);

    PLDHashEntryHdr *entry =
      m_cachedHeaders->Search((const void *)(uintptr_t) key);
    if (entry)
    {
      m_cachedHeaders->Remove((void *)(uintptr_t) key);
      NS_RELEASE(hdr); // get rid of extra ref the cache was holding.
    }

  }
  return NS_OK;
}


nsresult nsMsgDatabase::GetHdrFromUseCache(nsMsgKey key, nsIMsgDBHdr* *result)
{
  if (!result)
    return NS_ERROR_NULL_POINTER;

  nsresult rv = NS_ERROR_FAILURE;

  *result = nullptr;

  if (m_headersInUse)
  {
    PLDHashEntryHdr *entry =
      m_headersInUse->Search((const void *)(uintptr_t) key);
    if (entry)
    {
      MsgHdrHashElement* element = static_cast<MsgHdrHashElement*>(entry);
      *result = element->mHdr;
    }
    if (*result)
    {
      NS_ADDREF(*result);
      rv = NS_OK;
    }
  }
  return rv;
}

PLDHashTableOps nsMsgDatabase::gMsgDBHashTableOps =
{
  HashKey,
  MatchEntry,
  MoveEntry,
  ClearEntry,
  nullptr
};

// HashKey is supposed to maximize entropy in the low order bits, and the key
// as is, should do that.
PLDHashNumber
nsMsgDatabase::HashKey(const void* aKey)
{
  return PLDHashNumber(NS_PTR_TO_INT32(aKey));
}

bool
nsMsgDatabase::MatchEntry(const PLDHashEntryHdr* aEntry, const void* aKey)
{
  const MsgHdrHashElement* hdr = static_cast<const MsgHdrHashElement*>(aEntry);
  return aKey == (const void *)(uintptr_t) hdr->mKey; // ### or get the key from the hdr...
}

void
nsMsgDatabase::MoveEntry(PLDHashTable* aTable, const PLDHashEntryHdr* aFrom, PLDHashEntryHdr* aTo)
{
  const MsgHdrHashElement* from = static_cast<const MsgHdrHashElement*>(aFrom);
  MsgHdrHashElement* to = static_cast<MsgHdrHashElement*>(aTo);
  // ### eh? Why is this needed? I don't think we have a copy operator?
  *to = *from;
}

void
nsMsgDatabase::ClearEntry(PLDHashTable* aTable, PLDHashEntryHdr* aEntry)
{
  MsgHdrHashElement* element = static_cast<MsgHdrHashElement*>(aEntry);
  element->mHdr = nullptr; // eh? Need to release this or not?
  element->mKey = nsMsgKey_None; // eh?
}


nsresult nsMsgDatabase::AddHdrToUseCache(nsIMsgDBHdr *hdr, nsMsgKey key)
{
  if (!m_headersInUse)
  {
    mdb_count numHdrs = MSG_HASH_SIZE;
    if (m_mdbAllMsgHeadersTable)
      m_mdbAllMsgHeadersTable->GetCount(GetEnv(), &numHdrs);
    m_headersInUse = new PLDHashTable(&gMsgDBHashTableOps, sizeof(struct MsgHdrHashElement), std::max((mdb_count)MSG_HASH_SIZE, numHdrs));
  }
  if (m_headersInUse)
  {
    if (key == nsMsgKey_None)
      hdr->GetMessageKey(&key);
    PLDHashEntryHdr *entry = m_headersInUse->Add((void *)(uintptr_t) key, mozilla::fallible);
    if (!entry)
      return NS_ERROR_OUT_OF_MEMORY; // XXX out of memory

    MsgHdrHashElement* element = static_cast<MsgHdrHashElement*>(entry);
    element->mHdr = hdr;
    element->mKey = key;
    // the hash table won't add ref, we'll do it ourselves
    // stand for the addref that CreateMsgHdr normally does.
    NS_ADDREF(hdr);
    return NS_OK;
  }

  return NS_ERROR_OUT_OF_MEMORY;
}

nsresult nsMsgDatabase::ClearUseHdrCache()
{
  if (m_headersInUse)
  {
    // clear mdb row pointers of any headers still in use, because the
    // underlying db is going away.
    for (auto iter = m_headersInUse->Iter(); !iter.Done(); iter.Next()) {
      auto element = static_cast<const MsgHdrHashElement*>(iter.Get());
      if (element && element->mHdr) {
        nsMsgHdr* msgHdr = static_cast<nsMsgHdr*>(element->mHdr);  // closed system, so this is ok
        // clear out m_mdbRow member variable - the db is going away, which means that this member
        // variable might very well point to a mork db that is gone.
        NS_IF_RELEASE(msgHdr->m_mdbRow);
    //    NS_IF_RELEASE(msgHdr->m_mdb);
      }
    }
    delete m_headersInUse;
    m_headersInUse = nullptr;
  }
  return NS_OK;
}

nsresult nsMsgDatabase::RemoveHdrFromUseCache(nsIMsgDBHdr *hdr, nsMsgKey key)
{
  if (m_headersInUse)
  {
    if (key == nsMsgKey_None)
      hdr->GetMessageKey(&key);

    m_headersInUse->Remove((void *)(uintptr_t) key);
  }
  return NS_OK;
}


nsresult
nsMsgDatabase::CreateMsgHdr(nsIMdbRow* hdrRow, nsMsgKey key, nsIMsgDBHdr* *result)
{
  NS_ENSURE_ARG_POINTER(hdrRow);
  NS_ENSURE_ARG_POINTER(result);

  nsresult rv = GetHdrFromUseCache(key, result);
  if (NS_SUCCEEDED(rv) && *result)
  {
    hdrRow->Release();
    return rv;
  }

  nsMsgHdr *msgHdr = new nsMsgHdr(this, hdrRow);
  if(!msgHdr)
    return NS_ERROR_OUT_OF_MEMORY;
  msgHdr->SetMessageKey(key);
  // don't need to addref here; GetHdrFromUseCache addrefs.
  *result = msgHdr;

  AddHdrToCache(msgHdr, key);

  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::AddListener(nsIDBChangeListener *aListener)
{
  NS_ENSURE_ARG_POINTER(aListener);
  m_ChangeListeners.AppendElementUnlessExists(aListener);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::RemoveListener(nsIDBChangeListener *aListener)
{
  NS_ENSURE_ARG_POINTER(aListener);
  m_ChangeListeners.RemoveElement(aListener);
  return NS_OK;
}

// XXX should we return rv for listener->propertyfunc_?
#define NOTIFY_LISTENERS(propertyfunc_, params_) \
  PR_BEGIN_MACRO \
  nsTObserverArray<nsCOMPtr<nsIDBChangeListener> >::ForwardIterator iter(m_ChangeListeners); \
  nsCOMPtr<nsIDBChangeListener> listener; \
  while (iter.HasMore()) { \
    listener = iter.GetNext(); \
    listener->propertyfunc_ params_; \
  } \
  PR_END_MACRO

// change announcer methods - just broadcast to all listeners.
NS_IMETHODIMP nsMsgDatabase::NotifyHdrChangeAll(nsIMsgDBHdr *aHdrChanged,
                                                uint32_t aOldFlags,
                                                uint32_t aNewFlags,
                                                nsIDBChangeListener *aInstigator)
{
  // We will only notify the change if the header exists in the database.
  // This allows database functions to be usable in both the case where the
  // header is in the db, or the header is not so no notifications should be
  // given.
  nsMsgKey key;
  bool inDb = false;
  if (aHdrChanged)
  {
    aHdrChanged->GetMessageKey(&key);
    ContainsKey(key, &inDb);
  }
  if (inDb)
    NOTIFY_LISTENERS(OnHdrFlagsChanged,
                     (aHdrChanged, aOldFlags, aNewFlags, aInstigator));
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::NotifyReadChanged(nsIDBChangeListener *aInstigator)
{
  NOTIFY_LISTENERS(OnReadChanged, (aInstigator));
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::NotifyJunkScoreChanged(nsIDBChangeListener *aInstigator)
{
  NOTIFY_LISTENERS(OnJunkScoreChanged, (aInstigator));
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::NotifyHdrDeletedAll(nsIMsgDBHdr *aHdrDeleted,
                                                 nsMsgKey aParentKey,
                                                 int32_t aFlags,
                                                 nsIDBChangeListener *aInstigator)
{
  NOTIFY_LISTENERS(OnHdrDeleted, (aHdrDeleted, aParentKey, aFlags, aInstigator));
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::NotifyHdrAddedAll(nsIMsgDBHdr *aHdrAdded,
                                               nsMsgKey aParentKey,
                                               int32_t aFlags,
                                               nsIDBChangeListener *aInstigator)
{
#ifdef DEBUG_bienvenu1
  printf("notifying add of %ld parent %ld\n", keyAdded, parentKey);
#endif
  NOTIFY_LISTENERS(OnHdrAdded, (aHdrAdded, aParentKey, aFlags, aInstigator));
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::NotifyParentChangedAll(nsMsgKey aKeyReparented,
                                                    nsMsgKey aOldParent,
                                                    nsMsgKey aNewParent,
                                                    nsIDBChangeListener *aInstigator)
{
  NOTIFY_LISTENERS(OnParentChanged,
                   (aKeyReparented, aOldParent, aNewParent, aInstigator));
  return NS_OK;
}


NS_IMETHODIMP nsMsgDatabase::NotifyAnnouncerGoingAway(void)
{
  NOTIFY_LISTENERS(OnAnnouncerGoingAway, (this));
  return NS_OK;
}

bool nsMsgDatabase::MatchDbName(nsIFile *dbName)  // returns true if they match
{
  nsCString dbPath;
#ifdef XP_WIN
  dbName->GetPersistentDescriptor(dbPath);
#else
  dbName->GetNativePath(dbPath);
#endif
  return dbPath.Equals(m_dbName);
}

void nsMsgDBService::AddToCache(nsMsgDatabase* pMessageDB)
{
#ifdef DEBUG_David_Bienvenu
  NS_ASSERTION(m_dbCache.Length() < 50, "50 or more open db's");
#endif
#ifdef DEBUG
  if (pMessageDB->m_folder)
  {
    nsCOMPtr<nsIMsgDatabase> msgDB;
    CachedDBForFolder(pMessageDB->m_folder, getter_AddRefs(msgDB));
    NS_ASSERTION(!msgDB, "shouldn't have db in cache");
  }
#endif
  m_dbCache.AppendElement(pMessageDB);
}

/**
 * Log the open db's, and how many headers are in memory.
 */
void nsMsgDBService::DumpCache()
{
  nsMsgDatabase* db = nullptr;
  MOZ_LOG(DBLog, LogLevel::Info, ("%d open DB's\n", m_dbCache.Length()));
  for (uint32_t i = 0; i < m_dbCache.Length(); i++)
  {
    db = m_dbCache.ElementAt(i);
    MOZ_LOG(DBLog, LogLevel::Info, ("%s - %ld hdrs in use\n",
      (const char*)db->m_dbName.get(),
      db->m_headersInUse ? db->m_headersInUse->EntryCount() : 0));
  }
}

// Memory Reporting implementations

size_t nsMsgDatabase::SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const
{
  size_t totalSize = 0;
  if (m_dbFolderInfo)
    totalSize += m_dbFolderInfo->SizeOfExcludingThis(aMallocSizeOf);
  if (m_mdbEnv)
  {
    nsIMdbHeap *morkHeap = nullptr;
    m_mdbEnv->GetHeap(&morkHeap);
    if (morkHeap)
      totalSize += morkHeap->GetUsedSize();
  }
  totalSize += m_newSet.ShallowSizeOfExcludingThis(aMallocSizeOf);
  totalSize += m_ChangeListeners.ShallowSizeOfExcludingThis(aMallocSizeOf);
  totalSize += m_threads.ShallowSizeOfExcludingThis(aMallocSizeOf);
  // We have two tables of header objects, but every header in m_cachedHeaders
  // should be in m_headersInUse.
  // double-counting...
  size_t headerSize = 0;
  if (m_headersInUse)
  {
    headerSize = m_headersInUse->ShallowSizeOfIncludingThis(aMallocSizeOf);
    for (auto iter = m_headersInUse->Iter(); !iter.Done(); iter.Next()) {
      auto entry = static_cast<MsgHdrHashElement*>(iter.Get());
      // Sigh, this is dangerous, but so long as this is a closed system, this
      // is safe.
      headerSize += static_cast<nsMsgHdr*>(entry->mHdr)->SizeOfIncludingThis(aMallocSizeOf);
    }
  }
  totalSize += headerSize;
  if (m_msgReferences)
    totalSize += m_msgReferences->ShallowSizeOfIncludingThis(aMallocSizeOf);
  return totalSize;
}

namespace mozilla {
namespace mailnews {

MOZ_DEFINE_MALLOC_SIZE_OF(GetMallocSize)

class MsgDBReporter final : public nsIMemoryReporter
{
  nsMsgDatabase *mDatabase;
public:
  explicit MsgDBReporter(nsMsgDatabase *db) : mDatabase(db) {}

  NS_DECL_ISUPPORTS
  NS_IMETHOD GetName(nsACString &aName)
  {
    aName.AssignLiteral("msg-database-objects");
    return NS_OK;
  }

  NS_IMETHOD CollectReports(nsIMemoryReporterCallback*aCb,
                            nsISupports* aClosure,
                            bool aAnonymize) override
  {
    nsCString path;
    GetPath(path, aAnonymize);
    return aCb->Callback(EmptyCString(), path,
                         nsIMemoryReporter::KIND_HEAP,
                         nsIMemoryReporter::UNITS_BYTES,
                         mDatabase->SizeOfIncludingThis(GetMallocSize),           
                         NS_LITERAL_CSTRING("Memory used for the folder database."),
                         aClosure);
  }

  void GetPath(nsACString &memoryPath, bool aAnonymize)
  {
    memoryPath.AssignLiteral("explicit/maildb/database(");
    nsCOMPtr<nsIMsgFolder> folder;
    mDatabase->GetFolder(getter_AddRefs(folder));
    if (folder)
    {
      if (aAnonymize)
        memoryPath.AppendLiteral("<anonymized>");
      else
      {
        nsAutoCString folderURL;
        folder->GetFolderURL(folderURL);
        MsgReplaceChar(folderURL, '/', '\\');
        memoryPath += folderURL;
      }
    } else {
      memoryPath.AppendLiteral("UNKNOWN-FOLDER");
    }
    memoryPath.Append(')');
  }

private:
  ~MsgDBReporter() {}
};

NS_IMPL_ISUPPORTS(MsgDBReporter, nsIMemoryReporter)
}
}

nsMsgDatabase::nsMsgDatabase()
        : m_dbFolderInfo(nullptr),
        m_nextPseudoMsgKey(kFirstPseudoKey),
        m_mdbEnv(nullptr), m_mdbStore(nullptr),
        m_mdbAllMsgHeadersTable(nullptr), m_mdbAllThreadsTable(nullptr),
        m_create(false),
        m_leaveInvalidDB(false),
        m_dbName(""),
        m_mdbTokensInitialized(false),
        m_hdrRowScopeToken(0),
        m_hdrTableKindToken(0),
        m_threadTableKindToken(0),
        m_subjectColumnToken(0),
        m_senderColumnToken(0),
        m_messageIdColumnToken(0),
        m_referencesColumnToken(0),
        m_recipientsColumnToken(0),
        m_dateColumnToken(0),
        m_messageSizeColumnToken(0),
        m_flagsColumnToken(0),
        m_priorityColumnToken(0),
        m_labelColumnToken(0),
        m_statusOffsetColumnToken(0),
        m_numLinesColumnToken(0),
        m_ccListColumnToken(0),
        m_bccListColumnToken(0),
        m_threadFlagsColumnToken(0),
        m_threadIdColumnToken(0),
        m_threadChildrenColumnToken(0),
        m_threadUnreadChildrenColumnToken(0),
        m_messageThreadIdColumnToken(0),
        m_threadSubjectColumnToken(0),
        m_messageCharSetColumnToken(0),
        m_threadParentColumnToken(0),
        m_threadRootKeyColumnToken(0),
        m_threadNewestMsgDateColumnToken(0),
        m_offlineMsgOffsetColumnToken(0),
        m_offlineMessageSizeColumnToken(0),
        m_headersInUse(nullptr),
        m_cachedHeaders(nullptr),
        m_bCacheHeaders(true),
        m_cachedThreadId(nsMsgKey_None),
        m_msgReferences(nullptr),
        m_cacheSize(kMaxHdrsInCache)
{
  mMemReporter = new mozilla::mailnews::MsgDBReporter(this);
  mozilla::RegisterWeakMemoryReporter(mMemReporter);
}

nsMsgDatabase::~nsMsgDatabase()
{
  mozilla::UnregisterWeakMemoryReporter(mMemReporter);
  //  Close(FALSE);  // better have already been closed.
  ClearCachedObjects(true);
  ClearEnumerators();
  delete m_cachedHeaders;
  delete m_headersInUse;

  if (m_msgReferences)
  {
    delete m_msgReferences;
    m_msgReferences = nullptr;
  }

  MOZ_LOG(DBLog, LogLevel::Info, ("closing database    %s\n",
    (const char*)m_dbName.get()));

  nsCOMPtr<nsIMsgDBService> serv(do_GetService(NS_MSGDB_SERVICE_CONTRACTID));
  if (serv)
    static_cast<nsMsgDBService*>(serv.get())->RemoveFromCache(this);

  // if the db folder info refers to the mdb db, we must clear it because
  // the reference will be a dangling one soon.
  if (m_dbFolderInfo)
    m_dbFolderInfo->ReleaseExternalReferences();

  NS_IF_RELEASE(m_dbFolderInfo);
  if (m_mdbAllMsgHeadersTable)
    m_mdbAllMsgHeadersTable->Release();

  if (m_mdbAllThreadsTable)
    m_mdbAllThreadsTable->Release();

  if (m_mdbStore)
    m_mdbStore->Release();

  if (m_mdbEnv)
  {
    m_mdbEnv->Release(); //??? is this right?
    m_mdbEnv = nullptr;
  }
  m_ChangeListeners.Clear();
}

NS_IMPL_ISUPPORTS(nsMsgDatabase, nsIMsgDatabase, nsIDBChangeAnnouncer)

void nsMsgDatabase::GetMDBFactory(nsIMdbFactory ** aMdbFactory)
{
  if (!mMdbFactory)
  {
    nsresult rv;
    nsCOMPtr <nsIMdbFactoryService> mdbFactoryService = do_GetService(NS_MORK_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv) && mdbFactoryService)
      mdbFactoryService->GetMdbFactory(getter_AddRefs(mMdbFactory));
  }
  NS_IF_ADDREF(*aMdbFactory = mMdbFactory);
}

// aLeaveInvalidDB: true if caller wants back a db even out of date.
// If so, they'll extract out the interesting info from the db, close it,
// delete it, and then try to open the db again, prior to reparsing.
nsresult nsMsgDatabase::Open(nsMsgDBService *aDBService, nsIFile *aFolderName,
                             bool aCreate, bool aLeaveInvalidDB)
{
  return nsMsgDatabase::OpenInternal(aDBService, aFolderName, aCreate, aLeaveInvalidDB,
                                     true /* open synchronously */);
}

nsresult nsMsgDatabase::OpenInternal(nsMsgDBService *aDBService,
                                     nsIFile *summaryFile, bool aCreate,
                                     bool aLeaveInvalidDB, bool sync)
{
  nsAutoCString summaryFilePath;
#ifdef XP_WIN
  summaryFile->GetPersistentDescriptor(summaryFilePath);
#else
  summaryFile->GetNativePath(summaryFilePath);
#endif

  MOZ_LOG(DBLog, LogLevel::Info, ("nsMsgDatabase::Open(%s, %s, %p, %s)\n",
    (const char*)summaryFilePath.get(), aCreate ? "TRUE":"FALSE",
    this, aLeaveInvalidDB ? "TRUE":"FALSE"));


  nsresult rv = OpenMDB(summaryFilePath.get(), aCreate, sync);
  if (NS_FAILED(rv))
    MOZ_LOG(DBLog, LogLevel::Info, ("error opening db %lx", rv));

  if (MOZ_LOG_TEST(DBLog, LogLevel::Debug))
    aDBService->DumpCache();

  if (rv == NS_ERROR_FILE_TARGET_DOES_NOT_EXIST)
    return rv;

  m_create = aCreate;
  m_leaveInvalidDB = aLeaveInvalidDB;
  if (!sync && NS_SUCCEEDED(rv))
  {
    aDBService->AddToCache(this);
    // remember open options for when the parsing is complete.
    return rv;
  }
  return CheckForErrors(rv, true, aDBService, summaryFile);
}

nsresult nsMsgDatabase::CheckForErrors(nsresult err, bool sync,
                                       nsMsgDBService *aDBService,
                                       nsIFile *summaryFile)
{
  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  bool summaryFileExists;
  bool newFile = false;
  bool deleteInvalidDB = false;

  bool exists;
  int64_t fileSize;
  summaryFile->Exists(&exists);
  summaryFile->GetFileSize(&fileSize);
  // if the old summary doesn't exist, we're creating a new one.
  if ((!exists || !fileSize) && m_create)
    newFile = true;

  summaryFileExists = exists && fileSize > 0;

  if (NS_SUCCEEDED(err))
  {
    if (!m_dbFolderInfo)
    {
      err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
    }
    else
    {
      if (!newFile && summaryFileExists)
      {
        bool valid = false;
        nsresult rv = GetSummaryValid(&valid);
        if (NS_FAILED(rv) || !valid)
          err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
      }
      // compare current version of db versus filed out version info.
      uint32_t version;
      m_dbFolderInfo->GetVersion(&version);
      if (GetCurVersion() != version)
        err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;

      // Check if we should force a reparse because, for example, we have
      // reached the key limit.
      bool forceReparse;
      m_dbFolderInfo->GetBooleanProperty("forceReparse", false, &forceReparse);
      if (forceReparse)
      {
        NS_WARNING("Forcing a reparse presumably because key limit reached");
        err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
      }

    }
    if (NS_FAILED(err) && !m_leaveInvalidDB)
      deleteInvalidDB = true;
  }
  else
  {
    err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
    deleteInvalidDB = true;
  }

  if (deleteInvalidDB)
  {
    // this will make the db folder info release its ref to the mail db...
    NS_IF_RELEASE(m_dbFolderInfo);
    ForceClosed();
    if (err == NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE)
      summaryFile->Remove(false);
  }
  if (NS_FAILED(err) || newFile)
  {
    // if we couldn't open file, or we have a blank one, and we're supposed
    // to upgrade, updgrade it.
    if (newFile && !m_leaveInvalidDB)  // caller is upgrading, and we have empty summary file,
    {          // leave db around and open so caller can upgrade it.
      err = NS_MSG_ERROR_FOLDER_SUMMARY_MISSING;
    }
    else if (NS_FAILED(err) && err != NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE)
    {
      Close(false);
      summaryFile->Remove(false);  // blow away the db if it's corrupt.
    }
  }
  if (sync && (NS_SUCCEEDED(err) || err == NS_MSG_ERROR_FOLDER_SUMMARY_MISSING))
    aDBService->AddToCache(this);
  return (summaryFileExists) ? err : NS_MSG_ERROR_FOLDER_SUMMARY_MISSING;
}

/**
 * Open the MDB database synchronously or async based on sync argument.
 * If successful, this routine will set up the m_mdbStore and m_mdbEnv of
 * the database object so other database calls can work.
 */
nsresult nsMsgDatabase::OpenMDB(const char *dbName, bool create, bool sync)
{
  nsresult ret = NS_OK;
  nsCOMPtr<nsIMdbFactory> mdbFactory;
  GetMDBFactory(getter_AddRefs(mdbFactory));
  if (mdbFactory)
  {
    ret = mdbFactory->MakeEnv(NULL, &m_mdbEnv);
    if (NS_SUCCEEDED(ret))
    {
      nsIMdbHeap* dbHeap = nullptr;

      if (m_mdbEnv)
        m_mdbEnv->SetAutoClear(true);
      m_dbName = dbName;
      bool exists = false;
      nsCOMPtr<nsIFile> dbFile = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &ret);
      if (NS_SUCCEEDED(ret) && dbFile) {
        ret = dbFile->InitWithNativePath(m_dbName);
        if (NS_SUCCEEDED(ret))
          ret = dbFile->Exists(&exists);
      }
      if (!exists)
      {
        ret = NS_MSG_ERROR_FOLDER_SUMMARY_MISSING;
      }
      // If m_thumb is set, we're asynchronously opening the db already.
      else if (!m_thumb) 
      {
        mdbOpenPolicy inOpenPolicy;
        mdb_bool  canOpen;
        mdbYarn    outFormatVersion;

        nsIMdbFile* oldFile = nullptr;
        ret = mdbFactory->OpenOldFile(m_mdbEnv, dbHeap, dbName,
                                      mdbBool_kFalse, // not readonly, we want modifiable
                                      &oldFile);
        if (oldFile)
        {
          if (NS_SUCCEEDED(ret))
          {
            ret = mdbFactory->CanOpenFilePort(m_mdbEnv, oldFile, // the file to investigate
              &canOpen, &outFormatVersion);
            if (NS_SUCCEEDED(ret) && canOpen)
            {
              inOpenPolicy.mOpenPolicy_ScopePlan.mScopeStringSet_Count = 0;
              inOpenPolicy.mOpenPolicy_MinMemory = 0;
              inOpenPolicy.mOpenPolicy_MaxLazy = 0;

              ret = mdbFactory->OpenFileStore(m_mdbEnv, dbHeap,
                oldFile, &inOpenPolicy, getter_AddRefs(m_thumb));
            }
            else
              ret = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
          }
          NS_RELEASE(oldFile); // always release our file ref, store has own
        }
      }
      if (NS_SUCCEEDED(ret) && m_thumb && sync)
      {
        mdb_count outTotal;    // total somethings to do in operation
        mdb_count outCurrent;  // subportion of total completed so far
        mdb_bool outDone = false;      // is operation finished?
        mdb_bool outBroken;     // is operation irreparably dead and broken?
        do
        {
          ret = m_thumb->DoMore(m_mdbEnv, &outTotal, &outCurrent, &outDone, &outBroken);
          if (NS_FAILED(ret))
          {// mork isn't really doing NS errors yet.
            outDone = true;
            break;
          }
        }
        while (NS_SUCCEEDED(ret) && !outBroken && !outDone);
        //        m_mdbEnv->ClearErrors(); // ### temporary...
        // only 0 is a non-error return.
        if (NS_SUCCEEDED(ret) && outDone)
        {
          ret = mdbFactory->ThumbToOpenStore(m_mdbEnv, m_thumb, &m_mdbStore);
          if (NS_SUCCEEDED(ret))
            ret = (m_mdbStore) ? InitExistingDB() : NS_ERROR_FAILURE;
        }
#ifdef DEBUG_bienvenu1
        DumpContents();
#endif
        m_thumb = nullptr;
      }
      else if (create)  // ### need error code saying why open file store failed
      {
        nsIMdbFile* newFile = 0;
        ret = mdbFactory->CreateNewFile(m_mdbEnv, dbHeap, dbName, &newFile);
        if (NS_FAILED(ret))
          ret = NS_ERROR_FILE_TARGET_DOES_NOT_EXIST;
        if ( newFile )
        {
          if (NS_SUCCEEDED(ret))
          {
            mdbOpenPolicy inOpenPolicy;

            inOpenPolicy.mOpenPolicy_ScopePlan.mScopeStringSet_Count = 0;
            inOpenPolicy.mOpenPolicy_MinMemory = 0;
            inOpenPolicy.mOpenPolicy_MaxLazy = 0;

            ret = mdbFactory->CreateNewFileStore(m_mdbEnv, dbHeap,
              newFile, &inOpenPolicy, &m_mdbStore);
            if (NS_SUCCEEDED(ret))
              ret = (m_mdbStore) ? InitNewDB() : NS_ERROR_FAILURE;
          }
          NS_RELEASE(newFile); // always release our file ref, store has own
        }
      }
    }
  }
#ifdef DEBUG_David_Bienvenu
//  NS_ASSERTION(NS_SUCCEEDED(ret), "failed opening mdb");
#endif
  return ret;
}

nsresult nsMsgDatabase::CloseMDB(bool commit)
{
  if (commit)
    Commit(nsMsgDBCommitType::kSessionCommit);
  return(NS_OK);
}


// force the database to close - this'll flush out anybody holding onto
// a database without having a listener!
// This is evil in the com world, but there are times we need to delete the file.
NS_IMETHODIMP nsMsgDatabase::ForceClosed()
{
  nsresult  err = NS_OK;

  // make sure someone has a reference so object won't get deleted out from under us.
  AddRef();
  NotifyAnnouncerGoingAway();
  // make sure dbFolderInfo isn't holding onto mork stuff because mork db is going away
  if (m_dbFolderInfo)
    m_dbFolderInfo->ReleaseExternalReferences();
  NS_IF_RELEASE(m_dbFolderInfo);

  err = CloseMDB(true);  // Backup DB will try to recover info, so commit
  ClearCachedObjects(true);
  ClearEnumerators();
if (m_mdbAllMsgHeadersTable)
  {
    m_mdbAllMsgHeadersTable->Release();
    m_mdbAllMsgHeadersTable = nullptr;
  }
  if (m_mdbAllThreadsTable)
  {
    m_mdbAllThreadsTable->Release();
    m_mdbAllThreadsTable = nullptr;
  }
  if (m_mdbStore)
  {
    m_mdbStore->Release();
    m_mdbStore = nullptr;
  }

  // better not be any listeners, because we're going away.
  NS_ASSERTION(m_ChangeListeners.IsEmpty(), "shouldn't have any listeners left");

  Release();
  return err;
}

NS_IMETHODIMP nsMsgDatabase::GetDBFolderInfo(nsIDBFolderInfo  **result)
{
  if (!m_dbFolderInfo)
  {
    NS_ERROR("db must be corrupt");
    return NS_ERROR_NULL_POINTER;
  }
  NS_ADDREF(*result = m_dbFolderInfo);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetFolder(nsIMsgFolder **aFolder)
{
  NS_ENSURE_ARG_POINTER(aFolder);
  NS_IF_ADDREF(*aFolder = m_folder);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::Commit(nsMsgDBCommit commitType)
{
  nsresult  err = NS_OK;
  nsCOMPtr<nsIMdbThumb> commitThumb;

  RememberLastUseTime();
  if (commitType == nsMsgDBCommitType::kLargeCommit || commitType == nsMsgDBCommitType::kSessionCommit)
  {
    mdb_percent outActualWaste = 0;
    mdb_bool outShould;
    if (m_mdbStore) {
      err = m_mdbStore->ShouldCompress(GetEnv(), 30, &outActualWaste, &outShould);
      if (NS_SUCCEEDED(err) && outShould)
        commitType = nsMsgDBCommitType::kCompressCommit;
    }
  }
  //  commitType = nsMsgDBCommitType::kCompressCommit;  // ### until incremental writing works.

  if (m_mdbStore)
  {
    switch (commitType)
    {
    case nsMsgDBCommitType::kLargeCommit:
      err = m_mdbStore->LargeCommit(GetEnv(), getter_AddRefs(commitThumb));
      break;
    case nsMsgDBCommitType::kSessionCommit:
      err = m_mdbStore->SessionCommit(GetEnv(), getter_AddRefs(commitThumb));
      break;
    case nsMsgDBCommitType::kCompressCommit:
      err = m_mdbStore->CompressCommit(GetEnv(), getter_AddRefs(commitThumb));
      break;
    }
  }
  if (commitThumb)
  {
    mdb_count outTotal = 0;    // total somethings to do in operation
    mdb_count outCurrent = 0;  // subportion of total completed so far
    mdb_bool outDone = false;      // is operation finished?
    mdb_bool outBroken = false;     // is operation irreparably dead and broken?
    while (!outDone && !outBroken && NS_SUCCEEDED(err))
    {
      err = commitThumb->DoMore(GetEnv(), &outTotal, &outCurrent, &outDone, &outBroken);
    }

  }
  // ### do something with error, but clear it now because mork errors out on commits.
  if (GetEnv())
    GetEnv()->ClearErrors();

  nsresult rv;
  nsCOMPtr<nsIMsgAccountManager> accountManager =
    do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv) && accountManager)
  {
    nsCOMPtr<nsIMsgFolderCache> folderCache;

    rv = accountManager->GetFolderCache(getter_AddRefs(folderCache));
    if (NS_SUCCEEDED(rv) && folderCache)
    {
      nsCOMPtr <nsIMsgFolderCacheElement> cacheElement;
      rv = folderCache->GetCacheElement(m_dbName, false, getter_AddRefs(cacheElement));
      if (NS_SUCCEEDED(rv) && cacheElement && m_dbFolderInfo)
      {
        int32_t totalMessages, unreadMessages, pendingMessages, pendingUnreadMessages;

        m_dbFolderInfo->GetNumMessages(&totalMessages);
        m_dbFolderInfo->GetNumUnreadMessages(&unreadMessages);
        m_dbFolderInfo->GetImapUnreadPendingMessages(&pendingUnreadMessages);
        m_dbFolderInfo->GetImapTotalPendingMessages(&pendingMessages);
        cacheElement->SetInt32Property("totalMsgs", totalMessages);
        cacheElement->SetInt32Property("totalUnreadMsgs", unreadMessages);
        cacheElement->SetInt32Property("pendingMsgs", pendingMessages);
        cacheElement->SetInt32Property("pendingUnreadMsgs", pendingUnreadMessages);
        folderCache->Commit(false);
      }
    }
  }

  return err;
}

NS_IMETHODIMP nsMsgDatabase::Close(bool forceCommit /* = TRUE */)
{
  return CloseMDB(forceCommit);
}

const char *kMsgHdrsScope = "ns:msg:db:row:scope:msgs:all";  // scope for all headers table
const char *kMsgHdrsTableKind = "ns:msg:db:table:kind:msgs";
const char *kThreadTableKind = "ns:msg:db:table:kind:thread";
const char *kThreadHdrsScope = "ns:msg:db:row:scope:threads:all"; // scope for all threads table
const char *kAllThreadsTableKind = "ns:msg:db:table:kind:allthreads"; // kind for table of all threads
const char *kSubjectColumnName = "subject";
const char *kSenderColumnName = "sender";
const char *kMessageIdColumnName = "message-id";
const char *kReferencesColumnName = "references";
const char *kRecipientsColumnName = "recipients";
const char *kDateColumnName = "date";
const char *kMessageSizeColumnName = "size";
const char *kFlagsColumnName = "flags";
const char *kPriorityColumnName = "priority";
const char *kLabelColumnName = "label";
const char *kStatusOffsetColumnName = "statusOfset";
const char *kNumLinesColumnName = "numLines";
const char *kCCListColumnName = "ccList";
const char *kBCCListColumnName = "bccList";
const char *kMessageThreadIdColumnName = "msgThreadId";
const char *kThreadFlagsColumnName = "threadFlags";
const char *kThreadIdColumnName = "threadId";
const char *kThreadChildrenColumnName = "children";
const char *kThreadUnreadChildrenColumnName = "unreadChildren";
const char *kThreadSubjectColumnName = "threadSubject";
const char *kMessageCharSetColumnName = "msgCharSet";
const char *kThreadParentColumnName = "threadParent";
const char *kThreadRootColumnName = "threadRoot";
const char *kThreadNewestMsgDateColumnName = "threadNewestMsgDate";
const char *kOfflineMsgOffsetColumnName = "msgOffset";
const char *kOfflineMsgSizeColumnName = "offlineMsgSize";
struct mdbOid gAllMsgHdrsTableOID;
struct mdbOid gAllThreadsTableOID;
const char *kFixedBadRefThreadingProp = "fixedBadRefThreading";

// set up empty tables, dbFolderInfo, etc.
nsresult nsMsgDatabase::InitNewDB()
{
  nsresult err = NS_OK;

  err = InitMDBInfo();
  if (NS_SUCCEEDED(err))
  {
    nsDBFolderInfo *dbFolderInfo = new nsDBFolderInfo(this);
    if (dbFolderInfo)
    {
      NS_ADDREF(dbFolderInfo);
      err = dbFolderInfo->AddToNewMDB();
      dbFolderInfo->SetVersion(GetCurVersion());
      dbFolderInfo->SetBooleanProperty("forceReparse", false);
      dbFolderInfo->SetBooleanProperty(kFixedBadRefThreadingProp, true);
      nsIMdbStore *store = GetStore();
      // create the unique table for the dbFolderInfo.
      struct mdbOid allMsgHdrsTableOID;
      struct mdbOid allThreadsTableOID;
      if (!store)
        return NS_ERROR_NULL_POINTER;

      allMsgHdrsTableOID.mOid_Scope = m_hdrRowScopeToken;
      allMsgHdrsTableOID.mOid_Id = kAllMsgHdrsTableKey;
      allThreadsTableOID.mOid_Scope = m_threadRowScopeToken;
      allThreadsTableOID.mOid_Id = kAllThreadsTableKey;

      // TODO: check this error value?
      (void) store->NewTableWithOid(GetEnv(), &allMsgHdrsTableOID, m_hdrTableKindToken,
        false, nullptr, &m_mdbAllMsgHeadersTable);

      // error here is not fatal.
      (void) store->NewTableWithOid(GetEnv(), &allThreadsTableOID, m_allThreadsTableKindToken,
        false, nullptr, &m_mdbAllThreadsTable);

      m_dbFolderInfo = dbFolderInfo;

    }
    else
      err = NS_ERROR_OUT_OF_MEMORY;
  }
  return err;
}

nsresult nsMsgDatabase::GetTableCreateIfMissing(const char *scope, const char *kind, nsIMdbTable **table,
                                                mdb_token &scopeToken, mdb_token &kindToken)
{
  struct mdbOid tableOID;

  if (!m_mdbStore)
    return NS_ERROR_FAILURE;
  (void) m_mdbStore->StringToToken(GetEnv(), scope, &scopeToken);
  (void) m_mdbStore->StringToToken(GetEnv(), kind, &kindToken);
  tableOID.mOid_Scope = scopeToken;
  tableOID.mOid_Id = 1;

  nsresult rv = m_mdbStore->GetTable(GetEnv(), &tableOID, table);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // create new all all offline ops table, if it doesn't exist.
  if (NS_SUCCEEDED(rv) && !*table)
  {
    rv = m_mdbStore->NewTable(GetEnv(), scopeToken,kindToken,
                              false, nullptr, table);
    if (NS_FAILED(rv) || !*table)
      rv = NS_ERROR_FAILURE;
  }
  NS_ASSERTION(NS_SUCCEEDED(rv), "couldn't create offline ops table");
  return rv;
}

nsresult nsMsgDatabase::InitExistingDB()
{
  nsresult err = NS_OK;

  err = InitMDBInfo();
  if (NS_SUCCEEDED(err))
  {
    err = GetStore()->GetTable(GetEnv(), &gAllMsgHdrsTableOID, &m_mdbAllMsgHeadersTable);
    if (NS_SUCCEEDED(err))
    {
      m_dbFolderInfo = new nsDBFolderInfo(this);
      if (m_dbFolderInfo)
      {
        NS_ADDREF(m_dbFolderInfo);
        err = m_dbFolderInfo->InitFromExistingDB();
      }
    }
    else
      err = NS_ERROR_FAILURE;

    NS_ASSERTION(NS_SUCCEEDED(err), "failed initing existing db");
    NS_ENSURE_SUCCESS(err, err);
    // create new all msg hdrs table, if it doesn't exist.
    if (NS_SUCCEEDED(err) && !m_mdbAllMsgHeadersTable)
    {
      struct mdbOid allMsgHdrsTableOID;
      allMsgHdrsTableOID.mOid_Scope = m_hdrRowScopeToken;
      allMsgHdrsTableOID.mOid_Id = kAllMsgHdrsTableKey;

      nsresult mdberr = GetStore()->NewTableWithOid(GetEnv(), &allMsgHdrsTableOID, m_hdrTableKindToken,
        false, nullptr, &m_mdbAllMsgHeadersTable);
      if (NS_FAILED(mdberr) || !m_mdbAllMsgHeadersTable)
        err = NS_ERROR_FAILURE;
    }
    struct mdbOid allThreadsTableOID;
    allThreadsTableOID.mOid_Scope = m_threadRowScopeToken;
    allThreadsTableOID.mOid_Id = kAllThreadsTableKey;
    err = GetStore()->GetTable(GetEnv(), &gAllThreadsTableOID, &m_mdbAllThreadsTable);
    if (!m_mdbAllThreadsTable)
    {

      nsresult mdberr = GetStore()->NewTableWithOid(GetEnv(), &allThreadsTableOID, m_allThreadsTableKindToken,
        false, nullptr, &m_mdbAllThreadsTable);
      if (NS_FAILED(mdberr) || !m_mdbAllThreadsTable)
        err = NS_ERROR_FAILURE;
    }
  }
  if (NS_SUCCEEDED(err) && m_dbFolderInfo)
  {
    bool fixedBadRefThreading;
    m_dbFolderInfo->GetBooleanProperty(kFixedBadRefThreadingProp, false, &fixedBadRefThreading);
    if (!fixedBadRefThreading)
    {
      nsCOMPtr <nsISimpleEnumerator> enumerator;
      err = EnumerateMessages(getter_AddRefs(enumerator));
      if (NS_SUCCEEDED(err) && enumerator)
      {
        bool hasMore;

        while (NS_SUCCEEDED(err = enumerator->HasMoreElements(&hasMore)) &&
               hasMore)
        {
          nsCOMPtr <nsISupports> supports;
          err = enumerator->GetNext(getter_AddRefs(supports));
          NS_ASSERTION(NS_SUCCEEDED(err), "nsMsgDBEnumerator broken");
          nsCOMPtr <nsIMsgDBHdr> msgHdr = do_QueryInterface(supports);
          if (msgHdr && NS_SUCCEEDED(err))
          {
            nsCString messageId;
            nsAutoCString firstReference;
            msgHdr->GetMessageId(getter_Copies(messageId));
            msgHdr->GetStringReference(0, firstReference);
            if (messageId.Equals(firstReference))
            {
              err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
              break;
            }
          }
        }
      }

      m_dbFolderInfo->SetBooleanProperty(kFixedBadRefThreadingProp, true);
    }

  }
  return err;
}

// initialize the various tokens and tables in our db's env
nsresult nsMsgDatabase::InitMDBInfo()
{
  nsresult err = NS_OK;

  if (!m_mdbTokensInitialized && GetStore())
  {
    m_mdbTokensInitialized = true;
    err  = GetStore()->StringToToken(GetEnv(), kMsgHdrsScope, &m_hdrRowScopeToken);
    if (NS_SUCCEEDED(err))
    {
      GetStore()->StringToToken(GetEnv(),  kSubjectColumnName, &m_subjectColumnToken);
      GetStore()->StringToToken(GetEnv(),  kSenderColumnName, &m_senderColumnToken);
      GetStore()->StringToToken(GetEnv(),  kMessageIdColumnName, &m_messageIdColumnToken);
      // if we just store references as a string, we won't get any savings from the
      // fact there's a lot of duplication. So we may want to break them up into
      // multiple columns, r1, r2, etc.
      GetStore()->StringToToken(GetEnv(),  kReferencesColumnName, &m_referencesColumnToken);
      // similarly, recipients could be tokenized properties
      GetStore()->StringToToken(GetEnv(),  kRecipientsColumnName, &m_recipientsColumnToken);
      GetStore()->StringToToken(GetEnv(),  kDateColumnName, &m_dateColumnToken);
      GetStore()->StringToToken(GetEnv(),  kMessageSizeColumnName, &m_messageSizeColumnToken);
      GetStore()->StringToToken(GetEnv(),  kFlagsColumnName, &m_flagsColumnToken);
      GetStore()->StringToToken(GetEnv(),  kPriorityColumnName, &m_priorityColumnToken);
      GetStore()->StringToToken(GetEnv(),  kLabelColumnName, &m_labelColumnToken);
      GetStore()->StringToToken(GetEnv(),  kStatusOffsetColumnName, &m_statusOffsetColumnToken);
      GetStore()->StringToToken(GetEnv(),  kNumLinesColumnName, &m_numLinesColumnToken);
      GetStore()->StringToToken(GetEnv(),  kCCListColumnName, &m_ccListColumnToken);
      GetStore()->StringToToken(GetEnv(),  kBCCListColumnName, &m_bccListColumnToken);
      GetStore()->StringToToken(GetEnv(),  kMessageThreadIdColumnName, &m_messageThreadIdColumnToken);
      GetStore()->StringToToken(GetEnv(),  kThreadIdColumnName, &m_threadIdColumnToken);
      GetStore()->StringToToken(GetEnv(),  kThreadFlagsColumnName, &m_threadFlagsColumnToken);
      GetStore()->StringToToken(GetEnv(),  kThreadNewestMsgDateColumnName, &m_threadNewestMsgDateColumnToken);
      GetStore()->StringToToken(GetEnv(),  kThreadChildrenColumnName, &m_threadChildrenColumnToken);
      GetStore()->StringToToken(GetEnv(),  kThreadUnreadChildrenColumnName, &m_threadUnreadChildrenColumnToken);
      GetStore()->StringToToken(GetEnv(),  kThreadSubjectColumnName, &m_threadSubjectColumnToken);
      GetStore()->StringToToken(GetEnv(),  kMessageCharSetColumnName, &m_messageCharSetColumnToken);
      err = GetStore()->StringToToken(GetEnv(), kMsgHdrsTableKind, &m_hdrTableKindToken);
      if (NS_SUCCEEDED(err))
        err = GetStore()->StringToToken(GetEnv(), kThreadTableKind, &m_threadTableKindToken);
      err = GetStore()->StringToToken(GetEnv(), kAllThreadsTableKind, &m_allThreadsTableKindToken);
      err  = GetStore()->StringToToken(GetEnv(), kThreadHdrsScope, &m_threadRowScopeToken);
      err  = GetStore()->StringToToken(GetEnv(), kThreadParentColumnName, &m_threadParentColumnToken);
      err  = GetStore()->StringToToken(GetEnv(), kThreadRootColumnName, &m_threadRootKeyColumnToken);
      err = GetStore()->StringToToken(GetEnv(), kOfflineMsgOffsetColumnName, &m_offlineMsgOffsetColumnToken);
      err = GetStore()->StringToToken(GetEnv(), kOfflineMsgSizeColumnName, &m_offlineMessageSizeColumnToken);

      if (NS_SUCCEEDED(err))
      {
        // The table of all message hdrs will have table id 1.
        gAllMsgHdrsTableOID.mOid_Scope = m_hdrRowScopeToken;
        gAllMsgHdrsTableOID.mOid_Id = kAllMsgHdrsTableKey;
        gAllThreadsTableOID.mOid_Scope = m_threadRowScopeToken;
        gAllThreadsTableOID.mOid_Id = kAllThreadsTableKey;

      }
    }
  }
  return err;
}

// Returns if the db contains this key
NS_IMETHODIMP nsMsgDatabase::ContainsKey(nsMsgKey key, bool *containsKey)
{

  nsresult  err = NS_OK;
  mdb_bool  hasOid;
  mdbOid    rowObjectId;

  if (!containsKey || !m_mdbAllMsgHeadersTable)
    return NS_ERROR_NULL_POINTER;
  *containsKey = false;

  rowObjectId.mOid_Id = key;
  rowObjectId.mOid_Scope = m_hdrRowScopeToken;
  err = m_mdbAllMsgHeadersTable->HasOid(GetEnv(), &rowObjectId, &hasOid);
  if(NS_SUCCEEDED(err))
    *containsKey = hasOid;

  return err;
}

// get a message header for the given key. Caller must release()!
NS_IMETHODIMP nsMsgDatabase::GetMsgHdrForKey(nsMsgKey key, nsIMsgDBHdr **pmsgHdr)
{
  nsresult  err = NS_OK;
  mdb_bool  hasOid;
  mdbOid    rowObjectId;

  // Because this may be called a lot, and we don't want gettimeofday() to show
  // up in trace logs, we just remember the most recent time any db was used,
  // which should be close enough for our purposes.
  m_lastUseTime = gLastUseTime;

#ifdef DEBUG_bienvenu1
  NS_ASSERTION(m_folder, "folder should be set");
#endif

  if (!pmsgHdr || !m_mdbAllMsgHeadersTable || !m_mdbStore)
    return NS_ERROR_NULL_POINTER;

  *pmsgHdr = NULL;
  err = GetHdrFromUseCache(key, pmsgHdr);
  if (NS_SUCCEEDED(err) && *pmsgHdr)
    return err;

  rowObjectId.mOid_Id = key;
  rowObjectId.mOid_Scope = m_hdrRowScopeToken;
  err = m_mdbAllMsgHeadersTable->HasOid(GetEnv(), &rowObjectId, &hasOid);
  if (NS_SUCCEEDED(err) /* && hasOid */)
  {
    nsIMdbRow *hdrRow;
    err = m_mdbStore->GetRow(GetEnv(), &rowObjectId, &hdrRow);

    if (NS_SUCCEEDED(err))
    {
      if (!hdrRow)
      {
        err = NS_ERROR_NULL_POINTER;
      }
      else
      {
        //        NS_ASSERTION(hasOid, "we had oid, right?");
        err = CreateMsgHdr(hdrRow,  key, pmsgHdr);
      }
    }
  }

  return err;
}

NS_IMETHODIMP nsMsgDatabase::StartBatch()
{
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::EndBatch()
{
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::DeleteMessage(nsMsgKey key, nsIDBChangeListener *instigator, bool commit)
{
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  nsresult rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (!msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND;

  rv = DeleteHeader(msgHdr, instigator, commit, true);
  return rv;
}


NS_IMETHODIMP nsMsgDatabase::DeleteMessages(uint32_t aNumKeys, nsMsgKey* nsMsgKeys, nsIDBChangeListener *instigator)
{
  nsresult  err = NS_OK;

  uint32_t kindex;
  for (kindex = 0; kindex < aNumKeys; kindex++)
  {
    nsMsgKey key = nsMsgKeys[kindex];
    nsCOMPtr <nsIMsgDBHdr> msgHdr;

    bool hasKey;

    if (NS_SUCCEEDED(ContainsKey(key, &hasKey)) && hasKey)
    {
      err = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
      if (NS_FAILED(err))
      {
        err = NS_MSG_MESSAGE_NOT_FOUND;
        break;
      }
      if (msgHdr)
        err = DeleteHeader(msgHdr, instigator, kindex % 300 == 0, true);
      if (NS_FAILED(err))
        break;
    }
  }
  return err;
}

nsresult nsMsgDatabase::AdjustExpungedBytesOnDelete(nsIMsgDBHdr *msgHdr)
{
  uint32_t size = 0;
  (void)msgHdr->GetMessageSize(&size);
  return m_dbFolderInfo->ChangeExpungedBytes (size);
}

NS_IMETHODIMP nsMsgDatabase::DeleteHeader(nsIMsgDBHdr *msg, nsIDBChangeListener *instigator, bool commit, bool notify)
{
  if (!msg)
    return NS_ERROR_NULL_POINTER;

  nsMsgHdr* msgHdr = static_cast<nsMsgHdr*>(msg);  // closed system, so this is ok
  nsMsgKey key;
  (void)msg->GetMessageKey(&key);
  // only need to do this for mail - will this speed up news expiration?
  SetHdrFlag(msg, true, nsMsgMessageFlags::Expunged);  // tell mailbox (mail)

  bool hdrWasNew = m_newSet.BinaryIndexOf(key) != m_newSet.NoIndex;
  m_newSet.RemoveElement(key);

  if (m_dbFolderInfo != NULL)
  {
    bool isRead;
    m_dbFolderInfo->ChangeNumMessages(-1);
    IsRead(key, &isRead);
    if (!isRead)
      m_dbFolderInfo->ChangeNumUnreadMessages(-1);
    AdjustExpungedBytesOnDelete(msg);
  }

  uint32_t flags;
  nsMsgKey threadParent;

  //Save off flags and threadparent since they will no longer exist after we remove the header from the db.
  if (notify)
  {
    (void)msg->GetFlags(&flags);
    msg->GetThreadParent(&threadParent);
  }

  RemoveHeaderFromThread(msgHdr);
  if (notify)
  {
    // If deleted hdr was new, restore the new flag on flags 
    // so saved searches will know to reduce their new msg count.
    if (hdrWasNew)
      flags |= nsMsgMessageFlags::New;
    NotifyHdrDeletedAll(msg, threadParent, flags, instigator); // tell listeners
  }
  //  if (!onlyRemoveFromThread)  // to speed up expiration, try this. But really need to do this in RemoveHeaderFromDB
  nsresult ret = RemoveHeaderFromDB(msgHdr);


  if (commit)
    Commit(nsMsgDBCommitType::kLargeCommit);      // ### dmb is this a good time to commit?
  return ret;
}

NS_IMETHODIMP
nsMsgDatabase::UndoDelete(nsIMsgDBHdr *aMsgHdr)
{
  if (aMsgHdr)
  {
    // Force deleted flag, so SetHdrFlag won't bail out because deleted flag isn't set.
    uint32_t result;
    aMsgHdr->OrFlags(nsMsgMessageFlags::Expunged, &result);
    SetHdrFlag(aMsgHdr, false, nsMsgMessageFlags::Expunged);  // Clear deleted flag in db.
  }
  return NS_OK;
}

nsresult nsMsgDatabase::RemoveHeaderFromThread(nsMsgHdr *msgHdr)
{
  if (!msgHdr)
    return NS_ERROR_NULL_POINTER;
  nsresult ret = NS_OK;
  nsCOMPtr <nsIMsgThread> thread ;
  ret = GetThreadContainingMsgHdr(msgHdr, getter_AddRefs(thread));
  if (NS_SUCCEEDED(ret) && thread)
  {
    nsCOMPtr <nsIDBChangeAnnouncer> announcer = do_QueryInterface(this);
    ret = thread->RemoveChildHdr(msgHdr, announcer);
  }
  return ret;
}

NS_IMETHODIMP nsMsgDatabase::RemoveHeaderMdbRow(nsIMsgDBHdr *msg)
{
  NS_ENSURE_ARG_POINTER(msg);
  nsMsgHdr* msgHdr = static_cast<nsMsgHdr*>(msg);  // closed system, so this is ok
  return RemoveHeaderFromDB(msgHdr);
}

// This is a lower level routine which doesn't send notifcations or
// update folder info. One use is when a rule fires moving a header
// from one db to another, to remove it from the first db.

nsresult nsMsgDatabase::RemoveHeaderFromDB(nsMsgHdr *msgHdr)
{
  if (!msgHdr)
    return NS_ERROR_NULL_POINTER;
  nsresult ret = NS_OK;

  RemoveHdrFromCache(msgHdr, nsMsgKey_None);
  if (UseCorrectThreading())
    RemoveMsgRefsFromHash(msgHdr);
  nsIMdbRow* row = msgHdr->GetMDBRow();
  if (row)
  {
    ret = m_mdbAllMsgHeadersTable->CutRow(GetEnv(), row);
    row->CutAllColumns(GetEnv());
  }
  msgHdr->ClearCachedValues();
  return ret;
}

nsresult nsMsgDatabase::IsRead(nsMsgKey key, bool *pRead)
{
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  nsresult rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?
  rv = IsHeaderRead(msgHdr, pRead);
  return rv;
}

uint32_t  nsMsgDatabase::GetStatusFlags(nsIMsgDBHdr *msgHdr, uint32_t origFlags)
{
  uint32_t  statusFlags = origFlags;
  bool    isRead = true;

  nsMsgKey key;
  (void)msgHdr->GetMessageKey(&key);
  if ((!m_newSet.IsEmpty() && m_newSet[m_newSet.Length() - 1] == key) ||
      (m_newSet.BinaryIndexOf(key) != m_newSet.NoIndex))
    statusFlags |= nsMsgMessageFlags::New;
  if (NS_SUCCEEDED(IsHeaderRead(msgHdr, &isRead)) && isRead)
    statusFlags |= nsMsgMessageFlags::Read;
  return statusFlags;
}

nsresult nsMsgDatabase::IsHeaderRead(nsIMsgDBHdr *msgHdr, bool *pRead)
{
  if (!msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND;

  nsMsgHdr* hdr = static_cast<nsMsgHdr*>(msgHdr);          // closed system, cast ok
  // can't call GetFlags, because it will be recursive.
  uint32_t flags;
  hdr->GetRawFlags(&flags);
  *pRead = !!(flags & nsMsgMessageFlags::Read);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::IsMarked(nsMsgKey key, bool *pMarked)
{
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  nsresult rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv))
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?

  uint32_t flags;
  (void)msgHdr->GetFlags(&flags);
  *pMarked = !!(flags & nsMsgMessageFlags::Marked);
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::IsIgnored(nsMsgKey key, bool *pIgnored)
{
  NS_ENSURE_ARG_POINTER(pIgnored);

  nsCOMPtr <nsIMsgThread> threadHdr;

  nsresult rv = GetThreadForMsgKey(key, getter_AddRefs(threadHdr));
  // This should be very surprising, but we leave that up to the caller
  // to determine for now.
  if (!threadHdr)
    return NS_MSG_MESSAGE_NOT_FOUND;

  uint32_t threadFlags;
  threadHdr->GetFlags(&threadFlags);
  *pIgnored = !!(threadFlags & nsMsgMessageFlags::Ignored);
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::IsWatched(nsMsgKey key, bool *pWatched)
{
  NS_ENSURE_ARG_POINTER(pWatched);

  nsCOMPtr <nsIMsgThread> threadHdr;

  nsresult rv = GetThreadForMsgKey(key, getter_AddRefs(threadHdr));
  // This should be very surprising, but we leave that up to the caller
  // to determine for now.
  if (!threadHdr)
    return NS_MSG_MESSAGE_NOT_FOUND;

  uint32_t threadFlags;
  threadHdr->GetFlags(&threadFlags);
  *pWatched = !!(threadFlags & nsMsgMessageFlags::Watched);
  return rv;
}

nsresult nsMsgDatabase::HasAttachments(nsMsgKey key, bool *pHasThem)
{
  NS_ENSURE_ARG_POINTER(pHasThem);

  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  nsresult rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv))
    return rv;

  uint32_t flags;
  (void)msgHdr->GetFlags(&flags);
  *pHasThem = !!(flags & nsMsgMessageFlags::Attachment);
  return rv;
}

bool nsMsgDatabase::SetHdrReadFlag(nsIMsgDBHdr *msgHdr, bool bRead)
{
  return SetHdrFlag(msgHdr, bRead, nsMsgMessageFlags::Read);
}

nsresult nsMsgDatabase::MarkHdrReadInDB(nsIMsgDBHdr *msgHdr, bool bRead,
                                             nsIDBChangeListener *instigator)
{
  nsresult rv;
  nsMsgKey key;
  uint32_t oldFlags;
  bool     hdrInDB;
  (void)msgHdr->GetMessageKey(&key);
  msgHdr->GetFlags(&oldFlags);

  m_newSet.RemoveElement(key);
  (void) ContainsKey(key, &hdrInDB);
  if (hdrInDB && m_dbFolderInfo)
  {
    if (bRead)
      m_dbFolderInfo->ChangeNumUnreadMessages(-1);
    else
      m_dbFolderInfo->ChangeNumUnreadMessages(1);
  }

  SetHdrReadFlag(msgHdr, bRead); // this will cause a commit, at least for local mail, so do it after we change
  // the folder counts above, so they will get committed too.
  uint32_t flags;
  rv = msgHdr->GetFlags(&flags);
  flags &= ~nsMsgMessageFlags::New;
  msgHdr->SetFlags(flags);
  if (NS_FAILED(rv)) return rv;

  if (oldFlags == flags)
    return NS_OK;

  return NotifyHdrChangeAll(msgHdr, oldFlags, flags, instigator);
}

NS_IMETHODIMP nsMsgDatabase::MarkRead(nsMsgKey key, bool bRead,
                                      nsIDBChangeListener *instigator)
{
  nsresult rv;
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?

  rv = MarkHdrRead(msgHdr, bRead, instigator);
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::MarkReplied(nsMsgKey key, bool bReplied,
                                         nsIDBChangeListener *instigator /* = NULL */)
{
  return SetKeyFlag(key, bReplied, nsMsgMessageFlags::Replied, instigator);
}

NS_IMETHODIMP nsMsgDatabase::MarkForwarded(nsMsgKey key, bool bForwarded,
                                           nsIDBChangeListener *instigator /* = NULL */)
{
  return SetKeyFlag(key, bForwarded, nsMsgMessageFlags::Forwarded, instigator);
}

NS_IMETHODIMP nsMsgDatabase::MarkHasAttachments(nsMsgKey key, bool bHasAttachments,
                                                nsIDBChangeListener *instigator)
{
  return SetKeyFlag(key, bHasAttachments, nsMsgMessageFlags::Attachment, instigator);
}

NS_IMETHODIMP
nsMsgDatabase::MarkThreadRead(nsIMsgThread *thread, nsIDBChangeListener *instigator,
                              uint32_t *aNumMarked, nsMsgKey **aThoseMarked)
{
  NS_ENSURE_ARG_POINTER(thread);
  NS_ENSURE_ARG_POINTER(aNumMarked);
  NS_ENSURE_ARG_POINTER(aThoseMarked);
  nsresult rv = NS_OK;

  uint32_t numChildren;
  nsTArray<nsMsgKey> thoseMarked;
  thread->GetNumChildren(&numChildren);
  for (uint32_t curChildIndex = 0; curChildIndex < numChildren; curChildIndex++)
  {
    nsCOMPtr <nsIMsgDBHdr> child;

    rv = thread->GetChildHdrAt(curChildIndex, getter_AddRefs(child));
    if (NS_SUCCEEDED(rv) && child)
    {
      bool isRead = true;
      IsHeaderRead(child, &isRead);
      if (!isRead)
      {
        nsMsgKey key;
        if (NS_SUCCEEDED(child->GetMessageKey(&key)))
          thoseMarked.AppendElement(key);
        MarkHdrRead(child, true, instigator);
      }
    }
  }

  *aNumMarked = thoseMarked.Length();

  if (thoseMarked.Length())
  {
    *aThoseMarked =
      (nsMsgKey *) nsMemory::Clone(&thoseMarked[0],
                                   thoseMarked.Length() * sizeof(nsMsgKey));
    if (!*aThoseMarked)
      return NS_ERROR_OUT_OF_MEMORY;
  }
  else
    *aThoseMarked = nullptr;

  return rv;
}

NS_IMETHODIMP
nsMsgDatabase::MarkThreadIgnored(nsIMsgThread *thread, nsMsgKey threadKey, bool bIgnored,
                                 nsIDBChangeListener *instigator)
{
  NS_ENSURE_ARG(thread);
  uint32_t threadFlags;
  thread->GetFlags(&threadFlags);
  uint32_t oldThreadFlags = threadFlags; // not quite right, since we probably want msg hdr flags.
  if (bIgnored)
  {
    threadFlags |= nsMsgMessageFlags::Ignored;
    threadFlags &= ~nsMsgMessageFlags::Watched;  // ignore is implicit un-watch
  }
  else
    threadFlags &= ~nsMsgMessageFlags::Ignored;
  thread->SetFlags(threadFlags);

  nsCOMPtr <nsIMsgDBHdr> msg;
  nsresult rv = GetMsgHdrForKey(threadKey, getter_AddRefs(msg));
  NS_ENSURE_SUCCESS(rv, rv);
  return NotifyHdrChangeAll(msg, oldThreadFlags, threadFlags, instigator);
}

NS_IMETHODIMP
nsMsgDatabase::MarkHeaderKilled(nsIMsgDBHdr *msg, bool bIgnored,
                           nsIDBChangeListener *instigator)
{
  uint32_t msgFlags;
  msg->GetFlags(&msgFlags);
  uint32_t oldFlags = msgFlags;
  if (bIgnored)
    msgFlags |= nsMsgMessageFlags::Ignored;
  else
    msgFlags &= ~nsMsgMessageFlags::Ignored;
  msg->SetFlags(msgFlags);

  return NotifyHdrChangeAll(msg, oldFlags, msgFlags, instigator);
}

NS_IMETHODIMP
nsMsgDatabase::MarkThreadWatched(nsIMsgThread *thread, nsMsgKey threadKey, bool bWatched,
                                 nsIDBChangeListener *instigator)
{
  NS_ENSURE_ARG(thread);
  uint32_t threadFlags;
  thread->GetFlags(&threadFlags);
  uint32_t oldThreadFlags = threadFlags; // not quite right, since we probably want msg hdr flags.
  if (bWatched)
  {
    threadFlags |= nsMsgMessageFlags::Watched;
    threadFlags &= ~nsMsgMessageFlags::Ignored;  // watch is implicit un-ignore
  }
  else
    threadFlags &= ~nsMsgMessageFlags::Watched;

  nsCOMPtr <nsIMsgDBHdr> msg;
  GetMsgHdrForKey(threadKey, getter_AddRefs(msg));

  nsresult rv  = NotifyHdrChangeAll(msg, oldThreadFlags, threadFlags, instigator);
  thread->SetFlags(threadFlags);
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::MarkMarked(nsMsgKey key, bool mark,
                                        nsIDBChangeListener *instigator)
{
  return SetKeyFlag(key, mark, nsMsgMessageFlags::Marked, instigator);
}

NS_IMETHODIMP nsMsgDatabase::MarkOffline(nsMsgKey key, bool offline,
                                         nsIDBChangeListener *instigator)
{
  return SetKeyFlag(key, offline, nsMsgMessageFlags::Offline, instigator);
}

NS_IMETHODIMP nsMsgDatabase::SetStringProperty(nsMsgKey aKey, const char *aProperty, const char *aValue)
{
  nsCOMPtr <nsIMsgDBHdr> msgHdr;
  nsresult rv = GetMsgHdrForKey(aKey, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?
  return SetStringPropertyByHdr(msgHdr, aProperty, aValue);
}

NS_IMETHODIMP nsMsgDatabase::SetStringPropertyByHdr(nsIMsgDBHdr *msgHdr, const char *aProperty, const char *aValue)
{
  // don't do notifications if message not yet added to database.
  // Ignore errors (consequences of failure are minor).
  bool notify = true;
  nsMsgKey key = nsMsgKey_None;
  msgHdr->GetMessageKey(&key);
  ContainsKey(key, &notify);

  nsCString oldValue;
  nsresult rv = msgHdr->GetStringProperty(aProperty, getter_Copies(oldValue));
  NS_ENSURE_SUCCESS(rv,rv);

  // if no change to this string property, bail out
  if (oldValue.Equals(aValue))
    return NS_OK;

  // Precall OnHdrPropertyChanged to store prechange status
  nsTArray<uint32_t> statusArray(m_ChangeListeners.Length());
  uint32_t status;
  nsCOMPtr<nsIDBChangeListener> listener;
  if (notify)
  {
    nsTObserverArray<nsCOMPtr<nsIDBChangeListener> >::ForwardIterator listeners(m_ChangeListeners);
    while (listeners.HasMore())
    {
      listener = listeners.GetNext();
      listener->OnHdrPropertyChanged(msgHdr, true, &status, nullptr);
      // ignore errors, but append element to keep arrays in sync
      statusArray.AppendElement(status);
    }
  }

  rv = msgHdr->SetStringProperty(aProperty, aValue);
  NS_ENSURE_SUCCESS(rv,rv);

  //Postcall OnHdrPropertyChanged to process the change
  if (notify)
  {
    // if this is the junk score property notify, as long as we're not going
    // from no value to non junk
    if (!strcmp(aProperty, "junkscore") && !(oldValue.IsEmpty() && !strcmp(aValue, "0")))
      NotifyJunkScoreChanged(nullptr);

    nsTObserverArray<nsCOMPtr<nsIDBChangeListener> >::ForwardIterator listeners(m_ChangeListeners);
    for (uint32_t i = 0; listeners.HasMore(); i++)
    {
      listener = listeners.GetNext();
      status = statusArray[i];
      listener->OnHdrPropertyChanged(msgHdr, false, &status, nullptr);
      // ignore errors
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMsgDatabase::SetUint32PropertyByHdr(nsIMsgDBHdr *aMsgHdr,
                                      const char *aProperty,
                                      uint32_t aValue)
{
  // If no change to this property, bail out.
  uint32_t oldValue;
  nsresult rv = aMsgHdr->GetUint32Property(aProperty, &oldValue);
  NS_ENSURE_SUCCESS(rv, rv);
  if (oldValue == aValue)
    return NS_OK;

  // Don't do notifications if message not yet added to database.
  bool notify = true;  
  nsMsgKey key = nsMsgKey_None;
  aMsgHdr->GetMessageKey(&key);
  ContainsKey(key, &notify);

  // Precall OnHdrPropertyChanged to store prechange status.
  nsTArray<uint32_t> statusArray(m_ChangeListeners.Length());
  uint32_t status;
  nsCOMPtr<nsIDBChangeListener> listener;
  if (notify)
  {
    nsTObserverArray<nsCOMPtr<nsIDBChangeListener> >::ForwardIterator listeners(m_ChangeListeners);
    while (listeners.HasMore())
    {
      listener = listeners.GetNext();
      listener->OnHdrPropertyChanged(aMsgHdr, true, &status, nullptr);
      // Ignore errors, but append element to keep arrays in sync.
      statusArray.AppendElement(status);
    }
  }

  rv = aMsgHdr->SetUint32Property(aProperty, aValue);
  NS_ENSURE_SUCCESS(rv, rv);

  // Postcall OnHdrPropertyChanged to process the change.
  if (notify)
  {
    nsTObserverArray<nsCOMPtr<nsIDBChangeListener> >::ForwardIterator listeners(m_ChangeListeners);
    for (uint32_t i = 0; listeners.HasMore(); i++)
    {
      listener = listeners.GetNext();
      status = statusArray[i];
      listener->OnHdrPropertyChanged(aMsgHdr, false, &status, nullptr);
      // Ignore errors.
    }
  }

  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::SetLabel(nsMsgKey key, nsMsgLabelValue label)
{
  nsresult rv;
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND;
  nsMsgLabelValue oldLabel;
  msgHdr->GetLabel(&oldLabel);

  msgHdr->SetLabel(label);
  // clear old label
  if (oldLabel != label)
  {
    if (oldLabel != 0)
      rv = SetKeyFlag(key, false, oldLabel << 25, nullptr);
    // set the flag in the x-mozilla-status2 line.
    rv = SetKeyFlag(key, true, label << 25, nullptr);
  }
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::MarkImapDeleted(nsMsgKey key, bool deleted,
                                             nsIDBChangeListener *instigator)
{
  return SetKeyFlag(key, deleted, nsMsgMessageFlags::IMAPDeleted, instigator);
}

NS_IMETHODIMP nsMsgDatabase::MarkMDNNeeded(nsMsgKey key, bool bNeeded,
                                           nsIDBChangeListener *instigator /* = NULL */)
{
  return SetKeyFlag(key, bNeeded, nsMsgMessageFlags::MDNReportNeeded, instigator);
}

NS_IMETHODIMP nsMsgDatabase::IsMDNNeeded(nsMsgKey key, bool *pNeeded)
{
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  nsresult rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?

  uint32_t flags;
  (void)msgHdr->GetFlags(&flags);
  *pNeeded = !!(flags & nsMsgMessageFlags::MDNReportNeeded);
  return rv;
}


nsresult nsMsgDatabase::MarkMDNSent(nsMsgKey key, bool bSent,
                                    nsIDBChangeListener *instigator /* = NULL */)
{
  return SetKeyFlag(key, bSent, nsMsgMessageFlags::MDNReportSent, instigator);
}


nsresult nsMsgDatabase::IsMDNSent(nsMsgKey key, bool *pSent)
{
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  nsresult rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?

  uint32_t flags;
  (void)msgHdr->GetFlags(&flags);
  *pSent = !!(flags & nsMsgMessageFlags::MDNReportSent);
  return rv;
}


nsresult  nsMsgDatabase::SetKeyFlag(nsMsgKey key, bool set, uint32_t flag,
                                     nsIDBChangeListener *instigator)
{
  nsresult rv;
  nsCOMPtr <nsIMsgDBHdr> msgHdr;

  rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
  if (NS_FAILED(rv) || !msgHdr)
    return NS_MSG_MESSAGE_NOT_FOUND; // XXX return rv?

  uint32_t oldFlags;
  msgHdr->GetFlags(&oldFlags);

  SetHdrFlag(msgHdr, set, flag);

  uint32_t flags;
  (void)msgHdr->GetFlags(&flags);

  if (oldFlags == flags)
    return NS_OK;

  return NotifyHdrChangeAll(msgHdr, oldFlags, flags, instigator);
}

nsresult nsMsgDatabase::SetMsgHdrFlag(nsIMsgDBHdr *msgHdr, bool set, uint32_t flag, nsIDBChangeListener *instigator)
{
  uint32_t oldFlags;
  msgHdr->GetFlags(&oldFlags);

  SetHdrFlag(msgHdr, set, flag);

  uint32_t flags;
  (void)msgHdr->GetFlags(&flags);

  if (oldFlags == flags)
    return NS_OK;

  return NotifyHdrChangeAll(msgHdr, oldFlags, flags, instigator);
}

// Helper routine - lowest level of flag setting - returns true if flags change,
// false otherwise.
bool nsMsgDatabase::SetHdrFlag(nsIMsgDBHdr *msgHdr, bool bSet, nsMsgMessageFlagType flag)
{
  uint32_t statusFlags;
  (void)msgHdr->GetFlags(&statusFlags);
  uint32_t currentStatusFlags = GetStatusFlags(msgHdr, statusFlags);
  bool flagAlreadySet = (currentStatusFlags & flag) != 0;

  if ((flagAlreadySet && !bSet) || (!flagAlreadySet && bSet))
  {
    uint32_t resultFlags;
    if (bSet)
      msgHdr->OrFlags(flag, &resultFlags);
    else
      msgHdr->AndFlags(~flag, &resultFlags);
    return true;
  }
  return false;
}


NS_IMETHODIMP nsMsgDatabase::MarkHdrRead(nsIMsgDBHdr *msgHdr, bool bRead,
                                         nsIDBChangeListener *instigator)
{
  bool isReadInDB = true;
  nsresult rv = nsMsgDatabase::IsHeaderRead(msgHdr, &isReadInDB);
  NS_ENSURE_SUCCESS(rv, rv);

  bool isRead = true;
  rv = IsHeaderRead(msgHdr, &isRead);
  NS_ENSURE_SUCCESS(rv, rv);

  // if the flag is already correct in the db, don't change it.
  // Check msg flags as well as IsHeaderRead in case it's a newsgroup
  // and the msghdr flags are out of sync with the newsrc settings.
  // (we could override this method for news db's, but it's a trivial fix here.
  if (bRead != isRead || isRead != isReadInDB)
  {
    nsMsgKey msgKey;
    msgHdr->GetMessageKey(&msgKey);
    
    bool inDB = false;
    (void)ContainsKey(msgKey, &inDB);

    if (inDB)
    {
      nsCOMPtr <nsIMsgThread> threadHdr;
      rv = GetThreadForMsgKey(msgKey, getter_AddRefs(threadHdr));
      if (threadHdr)
        threadHdr->MarkChildRead(bRead);
    }
    return MarkHdrReadInDB(msgHdr, bRead, instigator);
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::MarkHdrReplied(nsIMsgDBHdr *msgHdr, bool bReplied,
                         nsIDBChangeListener *instigator)
{
  return SetMsgHdrFlag(msgHdr, bReplied, nsMsgMessageFlags::Replied, instigator);
}


NS_IMETHODIMP nsMsgDatabase::MarkHdrMarked(nsIMsgDBHdr *msgHdr, bool mark,
                         nsIDBChangeListener *instigator)
{
  return SetMsgHdrFlag(msgHdr, mark, nsMsgMessageFlags::Marked, instigator);
}

NS_IMETHODIMP
nsMsgDatabase::MarkHdrNotNew(nsIMsgDBHdr *aMsgHdr,
                             nsIDBChangeListener *aInstigator)
{
  NS_ENSURE_ARG_POINTER(aMsgHdr);
  nsMsgKey msgKey;
  aMsgHdr->GetMessageKey(&msgKey);
  m_newSet.RemoveElement(msgKey);
  return SetMsgHdrFlag(aMsgHdr, false, nsMsgMessageFlags::New, aInstigator);
}

NS_IMETHODIMP nsMsgDatabase::MarkAllRead(uint32_t *aNumKeys, nsMsgKey **aThoseMarked)
{
  NS_ENSURE_ARG_POINTER(aNumKeys);
  NS_ENSURE_ARG_POINTER(aThoseMarked);
  nsMsgHdr  *pHeader;

  nsCOMPtr<nsISimpleEnumerator> hdrs;
  nsTArray<nsMsgKey> thoseMarked;
  nsresult rv = EnumerateMessages(getter_AddRefs(hdrs));
  if (NS_FAILED(rv))
    return rv;
  bool hasMore = false;

  while (NS_SUCCEEDED(rv = hdrs->HasMoreElements(&hasMore)) && hasMore)
  {
    rv = hdrs->GetNext((nsISupports**)&pHeader);
    NS_ASSERTION(NS_SUCCEEDED(rv), "nsMsgDBEnumerator broken");
    if (NS_FAILED(rv))
      break;

    bool isRead;
    IsHeaderRead(pHeader, &isRead);

    if (!isRead)
    {
      nsMsgKey key;
      (void)pHeader->GetMessageKey(&key);
      thoseMarked.AppendElement(key);
      rv = MarkHdrRead(pHeader, true, nullptr);   // ### dmb - blow off error?
    }
    NS_RELEASE(pHeader);
  }

  *aNumKeys = thoseMarked.Length();

  if (thoseMarked.Length())
  {
    *aThoseMarked = (nsMsgKey *) nsMemory::Clone(&thoseMarked[0],
                                                 thoseMarked.Length() * sizeof(nsMsgKey));
    if (!*aThoseMarked)
      return NS_ERROR_OUT_OF_MEMORY;
  }
  else
    *aThoseMarked = nullptr;

  // force num new to 0.
  int32_t numUnreadMessages;

  rv = m_dbFolderInfo->GetNumUnreadMessages(&numUnreadMessages);
  if (NS_SUCCEEDED(rv))
    m_dbFolderInfo->ChangeNumUnreadMessages(-numUnreadMessages);
  // caller will Commit the db, so no need to do it here.
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::AddToNewList(nsMsgKey key)
{
  // we add new keys in increasing order...
  if (m_newSet.IsEmpty() || (m_newSet[m_newSet.Length() - 1] < key))
    m_newSet.AppendElement(key);
  return NS_OK;
}


NS_IMETHODIMP nsMsgDatabase::ClearNewList(bool notify /* = FALSE */)
{
  nsresult err = NS_OK;
  if (notify && !m_newSet.IsEmpty())  // need to update view
  {
    nsTArray<nsMsgKey> saveNewSet;
    // clear m_newSet so that the code that's listening to the key change
    // doesn't think we have new messages and send notifications all over
    // that we have new messages.
    saveNewSet.SwapElements(m_newSet);
    for (uint32_t elementIndex = saveNewSet.Length() - 1; ; elementIndex--)
    {
      nsMsgKey lastNewKey = saveNewSet.ElementAt(elementIndex);
      nsCOMPtr <nsIMsgDBHdr> msgHdr;
      err = GetMsgHdrForKey(lastNewKey, getter_AddRefs(msgHdr));
      if (NS_SUCCEEDED(err))
      {
        uint32_t flags;
        (void)msgHdr->GetFlags(&flags);

        if ((flags | nsMsgMessageFlags::New) != flags)
        {
          msgHdr->AndFlags(~nsMsgMessageFlags::New, &flags);
          NotifyHdrChangeAll(msgHdr, flags | nsMsgMessageFlags::New, flags, nullptr);
        }
      }
      if (elementIndex == 0)
        break;
    }
  }
  return err;
}

NS_IMETHODIMP nsMsgDatabase::HasNew(bool *_retval)
{
  if (!_retval) return NS_ERROR_NULL_POINTER;

  *_retval = (m_newSet.Length() > 0);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetFirstNew(nsMsgKey *result)
{
  bool hasnew;
  nsresult rv = HasNew(&hasnew);
  if (NS_FAILED(rv)) return rv;
  *result = (hasnew) ? m_newSet.ElementAt(0) : nsMsgKey_None;
  return NS_OK;
}


////////////////////////////////////////////////////////////////////////////////

nsMsgDBEnumerator::nsMsgDBEnumerator(nsMsgDatabase* db,
                                     nsIMdbTable *table,
                                     nsMsgDBEnumeratorFilter filter,
                                     void* closure,
                                     bool iterateForwards)
    : mDB(db),
      mDone(false),
      mIterateForwards(iterateForwards),
      mFilter(filter),
      mClosure(closure),
      mStopPos(-1)
{
  mNextPrefetched = false;
  mTable = table;
  mRowPos = 0;
  mDB->m_enumerators.AppendElement(this);
}

nsMsgDBEnumerator::~nsMsgDBEnumerator()
{
  Clear();
}

void nsMsgDBEnumerator::Clear()
{
  mRowCursor = nullptr;
  mTable = nullptr;
  mResultHdr = nullptr;
  if (mDB)
    mDB->m_enumerators.RemoveElement(this);
  mDB = nullptr;
}

NS_IMPL_ISUPPORTS(nsMsgDBEnumerator, nsISimpleEnumerator)

nsresult nsMsgDBEnumerator::GetRowCursor()
{
  mDone = false;

  if (!mDB || !mTable)
    return NS_ERROR_NULL_POINTER;

  if (mIterateForwards)
  {
    mRowPos = -1;
  }
  else
  {
    mdb_count numRows;
    mTable->GetCount(mDB->GetEnv(), &numRows);
    mRowPos = numRows; // startPos is 0 relative.
  }
  return mTable->GetTableRowCursor(mDB->GetEnv(), mRowPos, getter_AddRefs(mRowCursor));
}

NS_IMETHODIMP nsMsgDBEnumerator::GetNext(nsISupports **aItem)
{
  if (!aItem)
    return NS_ERROR_NULL_POINTER;
  nsresult rv = NS_OK;
  if (!mNextPrefetched)
    rv = PrefetchNext();
  if (NS_SUCCEEDED(rv))
  {
    if (mResultHdr)
    {
      *aItem = mResultHdr;
      NS_ADDREF(*aItem);
      mNextPrefetched = false;
    }
  }
  return rv;
}

nsresult nsMsgDBEnumerator::PrefetchNext()
{
  nsresult rv = NS_OK;
  nsIMdbRow* hdrRow;
  uint32_t flags;

  if (!mRowCursor)
  {
    rv = GetRowCursor();
    if (NS_FAILED(rv))
      return rv;
  }

  do
  {
    mResultHdr = nullptr;
    if (mIterateForwards)
      rv = mRowCursor->NextRow(mDB->GetEnv(), &hdrRow, &mRowPos);
    else
      rv = mRowCursor->PrevRow(mDB->GetEnv(), &hdrRow, &mRowPos);
    if (!hdrRow)
    {
      mDone = true;
      return NS_ERROR_FAILURE;
    }
    if (NS_FAILED(rv))
    {
      mDone = true;
      return rv;
    }
    //Get key from row
    mdbOid outOid;
    nsMsgKey key = nsMsgKey_None;
    rv = hdrRow->GetOid(mDB->GetEnv(), &outOid);
    if (NS_WARN_IF(NS_FAILED(rv)))
      return rv;
    key = outOid.mOid_Id;

    rv = mDB->GetHdrFromUseCache(key, getter_AddRefs(mResultHdr));
    if (NS_SUCCEEDED(rv) && mResultHdr)
      hdrRow->Release();
    else {
      rv = mDB->CreateMsgHdr(hdrRow, key, getter_AddRefs(mResultHdr));
      if (NS_WARN_IF(NS_FAILED(rv)))
        return rv;
    }

    if (mResultHdr)
      mResultHdr->GetFlags(&flags);
    else
      flags = 0;
  }
  while (mFilter && NS_FAILED(mFilter(mResultHdr, mClosure)) && !(flags & nsMsgMessageFlags::Expunged));

  if (mResultHdr)
  {
    mNextPrefetched = true;
    return NS_OK;
  }
  else
    mNextPrefetched = false;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsMsgDBEnumerator::HasMoreElements(bool *aResult)
{
  if (!aResult)
    return NS_ERROR_NULL_POINTER;

  if (!mNextPrefetched && (NS_FAILED(PrefetchNext())))
    mDone = true;
  *aResult = !mDone;
  return NS_OK;
}

nsMsgFilteredDBEnumerator::nsMsgFilteredDBEnumerator(nsMsgDatabase* db,
                                                     nsIMdbTable *table,
                                                     bool reverse,
                                                     nsIArray *searchTerms)
   : nsMsgDBEnumerator(db, table, nullptr, nullptr, !reverse)
{
}

nsMsgFilteredDBEnumerator::~nsMsgFilteredDBEnumerator()
{
}

/**
 * Create the search session for the enumerator,
 * add the scope term for "folder" to the search session, and add the search
 * terms in the array to the search session.
 */
nsresult nsMsgFilteredDBEnumerator::InitSearchSession(nsIArray *searchTerms, nsIMsgFolder *folder)
{
  nsresult rv;
  m_searchSession = do_CreateInstance(NS_MSGSEARCHSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  m_searchSession->AddScopeTerm(nsMsgSearchScope::offlineMail, folder);
  // add each item in termsArray to the search session
  uint32_t numTerms;
  rv = searchTerms->GetLength(&numTerms);
  NS_ENSURE_SUCCESS(rv, rv);
  for (uint32_t i = 0; i < numTerms; i++)
  {
    nsCOMPtr <nsIMsgSearchTerm> searchTerm;
    searchTerms->QueryElementAt(i, NS_GET_IID(nsIMsgSearchTerm), getter_AddRefs(searchTerm));
    m_searchSession->AppendTerm(searchTerm);
  }
  return NS_OK;
}

nsresult nsMsgFilteredDBEnumerator::PrefetchNext()
{
  nsresult rv;
  do
  {
    rv = nsMsgDBEnumerator::PrefetchNext();
    if (NS_SUCCEEDED(rv) && mResultHdr)
    {
      bool matches;
      rv  = m_searchSession->MatchHdr(mResultHdr, mDB, &matches);
      if (NS_SUCCEEDED(rv) && matches)
        break;
      mResultHdr = nullptr;
    }
    else
      break;
  } while (mStopPos == -1 || mRowPos != mStopPos);

  if (!mResultHdr)
    mNextPrefetched = false;

  return rv;
}


////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP
nsMsgDatabase::EnumerateMessages(nsISimpleEnumerator* *result)
{
  RememberLastUseTime();
  NS_ENSURE_ARG_POINTER(result);
  nsMsgDBEnumerator* e = new nsMsgDBEnumerator(this, m_mdbAllMsgHeadersTable,
                                               nullptr, nullptr);
  if (!e)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*result = e);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDatabase::ReverseEnumerateMessages(nsISimpleEnumerator* *result)
{
  NS_ENSURE_ARG_POINTER(result);
  nsMsgDBEnumerator* e = new nsMsgDBEnumerator(this, m_mdbAllMsgHeadersTable,
                                               nullptr, nullptr, false);
  if (!e)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*result = e);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDatabase::GetFilterEnumerator(nsIArray *searchTerms, bool aReverse,
                                   nsISimpleEnumerator **aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  RefPtr<nsMsgFilteredDBEnumerator> e = 
    new nsMsgFilteredDBEnumerator(this, m_mdbAllMsgHeadersTable, aReverse,
                                  searchTerms);

  NS_ENSURE_TRUE(e, NS_ERROR_OUT_OF_MEMORY);
  nsresult rv = e->InitSearchSession(searchTerms, m_folder);
  NS_ENSURE_SUCCESS(rv, rv);
  return CallQueryInterface(e.get(), aResult);
}

NS_IMETHODIMP
nsMsgDatabase::NextMatchingHdrs(nsISimpleEnumerator *aEnumerator,
                                int32_t aNumHdrsToLookAt, int32_t aMaxResults,
                                nsIMutableArray *aMatchingHdrs,
                                int32_t *aNumMatches, bool *aResult)
{
  NS_ENSURE_ARG_POINTER(aEnumerator);
  NS_ENSURE_ARG_POINTER(aResult);
  nsMsgFilteredDBEnumerator *enumerator =
    static_cast<nsMsgFilteredDBEnumerator *> (aEnumerator);

  // Force mRowPos to be initialized.
  if (!enumerator->mRowCursor)
    enumerator->GetRowCursor();

  if (aNumHdrsToLookAt)
  {
    enumerator->mStopPos = enumerator->mIterateForwards ?
      enumerator->mRowPos + aNumHdrsToLookAt :
      enumerator->mRowPos - aNumHdrsToLookAt;
    if (enumerator->mStopPos < 0)
      enumerator->mStopPos = 0;
  }
  int32_t numMatches = 0;
  nsresult rv;
  do
  {
    nsCOMPtr <nsISupports> supports;
    rv = enumerator->GetNext(getter_AddRefs(supports));
    nsCOMPtr <nsIMsgDBHdr> nextMessage = do_QueryInterface(supports);
    if (NS_SUCCEEDED(rv) && nextMessage)
    {
      if (aMatchingHdrs)
        aMatchingHdrs->AppendElement(nextMessage, false);
      ++numMatches;
      if (aMaxResults && numMatches == aMaxResults)
        break;
    }
    else
      break;
  }
  while (true);

  if (aNumMatches)
    *aNumMatches = numMatches;

  *aResult = !enumerator->mDone;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDatabase::SyncCounts()
{
  nsCOMPtr <nsISimpleEnumerator> hdrs;
  nsresult rv = EnumerateMessages(getter_AddRefs(hdrs));
  if (NS_FAILED(rv))
    return rv;
  bool hasMore = false;

  mdb_count numHdrsInTable = 0;
  int32_t numUnread = 0;
  int32_t numHdrs = 0;

  if (m_mdbAllMsgHeadersTable)
    m_mdbAllMsgHeadersTable->GetCount(GetEnv(), &numHdrsInTable);
  else
    return NS_ERROR_NULL_POINTER;

  while (NS_SUCCEEDED(rv = hdrs->HasMoreElements(&hasMore)) && hasMore)
  {
    nsCOMPtr <nsISupports> supports;
    rv = hdrs->GetNext(getter_AddRefs(supports));
    NS_ASSERTION(NS_SUCCEEDED(rv), "nsMsgDBEnumerator broken");
    if (NS_FAILED(rv))
      break;

    nsCOMPtr <nsIMsgDBHdr> pHeader = do_QueryInterface(supports);

    bool isRead;
    IsHeaderRead(pHeader, &isRead);
    if (!isRead)
      numUnread++;
    numHdrs++;
  }

  int32_t oldTotal, oldUnread;
  (void) m_dbFolderInfo->GetNumUnreadMessages(&oldUnread);
  (void) m_dbFolderInfo->GetNumMessages(&oldTotal);
  if (oldUnread != numUnread)
    m_dbFolderInfo->ChangeNumUnreadMessages(numUnread - oldUnread);
  if (oldTotal != numHdrs)
    m_dbFolderInfo->ChangeNumMessages(numHdrs - oldTotal);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::ListAllKeys(nsIMsgKeyArray *aKeys)
{
  NS_ENSURE_ARG_POINTER(aKeys);
  nsresult  rv = NS_OK;
  nsCOMPtr<nsIMdbTableRowCursor> rowCursor;

  RememberLastUseTime();

  if (m_mdbAllMsgHeadersTable)
  {
    uint32_t numMsgs = 0;
    m_mdbAllMsgHeadersTable->GetCount(GetEnv(), &numMsgs);
    aKeys->SetCapacity(numMsgs);
    rv = m_mdbAllMsgHeadersTable->GetTableRowCursor(GetEnv(), -1,
                                                     getter_AddRefs(rowCursor));
    while (NS_SUCCEEDED(rv) && rowCursor)
    {
      mdbOid outOid;
      mdb_pos  outPos;

      rv = rowCursor->NextRowOid(GetEnv(), &outOid, &outPos);
      // is this right? Mork is returning a 0 id, but that should valid.
      if (outPos < 0 || outOid.mOid_Id == (mdb_id) -1)
        break;
      if (NS_SUCCEEDED(rv))
        aKeys->AppendElement(outOid.mOid_Id);
    }
  }
  return rv;
}

class nsMsgDBThreadEnumerator : public nsISimpleEnumerator, public nsIDBChangeListener
{
public:
    NS_DECL_ISUPPORTS

    // nsISimpleEnumerator methods:
    NS_DECL_NSISIMPLEENUMERATOR

    NS_DECL_NSIDBCHANGELISTENER

    // nsMsgDBEnumerator methods:
    typedef nsresult (*nsMsgDBThreadEnumeratorFilter)(nsIMsgThread* thread);

    nsMsgDBThreadEnumerator(nsMsgDatabase* db, nsMsgDBThreadEnumeratorFilter filter);

protected:
    virtual ~nsMsgDBThreadEnumerator();
    nsresult          GetTableCursor(void);
    nsresult          PrefetchNext();
    nsMsgDatabase*              mDB;
    nsCOMPtr <nsIMdbPortTableCursor>  mTableCursor;
    nsIMsgThread*                 mResultThread;
    bool                        mDone;
    bool              mNextPrefetched;
    nsMsgDBThreadEnumeratorFilter     mFilter;
};

nsMsgDBThreadEnumerator::nsMsgDBThreadEnumerator(nsMsgDatabase* db,
                                     nsMsgDBThreadEnumeratorFilter filter)
    : mDB(db), mTableCursor(nullptr), mResultThread(nullptr), mDone(false),
      mFilter(filter)
{
    mDB->AddListener(this);
    mNextPrefetched = false;
}

nsMsgDBThreadEnumerator::~nsMsgDBThreadEnumerator()
{
  mTableCursor = nullptr;
  NS_IF_RELEASE(mResultThread);
  if (mDB)
    mDB->RemoveListener(this);
}

NS_IMPL_ISUPPORTS(nsMsgDBThreadEnumerator, nsISimpleEnumerator, nsIDBChangeListener)


/* void OnHdrFlagsChanged (in nsIMsgDBHdr aHdrChanged, in unsigned long aOldFlags, in unsigned long aNewFlags, in nsIDBChangeListener aInstigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnHdrFlagsChanged(nsIMsgDBHdr *aHdrChanged, uint32_t aOldFlags, uint32_t aNewFlags, nsIDBChangeListener *aInstigator)
{
    return NS_OK;
}

//void OnHdrPropertyChanged(in nsIMsgDBHdr aHdrToChange, in bool aPreChange, 
// inout uint32_t aStatus, in nsIDBChangeListener aInstigator);
NS_IMETHODIMP
nsMsgDBThreadEnumerator::OnHdrPropertyChanged(nsIMsgDBHdr *aHdrToChange, bool aPreChange, uint32_t *aStatus, 
                                         nsIDBChangeListener * aInstigator)
{
  return NS_OK;
}

/* void onHdrDeleted (in nsIMsgDBHdr aHdrChanged, in nsMsgKey aParentKey, in long aFlags, in nsIDBChangeListener aInstigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnHdrDeleted(nsIMsgDBHdr *aHdrChanged, nsMsgKey aParentKey, int32_t aFlags, nsIDBChangeListener *aInstigator)
{
    return NS_OK;
}

/* void onHdrAdded (in nsIMsgDBHdr aHdrChanged, in nsMsgKey aParentKey, in long aFlags, in nsIDBChangeListener aInstigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnHdrAdded(nsIMsgDBHdr *aHdrChanged, nsMsgKey aParentKey, int32_t aFlags, nsIDBChangeListener *aInstigator)
{
    return NS_OK;
}

/* void onParentChanged (in nsMsgKey aKeyChanged, in nsMsgKey oldParent, in nsMsgKey newParent, in nsIDBChangeListener aInstigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnParentChanged(nsMsgKey aKeyChanged, nsMsgKey oldParent, nsMsgKey newParent, nsIDBChangeListener *aInstigator)
{
    return NS_OK;
}

/* void onAnnouncerGoingAway (in nsIDBChangeAnnouncer instigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnAnnouncerGoingAway(nsIDBChangeAnnouncer *instigator)
{
  mTableCursor = nullptr;
  NS_IF_RELEASE(mResultThread);
  mDB->RemoveListener(this);
  mDB = nullptr;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBThreadEnumerator::OnEvent(nsIMsgDatabase *aDB, const char *aEvent)
{
  return NS_OK;
}

/* void onReadChanged (in nsIDBChangeListener aInstigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnReadChanged(nsIDBChangeListener *aInstigator)
{
    return NS_OK;
}

/* void onJunkScoreChanged (in nsIDBChangeListener aInstigator); */
NS_IMETHODIMP nsMsgDBThreadEnumerator::OnJunkScoreChanged(nsIDBChangeListener *aInstigator)
{
    return NS_OK;
}

nsresult nsMsgDBThreadEnumerator::GetTableCursor(void)
{
  nsresult rv = NS_OK;

  if (!mDB || !mDB->m_mdbStore)
    return NS_ERROR_NULL_POINTER;

  mDB->m_mdbStore->GetPortTableCursor(mDB->GetEnv(),   mDB->m_hdrRowScopeToken, mDB->m_threadTableKindToken,
    getter_AddRefs(mTableCursor));

  if (NS_FAILED(rv))
    return rv;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBThreadEnumerator::GetNext(nsISupports **aItem)
{
  NS_ENSURE_ARG_POINTER(aItem);

  *aItem = nullptr;
  nsresult rv = NS_OK;
  if (!mNextPrefetched)
    rv = PrefetchNext();
  if (NS_SUCCEEDED(rv))
  {
    if (mResultThread)
    {
      *aItem = mResultThread;
      NS_ADDREF(mResultThread);
      mNextPrefetched = false;
    }
  }
  return rv;
}


nsresult nsMsgDBThreadEnumerator::PrefetchNext()
{
  nsresult rv;
  nsCOMPtr<nsIMdbTable> table;

  if (!mDB)
    return NS_ERROR_NULL_POINTER;

  if (!mTableCursor)
  {
    rv = GetTableCursor();
    if (NS_FAILED(rv))
      return rv;
  }
  while (true)
  {
    NS_IF_RELEASE(mResultThread);
    mResultThread = nullptr;
    rv = mTableCursor->NextTable(mDB->GetEnv(), getter_AddRefs(table));
    if (!table)
    {
      mDone = true;
      return NS_ERROR_FAILURE;
    }
    if (NS_FAILED(rv))
    {
      mDone = true;
      return rv;
    }

    mdbOid tableId;
    table->GetOid(mDB->GetEnv(), &tableId);

    mResultThread = mDB->FindExistingThread(tableId.mOid_Id);
    if (!mResultThread)
      mResultThread = new nsMsgThread(mDB, table);

    if (mResultThread)
    {
      uint32_t numChildren = 0;
      NS_ADDREF(mResultThread);
      mResultThread->GetNumChildren(&numChildren);
      // we've got empty thread; don't tell caller about it.
      if (numChildren == 0)
        continue;
    }
    if (mFilter && NS_FAILED(mFilter(mResultThread)))
      continue;
    else
      break;
  }
  if (mResultThread)
  {
    mNextPrefetched = true;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsMsgDBThreadEnumerator::HasMoreElements(bool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  if (!mNextPrefetched)
    PrefetchNext();
  *aResult = !mDone;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDatabase::EnumerateThreads(nsISimpleEnumerator* *result)
{
  RememberLastUseTime();
  nsMsgDBThreadEnumerator* e = new nsMsgDBThreadEnumerator(this, nullptr);
  if (e == nullptr)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*result = e);
  return NS_OK;
}

// only return headers with a particular flag set
static nsresult
nsMsgFlagSetFilter(nsIMsgDBHdr *msg, void *closure)
{
  uint32_t msgFlags, desiredFlags;
  desiredFlags = * (uint32_t *) closure;
  msg->GetFlags(&msgFlags);
  return (msgFlags & desiredFlags) ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsMsgDatabase::EnumerateMessagesWithFlag(nsISimpleEnumerator* *result, uint32_t *pFlag)
{
  RememberLastUseTime();

  nsMsgDBEnumerator* e = new nsMsgDBEnumerator(this, m_mdbAllMsgHeadersTable, nsMsgFlagSetFilter, pFlag);
  if (!e)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*result = e);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::CreateNewHdr(nsMsgKey key, nsIMsgDBHdr **pnewHdr)
{
  nsresult  err = NS_OK;
  nsIMdbRow    *hdrRow = nullptr;
  struct mdbOid allMsgHdrsTableOID;

  if (!pnewHdr || !m_mdbAllMsgHeadersTable || !m_mdbStore)
    return NS_ERROR_NULL_POINTER;

  if (key != nsMsgKey_None)
  {
  allMsgHdrsTableOID.mOid_Scope = m_hdrRowScopeToken;
  allMsgHdrsTableOID.mOid_Id = key;  // presumes 0 is valid key value

  err = m_mdbStore->GetRow(GetEnv(), &allMsgHdrsTableOID, &hdrRow);
  if (!hdrRow)
    err  = m_mdbStore->NewRowWithOid(GetEnv(), &allMsgHdrsTableOID, &hdrRow);
  }
  else
  {
    // Mork will assign an ID to the new row, generally the next available ID.
    err  = m_mdbStore->NewRow(GetEnv(), m_hdrRowScopeToken, &hdrRow);
    if (hdrRow)
    {
      struct mdbOid oid;
      hdrRow->GetOid(GetEnv(), &oid);
      key = oid.mOid_Id;
    }
    else
    {
      // We failed to create a new row. That can happen if we run out of keys,
      // which will force a reparse.
      RefPtr<nsMsgKeyArray> keys = new nsMsgKeyArray;
      if (NS_SUCCEEDED(ListAllKeys(keys)))
      {
        uint32_t numKeys;
        keys->GetLength(&numKeys);
        for (uint32_t i = 0; i < numKeys; i++)
        {
          if (keys->m_keys[i] >= kForceReparseKey)
          {
            // Force a reparse.
            if (m_dbFolderInfo)
              m_dbFolderInfo->SetBooleanProperty("forceReparse", true);
            break;
          }
        }
      }
      err = NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
    }
  }
  if (NS_FAILED(err))
    return err;
  err = CreateMsgHdr(hdrRow, key, pnewHdr);
  return err;
}

NS_IMETHODIMP nsMsgDatabase::AddNewHdrToDB(nsIMsgDBHdr *newHdr, bool notify)
{
  NS_ENSURE_ARG_POINTER(newHdr);
  nsMsgHdr* hdr = static_cast<nsMsgHdr*>(newHdr);          // closed system, cast ok
  bool newThread;
  bool hasKey = false;
  nsMsgKey msgKey = nsMsgKey_None;
  (void)hdr->GetMessageKey(&msgKey);
  (void)ContainsKey(msgKey, &hasKey);
  if (hasKey)
  {
    NS_ERROR("adding hdr that already exists");
    return NS_ERROR_FAILURE;
  }
  nsresult err = ThreadNewHdr(hdr, newThread);
  // we thread header before we add it to the all headers table
  // so that subject and reference threading will work (otherwise,
  // when we try to find the first header with the same subject or
  // reference, we get the new header!)
  if (NS_SUCCEEDED(err))
  {
    nsMsgKey key;
    uint32_t flags;

    newHdr->GetMessageKey(&key);
    hdr->GetRawFlags(&flags);
    // use raw flags instead of GetFlags, because GetFlags will
    // pay attention to what's in m_newSet, and this new hdr isn't
    // in m_newSet yet.
    if (flags & nsMsgMessageFlags::New)
    {
      uint32_t newFlags;
      newHdr->AndFlags(~nsMsgMessageFlags::New, &newFlags);  // make sure not filed out
      AddToNewList(key);
    }
    if (m_dbFolderInfo != NULL)
    {
      m_dbFolderInfo->ChangeNumMessages(1);
      bool isRead = true;
      IsHeaderRead(newHdr, &isRead);
      if (!isRead)
        m_dbFolderInfo->ChangeNumUnreadMessages(1);
      m_dbFolderInfo->OnKeyAdded(key);
    }

    err = m_mdbAllMsgHeadersTable->AddRow(GetEnv(), hdr->GetMDBRow());
    if (notify)
    {
      nsMsgKey threadParent;

      newHdr->GetThreadParent(&threadParent);
      NotifyHdrAddedAll(newHdr, threadParent, flags, NULL);
    }

    if (UseCorrectThreading())
      err = AddMsgRefsToHash(newHdr);
  }
  NS_ASSERTION(NS_SUCCEEDED(err), "error creating thread");
  return err;
}

NS_IMETHODIMP nsMsgDatabase::CopyHdrFromExistingHdr(nsMsgKey key, nsIMsgDBHdr *existingHdr, bool addHdrToDB, nsIMsgDBHdr **newHdr)
{
  nsresult  err = NS_OK;

  if (existingHdr)
  {
    nsMsgHdr* sourceMsgHdr = static_cast<nsMsgHdr*>(existingHdr);      // closed system, cast ok
    nsMsgHdr *destMsgHdr = nullptr;
    CreateNewHdr(key, (nsIMsgDBHdr **) &destMsgHdr);
    nsIMdbRow  *sourceRow = sourceMsgHdr->GetMDBRow();
    if (!destMsgHdr || !sourceRow)
      return NS_MSG_MESSAGE_NOT_FOUND;

    nsIMdbRow  *destRow = destMsgHdr->GetMDBRow();
    if (!destRow)
      return NS_ERROR_UNEXPECTED;

    err = destRow->SetRow(GetEnv(), sourceRow);
    if (NS_SUCCEEDED(err))
    {
      // we may have gotten the header from a cache - calling SetRow
      // basically invalidates any cached values, so invalidate them.
      destMsgHdr->ClearCachedValues();
      if(addHdrToDB)
        err = AddNewHdrToDB(destMsgHdr, true);
      if (NS_SUCCEEDED(err) && newHdr)
        *newHdr = destMsgHdr;
    }
  }
  return err;
}

nsresult nsMsgDatabase::RowCellColumnTonsString(nsIMdbRow *hdrRow, mdb_token columnToken, nsAString &resultStr)
{
  NS_ENSURE_ARG_POINTER(hdrRow);

  struct mdbYarn yarn;
  nsresult rv = hdrRow->AliasCellYarn(GetEnv(), columnToken, &yarn);
  NS_ENSURE_SUCCESS(rv, rv);
  YarnTonsString(&yarn, resultStr);
  return NS_OK;
}

// as long as the row still exists, and isn't changed, the returned const char ** will be valid.
// But be very careful using this data - the caller should never return it in turn to another caller.
nsresult nsMsgDatabase::RowCellColumnToConstCharPtr(nsIMdbRow *hdrRow, mdb_token columnToken, const char **ptr)
{
  NS_ENSURE_ARG_POINTER(hdrRow);

  struct mdbYarn yarn;
  nsresult rv = hdrRow->AliasCellYarn(GetEnv(), columnToken, &yarn);
  NS_ENSURE_SUCCESS(rv, rv);
  *ptr = (const char*)yarn.mYarn_Buf;
  return NS_OK;
}

nsIMimeConverter *nsMsgDatabase::GetMimeConverter()
{
  if (!m_mimeConverter)
  {
    // apply mime decode
    m_mimeConverter = do_GetService(NS_MIME_CONVERTER_CONTRACTID);
  }
  return m_mimeConverter;
}

nsresult nsMsgDatabase::GetEffectiveCharset(nsIMdbRow *row, nsACString &resultCharset)
{
  resultCharset.Truncate();
  bool characterSetOverride;
  m_dbFolderInfo->GetCharacterSetOverride(&characterSetOverride);
  nsresult rv = RowCellColumnToCharPtr(row, m_messageCharSetColumnToken, getter_Copies(resultCharset));
  if (NS_FAILED(rv) || resultCharset.IsEmpty() ||
      resultCharset.Equals("us-ascii") || characterSetOverride)
  {
    rv = m_dbFolderInfo->GetEffectiveCharacterSet(resultCharset);
  }
  return rv;
}

nsresult nsMsgDatabase::RowCellColumnToMime2DecodedString(nsIMdbRow *row, mdb_token columnToken, nsAString &resultStr)
{
  nsresult err = NS_OK;
  const char *nakedString = nullptr;
  err = RowCellColumnToConstCharPtr(row, columnToken, &nakedString);
  if (NS_SUCCEEDED(err) && nakedString && strlen(nakedString))
  {
    GetMimeConverter();
    if (m_mimeConverter)
    {
      nsAutoString decodedStr;
      nsCString charSet;
      GetEffectiveCharset(row, charSet);

      err = m_mimeConverter->DecodeMimeHeader(nakedString, charSet.get(),
        false, true, resultStr);
    }
  }
  return err;
}

nsresult nsMsgDatabase::RowCellColumnToAddressCollationKey(nsIMdbRow *row, mdb_token colToken, uint8_t **result, uint32_t *len)
{
  nsString sender;
  nsresult rv = RowCellColumnToMime2DecodedString(row, colToken, sender);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString name;
  ExtractName(DecodedHeader(sender), name);
  return CreateCollationKey(name, len, result);
}

nsresult nsMsgDatabase::GetCollationKeyGenerator()
{
  nsresult err = NS_OK;
  if (!m_collationKeyGenerator)
  {
    nsCOMPtr <nsILocale> locale;
    nsAutoString localeName;

    // get a locale service
    nsCOMPtr <nsILocaleService> localeService = do_GetService(NS_LOCALESERVICE_CONTRACTID, &err);
    if (NS_SUCCEEDED(err))
    {
      // do this for a new db if no UI to be provided for locale selection
      err = localeService->GetApplicationLocale(getter_AddRefs(locale));

      if (locale)
      {
        // or generate a locale from a stored locale name ("en_US", "fr_FR")
        //err = localeFactory->NewLocale(&localeName, &locale);

        nsCOMPtr <nsICollationFactory> f = do_CreateInstance(NS_COLLATIONFACTORY_CONTRACTID, &err);
        if (NS_SUCCEEDED(err) && f)
        {
          // get a collation interface instance
          err = f->CreateCollation(locale, getter_AddRefs(m_collationKeyGenerator));
        }
      }
    }
  }
  return err;
}

nsresult nsMsgDatabase::RowCellColumnToCollationKey(nsIMdbRow *row, mdb_token columnToken, uint8_t **result, uint32_t *len)
{
  const char *nakedString = nullptr;
  nsresult err;

  err = RowCellColumnToConstCharPtr(row, columnToken, &nakedString);
  if (!nakedString)
    nakedString = "";
  if (NS_SUCCEEDED(err))
  {
    GetMimeConverter();
    if (m_mimeConverter)
    {
      nsCString decodedStr;
      nsCString charSet;
      GetEffectiveCharset(row, charSet);

      err = m_mimeConverter->DecodeMimeHeaderToUTF8(
        nsDependentCString(nakedString), charSet.get(), false,
        true, decodedStr);
      if (NS_SUCCEEDED(err))
        err = CreateCollationKey(NS_ConvertUTF8toUTF16(decodedStr), len, result);
    }
  }
  return err;
}

NS_IMETHODIMP
nsMsgDatabase::CompareCollationKeys(uint32_t len1, uint8_t *key1, uint32_t len2,
                                    uint8_t *key2, int32_t *result)
{
  nsresult rv = GetCollationKeyGenerator();
  NS_ENSURE_SUCCESS(rv,rv);
  if (!m_collationKeyGenerator) return NS_ERROR_FAILURE;

  rv = m_collationKeyGenerator->CompareRawSortKey(key1,len1,key2,len2,result);
  NS_ENSURE_SUCCESS(rv,rv);
  return rv;
}

NS_IMETHODIMP
nsMsgDatabase::CreateCollationKey(const nsAString& sourceString, uint32_t *len,
                                  uint8_t **result)
{
  nsresult err = GetCollationKeyGenerator();
  NS_ENSURE_SUCCESS(err,err);
  if (!m_collationKeyGenerator) return NS_ERROR_FAILURE;

  err = m_collationKeyGenerator->AllocateRawSortKey(nsICollation::kCollationCaseInSensitive, sourceString, result, len);
  NS_ENSURE_SUCCESS(err,err);
  return err;
}

nsresult nsMsgDatabase::RowCellColumnToUInt32(nsIMdbRow *hdrRow, mdb_token columnToken, uint32_t &uint32Result, uint32_t defaultValue)
{
  return RowCellColumnToUInt32(hdrRow, columnToken, &uint32Result, defaultValue);
}

nsresult nsMsgDatabase::RowCellColumnToUInt32(nsIMdbRow *hdrRow, mdb_token columnToken, uint32_t *uint32Result, uint32_t defaultValue)
{
  nsresult  err = NS_OK;

  if (uint32Result)
    *uint32Result = defaultValue;
  if (hdrRow)  // ### probably should be an error if hdrRow is NULL...
  {
    struct mdbYarn yarn;
    err = hdrRow->AliasCellYarn(GetEnv(), columnToken, &yarn);
    if (NS_SUCCEEDED(err))
      YarnToUInt32(&yarn, uint32Result);
  }
  return err;
}

nsresult nsMsgDatabase::UInt32ToRowCellColumn(nsIMdbRow *row, mdb_token columnToken, uint32_t value)
{
  struct mdbYarn yarn;
  char  yarnBuf[100];

  if (!row)
    return NS_ERROR_NULL_POINTER;

  yarn.mYarn_Buf = (void *) yarnBuf;
  yarn.mYarn_Size = sizeof(yarnBuf);
  yarn.mYarn_Fill = yarn.mYarn_Size;
  yarn.mYarn_Form = 0;
  yarn.mYarn_Grow = NULL;
  return row->AddColumn(GetEnv(),  columnToken, UInt32ToYarn(&yarn, value));
}

nsresult nsMsgDatabase::UInt64ToRowCellColumn(nsIMdbRow *row, mdb_token columnToken, uint64_t value)
{
  NS_ENSURE_ARG_POINTER(row);
  struct mdbYarn yarn;
  char  yarnBuf[17]; // max string is 16 bytes, + 1 for null.

  yarn.mYarn_Buf = (void *) yarnBuf;
  yarn.mYarn_Size = sizeof(yarnBuf);
  yarn.mYarn_Form = 0;
  yarn.mYarn_Grow = NULL;
  PR_snprintf((char *) yarn.mYarn_Buf, yarn.mYarn_Size, "%llx", value);
  yarn.mYarn_Fill = PL_strlen((const char *) yarn.mYarn_Buf);
  return row->AddColumn(GetEnv(),  columnToken, &yarn);
}

nsresult
nsMsgDatabase::RowCellColumnToUInt64(nsIMdbRow *hdrRow, mdb_token columnToken,
                                     uint64_t *uint64Result,
                                     uint64_t defaultValue)
{
  nsresult  err = NS_OK;

  if (uint64Result)
    *uint64Result = defaultValue;
  if (hdrRow)  // ### probably should be an error if hdrRow is NULL...
  {
    struct mdbYarn yarn;
    err = hdrRow->AliasCellYarn(GetEnv(), columnToken, &yarn);
    if (NS_SUCCEEDED(err))
      YarnToUInt64(&yarn, uint64Result);
  }
  return err;
}

nsresult nsMsgDatabase::CharPtrToRowCellColumn(nsIMdbRow *row, mdb_token columnToken, const char *charPtr)
{
  if (!row)
    return NS_ERROR_NULL_POINTER;

  struct mdbYarn yarn;
  yarn.mYarn_Buf = (void *) charPtr;
  yarn.mYarn_Size = PL_strlen((const char *) yarn.mYarn_Buf) + 1;
  yarn.mYarn_Fill = yarn.mYarn_Size - 1;
  yarn.mYarn_Form = 0;  // what to do with this? we're storing csid in the msg hdr...

  return row->AddColumn(GetEnv(),  columnToken, &yarn);
}

// caller must NS_Free result
nsresult nsMsgDatabase::RowCellColumnToCharPtr(nsIMdbRow *row, mdb_token columnToken, char **result)
{
  nsresult  err = NS_ERROR_NULL_POINTER;

  if (row && result)
  {
    struct mdbYarn yarn;
    err = row->AliasCellYarn(GetEnv(), columnToken, &yarn);
    if (NS_SUCCEEDED(err))
    {
      *result = (char *)NS_Alloc(yarn.mYarn_Fill + 1);
      if (*result)
      {
        if (yarn.mYarn_Fill > 0)
          memcpy(*result, yarn.mYarn_Buf, yarn.mYarn_Fill);
        (*result)[yarn.mYarn_Fill] = '\0';
      }
      else
        err = NS_ERROR_OUT_OF_MEMORY;

    }
  }
  return err;
}



/* static */struct mdbYarn *nsMsgDatabase::nsStringToYarn(struct mdbYarn *yarn, const nsAString &str)
{
  yarn->mYarn_Buf = ToNewCString(NS_ConvertUTF16toUTF8(str));
  yarn->mYarn_Size = str.Length() + 1;
  yarn->mYarn_Fill = yarn->mYarn_Size - 1;
  yarn->mYarn_Form = 0;  // what to do with this? we're storing csid in the msg hdr...
  return yarn;
}

/* static */struct mdbYarn *nsMsgDatabase::UInt32ToYarn(struct mdbYarn *yarn, uint32_t i)
{
  PR_snprintf((char *) yarn->mYarn_Buf, yarn->mYarn_Size, "%lx", i);
  yarn->mYarn_Fill = PL_strlen((const char *) yarn->mYarn_Buf);
  yarn->mYarn_Form = 0;  // what to do with this? Should be parsed out of the mime2 header?
  return yarn;
}

/* static */struct mdbYarn *nsMsgDatabase::UInt64ToYarn(struct mdbYarn *yarn, uint64_t i)
{
  PR_snprintf((char *) yarn->mYarn_Buf, yarn->mYarn_Size, "%llx", i);
  yarn->mYarn_Fill = PL_strlen((const char *) yarn->mYarn_Buf);
  yarn->mYarn_Form = 0;
  return yarn;
}

/* static */void nsMsgDatabase::YarnTonsString(struct mdbYarn *yarn, nsAString &str)
{
  const char* buf = (const char*)yarn->mYarn_Buf;
  if (buf)
    CopyASCIItoUTF16(Substring(buf, buf + yarn->mYarn_Fill), str);
  else
    str.Truncate();
}

/* static */void nsMsgDatabase::YarnTonsCString(struct mdbYarn *yarn, nsACString &str)
{
    const char* buf = (const char*)yarn->mYarn_Buf;
    if (buf)
        str.Assign(buf, yarn->mYarn_Fill);
    else
        str.Truncate();
}

// WARNING - if yarn is empty, *pResult will not be changed!!!!
// this is so we can leave default values as they were.
/* static */void nsMsgDatabase::YarnToUInt32(struct mdbYarn *yarn, uint32_t *pResult)
{
  uint8_t numChars = std::min<mdb_fill>(8, yarn->mYarn_Fill);

  if (numChars == 0)
    return;

  *pResult = MsgUnhex((char *) yarn->mYarn_Buf, numChars);
}

// WARNING - if yarn is empty, *pResult will not be changed!!!!
// this is so we can leave default values as they were.
/* static */void nsMsgDatabase::YarnToUInt64(struct mdbYarn *yarn, uint64_t *pResult)
{
  uint8_t numChars = std::min<mdb_fill>(16, yarn->mYarn_Fill);

  if (numChars == 0)
    return;

  *pResult = MsgUnhex((char *) yarn->mYarn_Buf, numChars);
}

nsresult nsMsgDatabase::GetProperty(nsIMdbRow *row, const char *propertyName, char **result)
{
  nsresult err = NS_OK;
  mdb_token  property_token;

  if (m_mdbStore)
    err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  else
    err = NS_ERROR_NULL_POINTER;
  if (NS_SUCCEEDED(err))
    err = RowCellColumnToCharPtr(row, property_token, result);

  return err;
}

nsresult nsMsgDatabase::SetProperty(nsIMdbRow *row, const char *propertyName, const char *propertyVal)
{
  nsresult err = NS_OK;
  mdb_token  property_token;

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  if (NS_SUCCEEDED(err))
    CharPtrToRowCellColumn(row, property_token, propertyVal);
  return err;
}

nsresult nsMsgDatabase::GetPropertyAsNSString(nsIMdbRow *row, const char *propertyName, nsAString &result)
{
  nsresult err = NS_OK;
  mdb_token  property_token;

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  if (NS_SUCCEEDED(err))
    err = RowCellColumnTonsString(row, property_token, result);

  return err;
}

nsresult nsMsgDatabase::SetPropertyFromNSString(nsIMdbRow *row, const char *propertyName, const nsAString &propertyVal)
{
  nsresult err = NS_OK;
  mdb_token  property_token;

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  if (NS_SUCCEEDED(err))
    return SetNSStringPropertyWithToken(row, property_token, propertyVal);

  return err;
}


nsresult nsMsgDatabase::GetUint32Property(nsIMdbRow *row, const char *propertyName, uint32_t *result, uint32_t defaultValue)
{
  nsresult err = NS_OK;
  mdb_token  property_token;

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  if (NS_SUCCEEDED(err))
    err = RowCellColumnToUInt32(row, property_token, result, defaultValue);

  return err;
}

nsresult nsMsgDatabase::GetUint64Property(nsIMdbRow *row, const char *propertyName, uint64_t *result, uint64_t defaultValue)
{
  nsresult err = NS_OK;
  mdb_token property_token;

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  err = m_mdbStore->StringToToken(GetEnv(), propertyName, &property_token);
  if (NS_SUCCEEDED(err))
    err = RowCellColumnToUInt64(row, property_token, result, defaultValue);

  return err;
}

nsresult nsMsgDatabase::SetUint32Property(nsIMdbRow *row, const char *propertyName, uint32_t propertyVal)
{
  struct mdbYarn yarn;
  char  int32StrBuf[20];
  yarn.mYarn_Buf = int32StrBuf;
  yarn.mYarn_Size = sizeof(int32StrBuf);
  yarn.mYarn_Fill = sizeof(int32StrBuf);

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  mdb_token  property_token;

  nsresult err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  if (NS_SUCCEEDED(err))
  {
    UInt32ToYarn(&yarn, propertyVal);
    err = row->AddColumn(GetEnv(), property_token, &yarn);
  }
  return err;
}

nsresult nsMsgDatabase::SetUint64Property(nsIMdbRow *row,
                                          const char *propertyName,
                                          uint64_t propertyVal)
{
  struct mdbYarn yarn;
  char  int64StrBuf[100];
  yarn.mYarn_Buf = int64StrBuf;
  yarn.mYarn_Size = sizeof(int64StrBuf);
  yarn.mYarn_Fill = sizeof(int64StrBuf);

  NS_ENSURE_STATE(m_mdbStore); // db might have been closed out from under us.
  if (!row)
    return NS_ERROR_NULL_POINTER;

  mdb_token  property_token;

  nsresult err = m_mdbStore->StringToToken(GetEnv(),  propertyName, &property_token);
  if (NS_SUCCEEDED(err))
  {
    UInt64ToYarn(&yarn, propertyVal);
    err = row->AddColumn(GetEnv(), property_token, &yarn);
  }
  return err;
}

nsresult nsMsgDatabase::GetBooleanProperty(nsIMdbRow *row, const char *propertyName, 
                                     bool *result, 
                                     bool defaultValue /* = false */)
{
  uint32_t res;
  nsresult rv = GetUint32Property(row, propertyName, &res, (uint32_t) defaultValue);
  *result = !!res;
  return rv;
}

nsresult nsMsgDatabase::SetBooleanProperty(nsIMdbRow *row, const char *propertyName, 
                                    bool propertyVal)
{
  return SetUint32Property(row, propertyName, (uint32_t) propertyVal);
}

nsresult nsMsgDatabase::SetNSStringPropertyWithToken(nsIMdbRow *row, mdb_token aProperty, const nsAString &propertyStr)
{
  NS_ENSURE_ARG(row);
  struct mdbYarn yarn;

  yarn.mYarn_Grow = NULL;
  nsresult err = row->AddColumn(GetEnv(), aProperty, nsStringToYarn(&yarn, propertyStr));
  free((char *)yarn.mYarn_Buf);  // won't need this when we have nsCString
  return err;
}


uint32_t nsMsgDatabase::GetCurVersion()
{
  return kMsgDBVersion;
}

NS_IMETHODIMP nsMsgDatabase::SetSummaryValid(bool valid /* = true */)
{
  // If the file was invalid when opened (for example in folder compact), then it may
  //  not have been added to the cache. Add it now if missing.
  if (valid)
  {
    nsCOMPtr<nsIMsgDBService> serv(mozilla::services::GetDBService());
    static_cast<nsMsgDBService*>(serv.get())->EnsureCached(this);
  }
  // setting the version to 0 ought to make it pretty invalid.
  if (m_dbFolderInfo)
    m_dbFolderInfo->SetVersion(valid ? GetCurVersion() : 0);

  // for default db (and news), there's no nothing to set to make it it valid
  return NS_OK;
}


NS_IMETHODIMP  nsMsgDatabase::GetSummaryValid(bool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = true;
  return NS_OK;
}


// protected routines

// should we thread messages with common subjects that don't start with Re: together?
// I imagine we might have separate preferences for mail and news, so this is a virtual method.
bool    nsMsgDatabase::ThreadBySubjectWithoutRe()
{
  GetGlobalPrefs();
  return gThreadWithoutRe;
}

bool nsMsgDatabase::UseStrictThreading()
{
  GetGlobalPrefs();
  return gStrictThreading;
}

// Should we make sure messages are always threaded correctly (see bug 181446)
bool nsMsgDatabase::UseCorrectThreading()
{
  GetGlobalPrefs();
  return gCorrectThreading;
}

// adapted from removed PL_DHashFreeStringKey
static void
msg_DHashFreeStringKey(PLDHashTable* aTable, PLDHashEntryHdr* aEntry)
{
  const PLDHashEntryStub* stub = (const PLDHashEntryStub*)aEntry;
  free((void*)stub->key);
  PLDHashTable::ClearEntryStub(aTable, aEntry);
}

PLDHashTableOps nsMsgDatabase::gRefHashTableOps =
{
  PLDHashTable::HashStringKey,
  PLDHashTable::MatchStringKey,
  PLDHashTable::MoveEntryStub,
  msg_DHashFreeStringKey,
  nullptr
};

nsresult nsMsgDatabase::GetRefFromHash(nsCString &reference, nsMsgKey *threadId)
{
  // Initialize the reference hash
  if (!m_msgReferences)
  {
    nsresult rv = InitRefHash();
    if (NS_FAILED(rv))
      return rv;
  }

  // Find reference from the hash
  PLDHashEntryHdr *entry =
    m_msgReferences->Search((const void *) reference.get());
  if (entry)
  {
    RefHashElement *element = static_cast<RefHashElement *>(entry);
    *threadId = element->mThreadId;
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

nsresult nsMsgDatabase::AddRefToHash(nsCString &reference, nsMsgKey threadId)
{
  if (m_msgReferences)
  {
    PLDHashEntryHdr *entry = m_msgReferences->Add((void *) reference.get(), mozilla::fallible);
    if (!entry)
      return NS_ERROR_OUT_OF_MEMORY; // XXX out of memory

    RefHashElement *element = static_cast<RefHashElement *>(entry);
    if (!element->mRef)
    {
      element->mRef = ToNewCString(reference);  // Will be freed in msg_DHashFreeStringKey()
      element->mThreadId = threadId;
      element->mCount = 1;
    }
    else
      element->mCount++;
  }

  return NS_OK;
}

nsresult nsMsgDatabase::AddMsgRefsToHash(nsIMsgDBHdr *msgHdr)
{
  uint16_t numReferences = 0;
  nsMsgKey threadId;
  nsresult rv = NS_OK;

  msgHdr->GetThreadId(&threadId);
  msgHdr->GetNumReferences(&numReferences);

  for (int32_t i = 0; i < numReferences; i++)
  {
    nsAutoCString reference;
    
    msgHdr->GetStringReference(i, reference);
    if (reference.IsEmpty())
      break;

    rv = AddRefToHash(reference, threadId);
    if (NS_FAILED(rv))
      break;
  }

  return rv;
}

nsresult nsMsgDatabase::RemoveRefFromHash(nsCString &reference)
{
  if (m_msgReferences)
  {
    PLDHashEntryHdr *entry =
     m_msgReferences->Search((const void *) reference.get());
    if (entry)
    {
      RefHashElement *element = static_cast<RefHashElement *>(entry);
      if (--element->mCount == 0)
        m_msgReferences->Remove((void *) reference.get());
    }
  }
  return NS_OK;
}

// Filter only messages with one or more references
nsresult nsMsgDatabase::RemoveMsgRefsFromHash(nsIMsgDBHdr *msgHdr)
{
  uint16_t numReferences = 0;
  nsresult rv = NS_OK;

  msgHdr->GetNumReferences(&numReferences);

  for (int32_t i = 0; i < numReferences; i++)
  {
    nsAutoCString reference;
    
    msgHdr->GetStringReference(i, reference);
    if (reference.IsEmpty())
      break;

    rv = RemoveRefFromHash(reference);
    if (NS_FAILED(rv))
      break;
  }

  return rv;
}

static nsresult nsReferencesOnlyFilter(nsIMsgDBHdr *msg, void *closure)
{
  uint16_t numReferences = 0;
  msg->GetNumReferences(&numReferences);
  return (numReferences) ? NS_OK : NS_ERROR_FAILURE;
}

nsresult nsMsgDatabase::InitRefHash()
{
  // Delete an existing table just in case
  if (m_msgReferences)
    delete m_msgReferences;

  // Create new table
  m_msgReferences = new PLDHashTable(&gRefHashTableOps, sizeof(struct RefHashElement), MSG_HASH_SIZE);
  if (!m_msgReferences)
    return NS_ERROR_OUT_OF_MEMORY;

  // Create enumerator to go through all messages with references
  nsCOMPtr <nsISimpleEnumerator> enumerator;
  enumerator = new nsMsgDBEnumerator(this, m_mdbAllMsgHeadersTable, nsReferencesOnlyFilter, nullptr);
  if (enumerator == nullptr)
    return NS_ERROR_OUT_OF_MEMORY;

  // Populate table with references of existing messages
  bool hasMore;
  nsresult rv = NS_OK;
  while (NS_SUCCEEDED(rv = enumerator->HasMoreElements(&hasMore)) && hasMore)
  {
    nsCOMPtr <nsISupports> supports;
    rv = enumerator->GetNext(getter_AddRefs(supports));
    NS_ASSERTION(NS_SUCCEEDED(rv), "nsMsgDBEnumerator broken");
    nsCOMPtr <nsIMsgDBHdr> msgHdr = do_QueryInterface(supports);
    if (msgHdr && NS_SUCCEEDED(rv))
      rv = AddMsgRefsToHash(msgHdr);
    if (NS_FAILED(rv))
      break;
  }

  return rv;
}

nsresult nsMsgDatabase::CreateNewThread(nsMsgKey threadId, const char *subject, nsMsgThread **pnewThread)
{
  nsresult  err = NS_OK;
  nsCOMPtr<nsIMdbTable> threadTable;
  struct mdbOid threadTableOID;
  struct mdbOid allThreadsTableOID;

  if (!pnewThread || !m_mdbStore)
    return NS_ERROR_NULL_POINTER;

  threadTableOID.mOid_Scope = m_hdrRowScopeToken;
  threadTableOID.mOid_Id = threadId;

  // Under some circumstances, mork seems to reuse an old table when we create one.
  // Prevent problems from that by finding any old table first, and deleting its rows.
  nsresult res = GetStore()->GetTable(GetEnv(), &threadTableOID, getter_AddRefs(threadTable));
  if (NS_SUCCEEDED(res) && threadTable)
    threadTable->CutAllRows(GetEnv());

  err  = GetStore()->NewTableWithOid(GetEnv(), &threadTableOID, m_threadTableKindToken,
    false, nullptr, getter_AddRefs(threadTable));
  if (NS_FAILED(err))
    return err;

  allThreadsTableOID.mOid_Scope = m_threadRowScopeToken;
  allThreadsTableOID.mOid_Id = threadId;

  // add a row for this thread in the table of all threads that we'll use
  // to do our mapping between subject strings and threads.
  nsCOMPtr<nsIMdbRow> threadRow;

  err = m_mdbStore->GetRow(GetEnv(), &allThreadsTableOID,
                           getter_AddRefs(threadRow));
  if (!threadRow)
  {
    err  = m_mdbStore->NewRowWithOid(GetEnv(), &allThreadsTableOID,
                                     getter_AddRefs(threadRow));
    if (NS_SUCCEEDED(err) && threadRow)
    {
      if (m_mdbAllThreadsTable)
        m_mdbAllThreadsTable->AddRow(GetEnv(), threadRow);
      err = CharPtrToRowCellColumn(threadRow, m_threadSubjectColumnToken, subject);
    }
  }
  else
  {
#ifdef DEBUG_David_Bienvenu
    NS_WARNING("odd that thread row already exists");
#endif
    threadRow->CutAllColumns(GetEnv());
    nsCOMPtr<nsIMdbRow> metaRow;
    threadTable->GetMetaRow(GetEnv(), nullptr, nullptr, getter_AddRefs(metaRow));
    if (metaRow)
      metaRow->CutAllColumns(GetEnv());

    CharPtrToRowCellColumn(threadRow, m_threadSubjectColumnToken, subject);
  }


  *pnewThread = new nsMsgThread(this, threadTable);
  if (*pnewThread)
  {
    (*pnewThread)->SetThreadKey(threadId);
     m_cachedThread = *pnewThread;
     m_cachedThreadId = threadId;
  }
  return err;
}


nsIMsgThread *nsMsgDatabase::GetThreadForReference(nsCString &msgID, nsIMsgDBHdr **pMsgHdr)
{
  nsMsgKey threadId;
  nsIMsgDBHdr  *msgHdr = nullptr;
  GetMsgHdrForMessageID(msgID.get(), &msgHdr);
  nsIMsgThread *thread = NULL;

  if (msgHdr != NULL)
  {
    if (NS_SUCCEEDED(msgHdr->GetThreadId(&threadId)))
    {
      // find thread header for header whose message id we matched.
      thread = GetThreadForThreadId(threadId);
    }
    if (pMsgHdr)
      *pMsgHdr = msgHdr;
    else
      msgHdr->Release();
  }
  // Referenced message not found, check if there are messages that reference same message
  else if (UseCorrectThreading())
  {
	if (NS_SUCCEEDED(GetRefFromHash(msgID, &threadId)))
	  thread = GetThreadForThreadId(threadId);
  }

  return thread;
}

nsIMsgThread *  nsMsgDatabase::GetThreadForSubject(nsCString &subject)
{
  nsIMsgThread *thread = nullptr;

  mdbYarn  subjectYarn;

  subjectYarn.mYarn_Buf = (void*)subject.get();
  subjectYarn.mYarn_Fill = PL_strlen(subject.get());
  subjectYarn.mYarn_Form = 0;
  subjectYarn.mYarn_Size = subjectYarn.mYarn_Fill;

  nsCOMPtr <nsIMdbRow>  threadRow;
  mdbOid    outRowId;
  if (m_mdbStore)
  {
    nsresult result = m_mdbStore->FindRow(GetEnv(), m_threadRowScopeToken,
      m_threadSubjectColumnToken, &subjectYarn,  &outRowId, getter_AddRefs(threadRow));
    if (NS_SUCCEEDED(result) && threadRow)
    {
      //Get key from row
      mdbOid outOid;
      nsMsgKey key = nsMsgKey_None;
      if (NS_SUCCEEDED(threadRow->GetOid(GetEnv(), &outOid)))
        key = outOid.mOid_Id;
      // find thread header for header whose message id we matched.
      // It is fine if key was not found,
      // GetThreadForThreadId(nsMsgKey_None) returns nullptr.
      thread = GetThreadForThreadId(key);
    }
#ifdef DEBUG_bienvenu1
    else
    {
      nsresult  rv;
      RefPtr<nsMsgThread> pThread;

      nsCOMPtr <nsIMdbPortTableCursor> tableCursor;
      m_mdbStore->GetPortTableCursor(GetEnv(), m_hdrRowScopeToken, m_threadTableKindToken,
                                     getter_AddRefs(tableCursor));

        nsCOMPtr<nsIMdbTable> table;

        while (true)
        {
          rv = tableCursor->NextTable(GetEnv(), getter_AddRefs(table));
          if (!table)
            break;
          if (NS_FAILED(rv))
            break;

          pThread = new nsMsgThread(this, table);
          if (pThread)
          {
            nsCString curSubject;
            pThread->GetSubject(curSubject);
            if (subject.Equals(curSubject))
            {
              NS_ERROR("thread with subject exists, but FindRow didn't find it\n");
              break;
            }
          }
          else
            break;
        }
      }
#endif
  }
  return thread;
}

// Returns thread that contains a message that references the passed message ID
nsIMsgThread *nsMsgDatabase::GetThreadForMessageId(nsCString &msgId)
{
  nsIMsgThread *thread = NULL;
  nsMsgKey threadId;
  
  if (NS_SUCCEEDED(GetRefFromHash(msgId, &threadId)))
    thread = GetThreadForThreadId(threadId);

  return thread;
}

nsresult nsMsgDatabase::ThreadNewHdr(nsMsgHdr* newHdr, bool &newThread)
{
  nsresult result=NS_ERROR_UNEXPECTED;
  nsCOMPtr <nsIMsgThread> thread;
  nsCOMPtr <nsIMsgDBHdr> replyToHdr;
  nsMsgKey threadId = nsMsgKey_None, newHdrKey;

  if (!newHdr)
    return NS_ERROR_NULL_POINTER;

  newHdr->SetThreadParent(nsMsgKey_None); // if we're undoing, could have a thread parent
  uint16_t numReferences = 0;
  uint32_t newHdrFlags = 0;

  // use raw flags instead of GetFlags, because GetFlags will
  // pay attention to what's in m_newSet, and this new hdr isn't
  // in m_newSet yet.
  newHdr->GetRawFlags(&newHdrFlags);
  newHdr->GetNumReferences(&numReferences);
  newHdr->GetMessageKey(&newHdrKey);

  // try reference threading first
  for (int32_t i = numReferences - 1; i >= 0;  i--)
  {
    nsAutoCString reference;

    newHdr->GetStringReference(i, reference);
    // first reference we have hdr for is best top-level hdr.
    // but we have to handle case of promoting new header to top-level
    // in case the top-level header comes after a reply.

    if (reference.IsEmpty())
      break;

    thread = dont_AddRef(GetThreadForReference(reference, getter_AddRefs(replyToHdr))) ;
    if (thread)
    {
      if (replyToHdr)
      {
        nsMsgKey replyToKey;
        replyToHdr->GetMessageKey(&replyToKey);
        // message claims to be a reply to itself - ignore that since it leads to corrupt threading.
        if (replyToKey == newHdrKey)
        {
          // bad references - throw them all away.
          newHdr->SetMessageId("");
          thread = nullptr;
          break;
        }
      }
      thread->GetThreadKey(&threadId);
      newHdr->SetThreadId(threadId);
      result = AddToThread(newHdr, thread, replyToHdr, true);
      break;
    }
  }
  // if user hasn't said "only thread by ref headers", thread by subject
  if (!thread && !UseStrictThreading())
  {
    // try subject threading if we couldn't find a reference and the subject starts with Re:
    nsCString subject;
    newHdr->GetSubject(getter_Copies(subject));
    if (ThreadBySubjectWithoutRe() || (newHdrFlags & nsMsgMessageFlags::HasRe))
    {
      nsAutoCString cSubject(subject);
      thread = dont_AddRef(GetThreadForSubject(cSubject));
      if(thread)
      {
        thread->GetThreadKey(&threadId);
        newHdr->SetThreadId(threadId);
        //TRACE("threading based on subject %s\n", (const char *) msgHdr->m_subject);
        // if we move this and do subject threading after, ref threading,
        // don't thread within children, since we know it won't work. But for now, pass TRUE.
        result = AddToThread(newHdr, thread, nullptr, true);
      }
    }
  }

  // Check if this is a new parent to an existing message (that has a reference to this message)
  if (!thread && UseCorrectThreading())
  {
    nsCString msgId;
    newHdr->GetMessageId(getter_Copies(msgId));

    thread = dont_AddRef(GetThreadForMessageId(msgId));
    if (thread)
    {
      thread->GetThreadKey(&threadId);
      newHdr->SetThreadId(threadId);
      result = AddToThread(newHdr, thread, nullptr, true);
    }
  }

  if (!thread)
  {
    // Not a parent or child, make it a new thread for now
    result = AddNewThread(newHdr);
    newThread = true;
  }
  else
  {
    newThread = false;
  }
  return result;
}

nsresult nsMsgDatabase::AddToThread(nsMsgHdr *newHdr, nsIMsgThread *thread, nsIMsgDBHdr *inReplyTo, bool threadInThread)
{
  // don't worry about real threading yet.
  nsCOMPtr <nsIDBChangeAnnouncer> announcer = do_QueryInterface(this);

  return thread->AddChild(newHdr, inReplyTo, threadInThread, announcer);
}

nsMsgHdr * nsMsgDatabase::GetMsgHdrForReference(nsCString &reference)
{
  NS_ASSERTION(false, "not implemented yet.");
  return nullptr;
}

NS_IMETHODIMP nsMsgDatabase::GetMsgHdrForMessageID(const char *aMsgID, nsIMsgDBHdr **aHdr)
{
  NS_ENSURE_ARG_POINTER(aHdr);
  NS_ENSURE_ARG_POINTER(aMsgID);
  nsIMsgDBHdr  *msgHdr = nullptr;
  nsresult rv = NS_OK;
  mdbYarn  messageIdYarn;

  messageIdYarn.mYarn_Buf = (void *) aMsgID;
  messageIdYarn.mYarn_Fill = PL_strlen(aMsgID);
  messageIdYarn.mYarn_Form = 0;
  messageIdYarn.mYarn_Size = messageIdYarn.mYarn_Fill;

  nsIMdbRow *hdrRow;
  mdbOid outRowId;
  nsresult result;
  if (m_mdbStore)
    result = m_mdbStore->FindRow(GetEnv(), m_hdrRowScopeToken,
      m_messageIdColumnToken, &messageIdYarn,  &outRowId,
      &hdrRow);
  else
    return NS_ERROR_FAILURE;
  if (NS_SUCCEEDED(result) && hdrRow)
  {
    //Get key from row
    mdbOid outOid;
    nsMsgKey key = nsMsgKey_None;
    rv = hdrRow->GetOid(GetEnv(), &outOid);
    if (NS_WARN_IF(NS_FAILED(rv)))
      return rv;
    key = outOid.mOid_Id;

    rv = GetHdrFromUseCache(key, &msgHdr);
    if (NS_SUCCEEDED(rv) && msgHdr)
      hdrRow->Release();
    else {
      rv = CreateMsgHdr(hdrRow, key, &msgHdr);
      if (NS_WARN_IF(NS_FAILED(rv)))
        return rv;
    }
  }
  *aHdr = msgHdr; // already addreffed above.
  return NS_OK; // it's not an error not to find a msg hdr.
}

NS_IMETHODIMP nsMsgDatabase::GetMsgHdrForGMMsgID(const char *aGMMsgId, nsIMsgDBHdr **aHdr)
{
  NS_ENSURE_ARG_POINTER(aGMMsgId);
  NS_ENSURE_ARG_POINTER(aHdr);
  nsIMsgDBHdr *msgHdr = nullptr;
  nsresult rv = NS_OK;
  mdbYarn gMailMessageIdYarn;
  gMailMessageIdYarn.mYarn_Buf = (void *) aGMMsgId;
  gMailMessageIdYarn.mYarn_Fill = strlen(aGMMsgId);
  gMailMessageIdYarn.mYarn_Form = 0;
  gMailMessageIdYarn.mYarn_Size = gMailMessageIdYarn.mYarn_Fill;

  nsIMdbRow *hdrRow;
  mdbOid outRowId;
  nsresult result;
  mdb_token property_token;
  NS_ENSURE_TRUE(m_mdbStore, NS_ERROR_NULL_POINTER);
  result = m_mdbStore->StringToToken(GetEnv(), "X-GM-MSGID",
    &property_token);
  NS_ENSURE_SUCCESS(result, result);
  result = m_mdbStore->FindRow(GetEnv(), m_hdrRowScopeToken,
    property_token, &gMailMessageIdYarn, &outRowId, &hdrRow);
  if (NS_SUCCEEDED(result) && hdrRow)
  {
    // Get key from row
    mdbOid outOid;
    rv = hdrRow->GetOid(GetEnv(), &outOid);
    NS_ENSURE_SUCCESS(rv, rv);
    nsMsgKey key = outOid.mOid_Id;
    rv = GetHdrFromUseCache(key, &msgHdr);
    if ((NS_SUCCEEDED(rv) && msgHdr))
      hdrRow->Release();
    else {
      rv = CreateMsgHdr(hdrRow, key, &msgHdr);
      if (NS_WARN_IF(NS_FAILED(rv)))
        return rv;
    }
  }
  *aHdr = msgHdr;
  return NS_OK; // it's not an error not to find a msg hdr.
}

nsIMsgDBHdr *nsMsgDatabase::GetMsgHdrForSubject(nsCString &subject)
{
  nsIMsgDBHdr *msgHdr = nullptr;
  nsresult rv = NS_OK;
  mdbYarn subjectYarn;

  subjectYarn.mYarn_Buf = (void*)subject.get();
  subjectYarn.mYarn_Fill = PL_strlen(subject.get());
  subjectYarn.mYarn_Form = 0;
  subjectYarn.mYarn_Size = subjectYarn.mYarn_Fill;

  nsIMdbRow *hdrRow;
  mdbOid outRowId;
  nsresult result = GetStore()->FindRow(GetEnv(), m_hdrRowScopeToken,
    m_subjectColumnToken, &subjectYarn,  &outRowId,
    &hdrRow);
  if (NS_SUCCEEDED(result) && hdrRow)
  {
    //Get key from row
    mdbOid outOid;
    nsMsgKey key = nsMsgKey_None;
    rv = hdrRow->GetOid(GetEnv(), &outOid);
    if (NS_WARN_IF(NS_FAILED(rv)))
      return nullptr;
    key = outOid.mOid_Id;

    rv = GetHdrFromUseCache(key, &msgHdr);
    if (NS_SUCCEEDED(rv) && msgHdr)
      hdrRow->Release();
    else {
      rv = CreateMsgHdr(hdrRow, key, &msgHdr);
      if (NS_WARN_IF(NS_FAILED(rv)))
        return nullptr;
    }
  }
  return msgHdr;
}

NS_IMETHODIMP nsMsgDatabase::GetThreadContainingMsgHdr(nsIMsgDBHdr *msgHdr, nsIMsgThread **result)
{
  NS_ENSURE_ARG_POINTER(msgHdr);
  NS_ENSURE_ARG_POINTER(result);

  *result = nullptr;
  nsMsgKey threadId = nsMsgKey_None;
  (void)msgHdr->GetThreadId(&threadId);
  if (threadId != nsMsgKey_None)
    *result = GetThreadForThreadId(threadId);

  // if we can't find the thread, try using the msg key as the thread id,
  // because the msg hdr might not have the thread id set correctly
  // Or maybe the message was deleted?
  if (!*result)
  {
    nsMsgKey msgKey;
    msgHdr->GetMessageKey(&msgKey);
    *result = GetThreadForThreadId(msgKey);
  }
  // failure is normal when message was deleted
  return (*result) ? NS_OK : NS_ERROR_FAILURE;
}


nsresult nsMsgDatabase::GetThreadForMsgKey(nsMsgKey msgKey, nsIMsgThread **aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  nsCOMPtr <nsIMsgDBHdr> msg;
  nsresult rv = GetMsgHdrForKey(msgKey, getter_AddRefs(msg));

  if (NS_SUCCEEDED(rv) && msg)
    rv = GetThreadContainingMsgHdr(msg, aResult);

  return rv;
}

// caller needs to unrefer.
nsIMsgThread *  nsMsgDatabase::GetThreadForThreadId(nsMsgKey threadId)
{

  nsIMsgThread *retThread = (threadId == m_cachedThreadId && m_cachedThread) ?
    m_cachedThread.get() : FindExistingThread(threadId);
  if (retThread)
  {
    NS_ADDREF(retThread);
    return retThread;
  }
  if (m_mdbStore)
  {
    mdbOid tableId;
    tableId.mOid_Id = threadId;
    tableId.mOid_Scope = m_hdrRowScopeToken;

    nsCOMPtr<nsIMdbTable> threadTable;
    nsresult res = m_mdbStore->GetTable(GetEnv(), &tableId,
                                        getter_AddRefs(threadTable));

    if (NS_SUCCEEDED(res) && threadTable)
    {
      retThread = new nsMsgThread(this, threadTable);
      if (retThread)
      {
        NS_ADDREF(retThread);
        m_cachedThread = retThread;
        m_cachedThreadId = threadId;
      }
    }
  }
  return retThread;
}

// make the passed in header a thread header
nsresult nsMsgDatabase::AddNewThread(nsMsgHdr *msgHdr)
{

  if (!msgHdr)
    return NS_ERROR_NULL_POINTER;

  nsMsgThread *threadHdr = nullptr;

  nsCString subject;
  nsMsgKey threadKey;
  msgHdr->GetMessageKey(&threadKey);
  // can't have a thread with key 1 since that's the table id of the all msg hdr table,
  // so give it kTableKeyForThreadOne (0xfffffffe).
  if (threadKey == kAllMsgHdrsTableKey)
    threadKey = kTableKeyForThreadOne;

  nsresult err = msgHdr->GetSubject(getter_Copies(subject));

  err = CreateNewThread(threadKey, subject.get(), &threadHdr);
  msgHdr->SetThreadId(threadKey);
  if (threadHdr)
  {
    threadHdr->AddRef();
    // err = msgHdr->GetSubject(subject);
    // threadHdr->SetThreadKey(msgHdr->m_messageKey);
    // threadHdr->SetSubject(subject.get());
    // need to add the thread table to the db.
    AddToThread(msgHdr, threadHdr, nullptr, false);
    threadHdr->Release();
  }
  return err;
}

nsresult nsMsgDatabase::GetBoolPref(const char *prefName, bool *result)
{
  bool prefValue = false;
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> pPrefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID, &rv));
  if (pPrefBranch)
  {
    rv = pPrefBranch->GetBoolPref(prefName, &prefValue);
    *result = prefValue;
  }
  return rv;
}

nsresult nsMsgDatabase::GetIntPref(const char *prefName, int32_t *result)
{
  int32_t prefValue = 0;
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> pPrefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID, &rv));
  if (pPrefBranch)
  {
    rv = pPrefBranch->GetIntPref(prefName, &prefValue);
    *result = prefValue;
  }
  return rv;
}


nsresult nsMsgDatabase::ListAllThreads(nsTArray<nsMsgKey> *threadIds)
{
  nsresult rv;
  nsMsgThread *pThread;

  nsCOMPtr <nsISimpleEnumerator> threads;
  rv = EnumerateThreads(getter_AddRefs(threads));
  if (NS_FAILED(rv)) return rv;
  bool hasMore = false;

  while (NS_SUCCEEDED(rv = threads->HasMoreElements(&hasMore)) && hasMore)
  {
    rv = threads->GetNext((nsISupports**)&pThread);
    NS_ENSURE_SUCCESS(rv,rv);

    if (threadIds) {
      nsMsgKey key;
      (void)pThread->GetThreadKey(&key);
      threadIds->AppendElement(key);
    }
    // NS_RELEASE(pThread);
    pThread = nullptr;
  }
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::SetAttributeOnPendingHdr(nsIMsgDBHdr *pendingHdr, const char *property,
                                  const char *propertyVal)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDatabase::SetUint32AttributeOnPendingHdr(nsIMsgDBHdr *pendingHdr, const char *property,
                                  uint32_t propertyVal)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMsgDatabase::SetUint64AttributeOnPendingHdr(nsIMsgDBHdr *aPendingHdr,
                                              const char *aProperty,
                                              uint64_t aPropertyVal)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMsgDatabase::UpdatePendingAttributes(nsIMsgDBHdr *aNewHdr)
{
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetOfflineOpForKey(nsMsgKey msgKey, bool create, nsIMsgOfflineImapOperation **offlineOp)
{
  NS_ASSERTION(false, "overridden by nsMailDatabase");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP  nsMsgDatabase::RemoveOfflineOp(nsIMsgOfflineImapOperation *op)
{
  NS_ASSERTION(false, "overridden by nsMailDatabase");
  return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP nsMsgDatabase::ListAllOfflineMsgs(nsIMsgKeyArray *aKeys)
{
  NS_ENSURE_ARG_POINTER(aKeys);
  nsCOMPtr <nsISimpleEnumerator> enumerator;
  uint32_t flag = nsMsgMessageFlags::Offline;
  // if we change this routine to return an enumerator that generates the keys
  // one by one, we'll need to somehow make a copy of flag for the enumerator
  // to own, since the enumerator will persist past the life of flag on the stack.
  nsresult rv = EnumerateMessagesWithFlag(getter_AddRefs(enumerator), &flag);
  if (NS_SUCCEEDED(rv) && enumerator)
  {
    bool hasMoreElements;
    while(NS_SUCCEEDED(enumerator->HasMoreElements(&hasMoreElements)) && hasMoreElements)
    {
      nsCOMPtr<nsISupports> childSupports;
      rv = enumerator->GetNext(getter_AddRefs(childSupports));
      if(NS_FAILED(rv))
        return rv;

      // clear out db hdr, because it won't be valid when we get rid of the .msf file
      nsCOMPtr<nsIMsgDBHdr> dbMessage(do_QueryInterface(childSupports, &rv));
      if(NS_SUCCEEDED(rv) && dbMessage)
      {
        nsMsgKey msgKey;
        dbMessage->GetMessageKey(&msgKey);
        aKeys->AppendElement(msgKey);
      }
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDatabase::EnumerateOfflineOps(nsISimpleEnumerator **enumerator)
{
  NS_ASSERTION(false, "overridden by nsMailDatabase");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDatabase::ListAllOfflineOpIds(nsTArray<nsMsgKey> *offlineOpIds)
{
  NS_ASSERTION(false, "overridden by nsMailDatabase");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDatabase::ListAllOfflineDeletes(nsTArray<nsMsgKey> *offlineDeletes)
{
  nsresult ret = NS_OK;
  if (!offlineDeletes)
    return NS_ERROR_NULL_POINTER;

  // technically, notimplemented, but no one's putting offline ops in anyway.
  return ret;
}
NS_IMETHODIMP nsMsgDatabase::GetHighWaterArticleNum(nsMsgKey *key)
{
  if (!m_dbFolderInfo)
    return NS_ERROR_NULL_POINTER;
  return m_dbFolderInfo->GetHighWater(key);
}

NS_IMETHODIMP nsMsgDatabase::GetLowWaterArticleNum(nsMsgKey *key)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute nsMsgKey NextPseudoMsgKey */

NS_IMETHODIMP nsMsgDatabase::GetNextPseudoMsgKey(nsMsgKey *nextPseudoMsgKey)
{
  NS_ENSURE_ARG_POINTER(nextPseudoMsgKey);
  *nextPseudoMsgKey = m_nextPseudoMsgKey--;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::SetNextPseudoMsgKey(nsMsgKey nextPseudoMsgKey)
{
  m_nextPseudoMsgKey = nextPseudoMsgKey;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetNextFakeOfflineMsgKey(nsMsgKey *nextFakeOfflineMsgKey)
{
  NS_ENSURE_ARG_POINTER(nextFakeOfflineMsgKey);
  // iterate over hdrs looking for first non-existant fake offline msg key
  nsMsgKey fakeMsgKey = kIdStartOfFake;

  bool containsKey;
  do
  {
     ContainsKey(fakeMsgKey, &containsKey);
     if (!containsKey)
       break;
     fakeMsgKey--;
  }
  while (containsKey);

  *nextFakeOfflineMsgKey = fakeMsgKey;
  return NS_OK;
}

#ifdef DEBUG
nsresult nsMsgDatabase::DumpContents()
{
  nsMsgKey key;
  uint32_t i;

  RefPtr<nsMsgKeyArray> keys = new nsMsgKeyArray;
  if (!keys)
    return NS_ERROR_OUT_OF_MEMORY;
  nsresult rv = ListAllKeys(keys);
  uint32_t numKeys;
  keys->GetLength(&numKeys);
  for (i = 0; i < numKeys; i++) {
    key = keys->m_keys[i];
    nsCOMPtr<nsIMsgDBHdr> msgHdr;
    rv = GetMsgHdrForKey(key, getter_AddRefs(msgHdr));
    if (NS_SUCCEEDED(rv))
    {
      nsCString author;
      nsCString subject;

      msgHdr->GetMessageKey(&key);
      msgHdr->GetAuthor(getter_Copies(author));
      msgHdr->GetSubject(getter_Copies(subject));
      printf("hdr key = %u, author = %s subject = %s\n", key, author.get(), subject.get());
    }
  }
  nsTArray<nsMsgKey> threads;
  rv = ListAllThreads(&threads);
  for ( i = 0; i < threads.Length(); i++)
  {
    key = threads[i];
    printf("thread key = %u\n", key);
    // DumpThread(key);
  }
  return NS_OK;
}

nsresult nsMsgDatabase::DumpMsgChildren(nsIMsgDBHdr *msgHdr)
{
  return NS_OK;
}

nsresult nsMsgDatabase::DumpThread(nsMsgKey threadId)
{
  nsresult rv = NS_OK;
  nsIMsgThread *thread = nullptr;

  thread = GetThreadForThreadId(threadId);
  if (thread)
  {
    nsISimpleEnumerator *enumerator = nullptr;

    rv = thread->EnumerateMessages(nsMsgKey_None, &enumerator);
    if (NS_SUCCEEDED(rv) && enumerator)
    {
      bool hasMore = false;

      while (NS_SUCCEEDED(rv = enumerator->HasMoreElements(&hasMore)) &&
             hasMore)
      {
        nsCOMPtr <nsISupports> supports;
        rv = enumerator->GetNext(getter_AddRefs(supports));
        NS_ASSERTION(NS_SUCCEEDED(rv), "nsMsgDBEnumerator broken");
        nsCOMPtr <nsIMsgDBHdr> pMessage = do_QueryInterface(supports);
        if (NS_FAILED(rv) || !pMessage)
          break;
      }
      NS_RELEASE(enumerator);

    }
  }
  return rv;
}
#endif /* DEBUG */

NS_IMETHODIMP nsMsgDatabase::SetMsgRetentionSettings(nsIMsgRetentionSettings *retentionSettings)
{
  m_retentionSettings = retentionSettings;
  if (retentionSettings && m_dbFolderInfo)
  {
    nsresult rv;

    nsMsgRetainByPreference retainByPreference;
    uint32_t daysToKeepHdrs;
    uint32_t numHeadersToKeep;
    uint32_t daysToKeepBodies;
    bool cleanupBodiesByDays;
    bool useServerDefaults;
    bool applyToFlaggedMessages;

    rv = retentionSettings->GetRetainByPreference(&retainByPreference);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = retentionSettings->GetDaysToKeepHdrs(&daysToKeepHdrs);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = retentionSettings->GetNumHeadersToKeep(&numHeadersToKeep);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = retentionSettings->GetDaysToKeepBodies(&daysToKeepBodies);
    NS_ENSURE_SUCCESS(rv, rv);
    (void) retentionSettings->GetCleanupBodiesByDays(&cleanupBodiesByDays);
    (void) retentionSettings->GetUseServerDefaults(&useServerDefaults);
    rv = retentionSettings->GetApplyToFlaggedMessages(&applyToFlaggedMessages);
    NS_ENSURE_SUCCESS(rv, rv);
    // need to write this to the db. We'll just use the dbfolderinfo to write properties.
    m_dbFolderInfo->SetUint32Property("retainBy", retainByPreference);
    m_dbFolderInfo->SetUint32Property("daysToKeepHdrs", daysToKeepHdrs);
    m_dbFolderInfo->SetUint32Property("numHdrsToKeep", numHeadersToKeep);
    m_dbFolderInfo->SetUint32Property("daysToKeepBodies", daysToKeepBodies);
    m_dbFolderInfo->SetBooleanProperty("cleanupBodies", cleanupBodiesByDays);
    m_dbFolderInfo->SetBooleanProperty("useServerDefaults", useServerDefaults);
    m_dbFolderInfo->SetBooleanProperty("applyToFlaggedMessages", applyToFlaggedMessages);
  }
  Commit(nsMsgDBCommitType::kLargeCommit);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetMsgRetentionSettings(nsIMsgRetentionSettings **retentionSettings)
{
  NS_ENSURE_ARG_POINTER(retentionSettings);
  if (!m_retentionSettings)
  {
    // create a new one, and initialize it from the db.
    m_retentionSettings = new nsMsgRetentionSettings;
    if (m_retentionSettings && m_dbFolderInfo)
    {
      nsMsgRetainByPreference retainByPreference;
      uint32_t daysToKeepHdrs = 0;
      uint32_t numHeadersToKeep = 0;
      bool useServerDefaults;
      uint32_t daysToKeepBodies = 0;
      bool cleanupBodiesByDays = false;
      bool applyToFlaggedMessages;

      m_dbFolderInfo->GetUint32Property("retainBy", nsIMsgRetentionSettings::nsMsgRetainAll, &retainByPreference);
      m_dbFolderInfo->GetUint32Property("daysToKeepHdrs", 0, &daysToKeepHdrs);
      m_dbFolderInfo->GetUint32Property("numHdrsToKeep", 0, &numHeadersToKeep);
      m_dbFolderInfo->GetUint32Property("daysToKeepBodies", 0, &daysToKeepBodies);
      m_dbFolderInfo->GetBooleanProperty("useServerDefaults", true, &useServerDefaults);
      m_dbFolderInfo->GetBooleanProperty("cleanupBodies", false, &cleanupBodiesByDays);
      m_dbFolderInfo->GetBooleanProperty("applyToFlaggedMessages", false,
                                         &applyToFlaggedMessages);
      m_retentionSettings->SetRetainByPreference(retainByPreference);
      m_retentionSettings->SetDaysToKeepHdrs(daysToKeepHdrs);
      m_retentionSettings->SetNumHeadersToKeep(numHeadersToKeep);
      m_retentionSettings->SetDaysToKeepBodies(daysToKeepBodies);
      m_retentionSettings->SetUseServerDefaults(useServerDefaults);
      m_retentionSettings->SetCleanupBodiesByDays(cleanupBodiesByDays);
      m_retentionSettings->SetApplyToFlaggedMessages(applyToFlaggedMessages);
    }
  }
  *retentionSettings = m_retentionSettings;
  NS_IF_ADDREF(*retentionSettings);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::SetMsgDownloadSettings(nsIMsgDownloadSettings *downloadSettings)
{
  m_downloadSettings = downloadSettings;
  if (downloadSettings && m_dbFolderInfo)
  {
    nsresult rv;

    bool useServerDefaults;
    bool downloadByDate;
    uint32_t ageLimitOfMsgsToDownload;
    bool downloadUnreadOnly;

    rv = downloadSettings->GetUseServerDefaults(&useServerDefaults);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = downloadSettings->GetDownloadByDate(&downloadByDate);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = downloadSettings->GetDownloadUnreadOnly(&downloadUnreadOnly);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = downloadSettings->GetAgeLimitOfMsgsToDownload(&ageLimitOfMsgsToDownload);
    NS_ENSURE_SUCCESS(rv, rv);
    // need to write this to the db. We'll just use the dbfolderinfo to write properties.
    m_dbFolderInfo->SetBooleanProperty("useServerDefaults", useServerDefaults);
    m_dbFolderInfo->SetBooleanProperty("downloadByDate", downloadByDate);
    m_dbFolderInfo->SetBooleanProperty("downloadUnreadOnly", downloadUnreadOnly);
    m_dbFolderInfo->SetUint32Property("ageLimit", ageLimitOfMsgsToDownload);
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetMsgDownloadSettings(nsIMsgDownloadSettings **downloadSettings)
{
  NS_ENSURE_ARG_POINTER(downloadSettings);
  if (!m_downloadSettings)
  {
    // create a new one, and initialize it from the db.
    m_downloadSettings = new nsMsgDownloadSettings;
    if (m_downloadSettings && m_dbFolderInfo)
    {
      bool useServerDefaults;
      bool downloadByDate;
      uint32_t ageLimitOfMsgsToDownload;
      bool downloadUnreadOnly;

      m_dbFolderInfo->GetBooleanProperty("useServerDefaults", true, &useServerDefaults);
      m_dbFolderInfo->GetBooleanProperty("downloadByDate", false, &downloadByDate);
      m_dbFolderInfo->GetBooleanProperty("downloadUnreadOnly", false, &downloadUnreadOnly);
      m_dbFolderInfo->GetUint32Property("ageLimit", 0, &ageLimitOfMsgsToDownload);

      m_downloadSettings->SetUseServerDefaults(useServerDefaults);
      m_downloadSettings->SetDownloadByDate(downloadByDate);
      m_downloadSettings->SetDownloadUnreadOnly(downloadUnreadOnly);
      m_downloadSettings->SetAgeLimitOfMsgsToDownload(ageLimitOfMsgsToDownload);
    }
  }
  *downloadSettings = m_downloadSettings;
  NS_IF_ADDREF(*downloadSettings);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::ApplyRetentionSettings(nsIMsgRetentionSettings *aMsgRetentionSettings,
                                                    bool aDeleteViaFolder)
{
  NS_ENSURE_ARG_POINTER(aMsgRetentionSettings);
  nsresult rv = NS_OK;

  if (!m_folder)
    return NS_ERROR_NULL_POINTER;

  bool isDraftsTemplatesOutbox;
  uint32_t dtoFlags = nsMsgFolderFlags::Drafts | nsMsgFolderFlags::Templates |
                        nsMsgFolderFlags::Queue;
  (void) m_folder->IsSpecialFolder(dtoFlags, true, &isDraftsTemplatesOutbox);
  // Never apply retention settings to Drafts/Templates/Outbox.
  if (isDraftsTemplatesOutbox)
    return NS_OK;

  nsCOMPtr <nsIMutableArray> msgHdrsToDelete;
  if (aDeleteViaFolder)
  {
    msgHdrsToDelete = do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  nsMsgRetainByPreference retainByPreference;
  aMsgRetentionSettings->GetRetainByPreference(&retainByPreference);

  bool applyToFlaggedMessages = false;
  aMsgRetentionSettings->GetApplyToFlaggedMessages(&applyToFlaggedMessages);

  uint32_t daysToKeepHdrs = 0;
  uint32_t numHeadersToKeep = 0;
  switch (retainByPreference)
  {
  case nsIMsgRetentionSettings::nsMsgRetainAll:
    break;
  case nsIMsgRetentionSettings::nsMsgRetainByAge:
    aMsgRetentionSettings->GetDaysToKeepHdrs(&daysToKeepHdrs);
    rv = PurgeMessagesOlderThan(daysToKeepHdrs,
                                applyToFlaggedMessages, msgHdrsToDelete);
    break;
  case nsIMsgRetentionSettings::nsMsgRetainByNumHeaders:
    aMsgRetentionSettings->GetNumHeadersToKeep(&numHeadersToKeep);
    rv = PurgeExcessMessages(numHeadersToKeep,
                             applyToFlaggedMessages, msgHdrsToDelete);
    break;
  }
  if (m_folder)
  {
    // update the time we attempted to purge this folder
    char dateBuf[100];
    dateBuf[0] = '\0';
    PRExplodedTime exploded;
    PR_ExplodeTime(PR_Now(), PR_LocalTimeParameters, &exploded);
    PR_FormatTimeUSEnglish(dateBuf, sizeof(dateBuf), "%a %b %d %H:%M:%S %Y", &exploded);
    m_folder->SetStringProperty("LastPurgeTime", nsDependentCString(dateBuf));
  }
  if (msgHdrsToDelete)
  {
    uint32_t count;
    msgHdrsToDelete->GetLength(&count);
    if (count > 0)
      rv = m_folder->DeleteMessages(msgHdrsToDelete, nullptr, true, false, nullptr, false);
  }
  return rv;
}

nsresult nsMsgDatabase::PurgeMessagesOlderThan(uint32_t daysToKeepHdrs,
                                               bool applyToFlaggedMessages,
                                               nsIMutableArray *hdrsToDelete)
{
  nsresult rv = NS_OK;
  nsMsgHdr *pHeader;
  nsCOMPtr <nsISimpleEnumerator> hdrs;
  rv = EnumerateMessages(getter_AddRefs(hdrs));
  nsTArray<nsMsgKey> keysToDelete;

  if (NS_FAILED(rv))
    return rv;
  bool hasMore = false;

  PRTime cutOffDay = PR_Now() - daysToKeepHdrs * PR_USEC_PER_DAY;

  // so now cutOffDay is the PRTime cut-off point. Any msg with a date less than that will get purged.
  while (NS_SUCCEEDED(rv = hdrs->HasMoreElements(&hasMore)) && hasMore)
  {
    bool purgeHdr = false;

    rv = hdrs->GetNext((nsISupports**)&pHeader);
    NS_ASSERTION(NS_SUCCEEDED(rv), "nsMsgDBEnumerator broken");
    if (NS_FAILED(rv))
      break;

    if (!applyToFlaggedMessages)
    {
      uint32_t flags;
      (void)pHeader->GetFlags(&flags);
      if (flags & nsMsgMessageFlags::Marked)
        continue;
    }

    if (!purgeHdr)
    {
      PRTime date;
      pHeader->GetDate(&date);
      if (date < cutOffDay)
        purgeHdr = true;
    }
    if (purgeHdr)
    {
      nsMsgKey msgKey;
      pHeader->GetMessageKey(&msgKey);
      keysToDelete.AppendElement(msgKey);
      if (hdrsToDelete)
        hdrsToDelete->AppendElement(pHeader, false);
    }
    NS_RELEASE(pHeader);
  }

  if (!hdrsToDelete)
  {
    DeleteMessages(keysToDelete.Length(), keysToDelete.Elements(), nullptr);

    if (keysToDelete.Length() > 10) // compress commit if we deleted more than 10
      Commit(nsMsgDBCommitType::kCompressCommit);
    else if (!keysToDelete.IsEmpty())
      Commit(nsMsgDBCommitType::kLargeCommit);
  }
  return rv;
}

nsresult nsMsgDatabase::PurgeExcessMessages(uint32_t numHeadersToKeep,
                                            bool applyToFlaggedMessages,
                                            nsIMutableArray *hdrsToDelete)
{
  nsresult rv = NS_OK;
  nsMsgHdr    *pHeader;
  nsCOMPtr <nsISimpleEnumerator> hdrs;
  rv = EnumerateMessages(getter_AddRefs(hdrs));
  if (NS_FAILED(rv))
    return rv;
  bool hasMore = false;
  nsTArray<nsMsgKey> keysToDelete;

  mdb_count numHdrs = 0;
  if (m_mdbAllMsgHeadersTable)
    m_mdbAllMsgHeadersTable->GetCount(GetEnv(), &numHdrs);
  else
    return NS_ERROR_NULL_POINTER;

  while (NS_SUCCEEDED(rv = hdrs->HasMoreElements(&hasMore)) && hasMore)
  {
    bool purgeHdr = false;
    rv = hdrs->GetNext((nsISupports**)&pHeader);
    NS_ASSERTION(NS_SUCCEEDED(rv), "nsMsgDBEnumerator broken");
    if (NS_FAILED(rv))
      break;

    if (!applyToFlaggedMessages)
    {
      uint32_t flags;
      (void)pHeader->GetFlags(&flags);
      if (flags & nsMsgMessageFlags::Marked)
        continue;
    }

    // this isn't quite right - we want to prefer unread messages (keep all of those we can)
    if (numHdrs > numHeadersToKeep)
      purgeHdr = true;

    if (purgeHdr)
    {
      nsMsgKey msgKey;
      pHeader->GetMessageKey(&msgKey);
      keysToDelete.AppendElement(msgKey);
      numHdrs--;
      if (hdrsToDelete)
        hdrsToDelete->AppendElement(pHeader, false);
    }
    NS_RELEASE(pHeader);
  }

  if (!hdrsToDelete)
  {
    int32_t numKeysToDelete = keysToDelete.Length();
    if (numKeysToDelete > 0)
    {
      DeleteMessages(keysToDelete.Length(), keysToDelete.Elements(), nullptr);
      if (numKeysToDelete > 10)  // compress commit if we deleted more than 10
        Commit(nsMsgDBCommitType::kCompressCommit);
      else
        Commit(nsMsgDBCommitType::kLargeCommit);
    }
  }
  return rv;
}

NS_IMPL_ISUPPORTS(nsMsgRetentionSettings, nsIMsgRetentionSettings)

// Initialise the member variables to resonable defaults.
nsMsgRetentionSettings::nsMsgRetentionSettings()
: m_retainByPreference(1),
  m_daysToKeepHdrs(0),
  m_numHeadersToKeep(0),
  m_useServerDefaults(true),
  m_cleanupBodiesByDays(false),
  m_daysToKeepBodies(0),
  m_applyToFlaggedMessages(false)
{
}

nsMsgRetentionSettings::~nsMsgRetentionSettings()
{
}

/* attribute unsigned long retainByPreference */

NS_IMETHODIMP nsMsgRetentionSettings::GetRetainByPreference(nsMsgRetainByPreference *retainByPreference)
{
  NS_ENSURE_ARG_POINTER(retainByPreference);
  *retainByPreference = m_retainByPreference;
  return NS_OK;
}

NS_IMETHODIMP nsMsgRetentionSettings::SetRetainByPreference(nsMsgRetainByPreference retainByPreference)
{
  m_retainByPreference = retainByPreference;
  return NS_OK;
}

/* attribute long daysToKeepHdrs; */
NS_IMETHODIMP nsMsgRetentionSettings::GetDaysToKeepHdrs(uint32_t *aDaysToKeepHdrs)
{
  NS_ENSURE_ARG_POINTER(aDaysToKeepHdrs);
  *aDaysToKeepHdrs = m_daysToKeepHdrs;
  return NS_OK;
}

NS_IMETHODIMP nsMsgRetentionSettings::SetDaysToKeepHdrs(uint32_t aDaysToKeepHdrs)
{
  m_daysToKeepHdrs = aDaysToKeepHdrs;
  return NS_OK;
}

/* attribute long numHeadersToKeep; */
NS_IMETHODIMP nsMsgRetentionSettings::GetNumHeadersToKeep(uint32_t *aNumHeadersToKeep)
{
  NS_ENSURE_ARG_POINTER(aNumHeadersToKeep);
  *aNumHeadersToKeep = m_numHeadersToKeep;
  return NS_OK;
}
NS_IMETHODIMP nsMsgRetentionSettings::SetNumHeadersToKeep(uint32_t aNumHeadersToKeep)
{
  m_numHeadersToKeep = aNumHeadersToKeep;
  return NS_OK;
}
/* attribute boolean useServerDefaults; */
NS_IMETHODIMP nsMsgRetentionSettings::GetUseServerDefaults(bool *aUseServerDefaults)
{
  NS_ENSURE_ARG_POINTER(aUseServerDefaults);
  *aUseServerDefaults = m_useServerDefaults;
  return NS_OK;
}
NS_IMETHODIMP nsMsgRetentionSettings::SetUseServerDefaults(bool aUseServerDefaults)
{
  m_useServerDefaults = aUseServerDefaults;
  return NS_OK;
}

/* attribute boolean cleanupBodiesByDays; */
NS_IMETHODIMP nsMsgRetentionSettings::GetCleanupBodiesByDays(bool *aCleanupBodiesByDays)
{
  NS_ENSURE_ARG_POINTER(aCleanupBodiesByDays);
  *aCleanupBodiesByDays = m_cleanupBodiesByDays;
  return NS_OK;
}
NS_IMETHODIMP nsMsgRetentionSettings::SetCleanupBodiesByDays(bool aCleanupBodiesByDays)
{
  m_cleanupBodiesByDays = aCleanupBodiesByDays;
  return NS_OK;
}

/* attribute long daysToKeepBodies; */
NS_IMETHODIMP nsMsgRetentionSettings::GetDaysToKeepBodies(uint32_t *aDaysToKeepBodies)
{
  NS_ENSURE_ARG_POINTER(aDaysToKeepBodies);
  *aDaysToKeepBodies = m_daysToKeepBodies;
  return NS_OK;
}
NS_IMETHODIMP nsMsgRetentionSettings::SetDaysToKeepBodies(uint32_t aDaysToKeepBodies)
{
  m_daysToKeepBodies = aDaysToKeepBodies;
  return NS_OK;
}

/* attribute boolean applyToFlaggedMessages; */
NS_IMETHODIMP nsMsgRetentionSettings::GetApplyToFlaggedMessages(bool *aApplyToFlaggedMessages)
{
  NS_ENSURE_ARG_POINTER(aApplyToFlaggedMessages);
  *aApplyToFlaggedMessages = m_applyToFlaggedMessages;
  return NS_OK;
}
NS_IMETHODIMP nsMsgRetentionSettings::SetApplyToFlaggedMessages(bool aApplyToFlaggedMessages)
{
  m_applyToFlaggedMessages = aApplyToFlaggedMessages;
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsMsgDownloadSettings, nsIMsgDownloadSettings)

nsMsgDownloadSettings::nsMsgDownloadSettings()
{
  m_useServerDefaults = false;
  m_downloadUnreadOnly = false;
  m_downloadByDate = false;
  m_ageLimitOfMsgsToDownload = 0;
}

nsMsgDownloadSettings::~nsMsgDownloadSettings()
{
}

/* attribute boolean useServerDefaults; */
NS_IMETHODIMP nsMsgDownloadSettings::GetUseServerDefaults(bool *aUseServerDefaults)
{
  NS_ENSURE_ARG_POINTER(aUseServerDefaults);
  *aUseServerDefaults = m_useServerDefaults;
  return NS_OK;
}
NS_IMETHODIMP nsMsgDownloadSettings::SetUseServerDefaults(bool aUseServerDefaults)
{
  m_useServerDefaults = aUseServerDefaults;
  return NS_OK;
}


/* attribute boolean downloadUnreadOnly; */
NS_IMETHODIMP nsMsgDownloadSettings::GetDownloadUnreadOnly(bool *aDownloadUnreadOnly)
{
  NS_ENSURE_ARG_POINTER(aDownloadUnreadOnly);
  *aDownloadUnreadOnly = m_downloadUnreadOnly;
  return NS_OK;
}
NS_IMETHODIMP nsMsgDownloadSettings::SetDownloadUnreadOnly(bool aDownloadUnreadOnly)
{
  m_downloadUnreadOnly = aDownloadUnreadOnly;
  return NS_OK;
}

/* attribute boolean downloadByDate; */
NS_IMETHODIMP nsMsgDownloadSettings::GetDownloadByDate(bool *aDownloadByDate)
{
  NS_ENSURE_ARG_POINTER(aDownloadByDate);
  *aDownloadByDate = m_downloadByDate;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDownloadSettings::SetDownloadByDate(bool aDownloadByDate)
{
  m_downloadByDate = aDownloadByDate;
  return NS_OK;
}


/* attribute long ageLimitOfMsgsToDownload; */
NS_IMETHODIMP nsMsgDownloadSettings::GetAgeLimitOfMsgsToDownload(uint32_t *ageLimitOfMsgsToDownload)
{
  NS_ENSURE_ARG_POINTER(ageLimitOfMsgsToDownload);
  *ageLimitOfMsgsToDownload = m_ageLimitOfMsgsToDownload;
  return NS_OK;
}
NS_IMETHODIMP nsMsgDownloadSettings::SetAgeLimitOfMsgsToDownload(uint32_t ageLimitOfMsgsToDownload)
{
  m_ageLimitOfMsgsToDownload = ageLimitOfMsgsToDownload;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetDefaultViewFlags(nsMsgViewFlagsTypeValue *aDefaultViewFlags)
{
  NS_ENSURE_ARG_POINTER(aDefaultViewFlags);
  GetIntPref("mailnews.default_view_flags", aDefaultViewFlags);
  if (*aDefaultViewFlags < nsMsgViewFlagsType::kNone ||
      *aDefaultViewFlags > (nsMsgViewFlagsType::kThreadedDisplay |
                            nsMsgViewFlagsType::kShowIgnored |
                            nsMsgViewFlagsType::kUnreadOnly |
                            nsMsgViewFlagsType::kExpandAll |
                            nsMsgViewFlagsType::kGroupBySort))
    *aDefaultViewFlags = nsMsgViewFlagsType::kNone;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetDefaultSortType(nsMsgViewSortTypeValue *aDefaultSortType)
{
  NS_ENSURE_ARG_POINTER(aDefaultSortType);
  GetIntPref("mailnews.default_sort_type", aDefaultSortType);
  if (*aDefaultSortType < nsMsgViewSortType::byDate ||
      *aDefaultSortType > nsMsgViewSortType::byAccount)
    *aDefaultSortType = nsMsgViewSortType::byDate;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::GetDefaultSortOrder(nsMsgViewSortOrderValue *aDefaultSortOrder)
{
  NS_ENSURE_ARG_POINTER(aDefaultSortOrder);
  GetIntPref("mailnews.default_sort_order", aDefaultSortOrder);
  if (*aDefaultSortOrder != nsMsgViewSortOrder::descending)
    *aDefaultSortOrder = nsMsgViewSortOrder::ascending;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::ResetHdrCacheSize(uint32_t aSize)
{
  if (m_cacheSize > aSize)
  {
    m_cacheSize = aSize;
    ClearHdrCache(false);
  }
  return NS_OK;
}

/**
  void getNewList(out unsigned long count, [array, size_is(count)] out long newKeys);
 */
NS_IMETHODIMP
nsMsgDatabase::GetNewList(uint32_t *aCount, nsMsgKey **aNewKeys)
{
    NS_ENSURE_ARG_POINTER(aCount);
    NS_ENSURE_ARG_POINTER(aNewKeys);

    *aCount = m_newSet.Length();
    if (*aCount > 0)
    {
      *aNewKeys = static_cast<nsMsgKey *>(moz_xmalloc(*aCount * sizeof(nsMsgKey)));
      if (!*aNewKeys)
        return NS_ERROR_OUT_OF_MEMORY;
      memcpy(*aNewKeys, m_newSet.Elements(), *aCount * sizeof(nsMsgKey));
      return NS_OK;
    }
    // if there were no new messages, signal this by returning a null pointer
    //
    *aNewKeys = nullptr;
    return NS_OK;
}

nsresult nsMsgDatabase::GetSearchResultsTable(const char *searchFolderUri, bool createIfMissing, nsIMdbTable **table)
{
  mdb_kind kindToken;
  mdb_count numTables;
  mdb_bool mustBeUnique;
  NS_ENSURE_TRUE(m_mdbStore, NS_ERROR_NULL_POINTER);

  nsresult err = m_mdbStore->StringToToken(GetEnv(), searchFolderUri, &kindToken);
  err = m_mdbStore->GetTableKind(GetEnv(), m_hdrRowScopeToken,  kindToken,
                                  &numTables, &mustBeUnique, table);
  if ((!*table || NS_FAILED(err)) && createIfMissing)
    err = m_mdbStore->NewTable(GetEnv(), m_hdrRowScopeToken, kindToken, true, nullptr, table);

  return *table ? err : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsMsgDatabase::GetCachedHits(const char *aSearchFolderUri, nsISimpleEnumerator **aEnumerator)
{
  nsCOMPtr <nsIMdbTable> table;
  (void) GetSearchResultsTable(aSearchFolderUri, false, getter_AddRefs(table));
  if (!table)
    return NS_ERROR_FAILURE; // expected result for no cached hits
  nsMsgDBEnumerator* e = new nsMsgDBEnumerator(this, table, nullptr, nullptr);
  if (e == nullptr)
      return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*aEnumerator = e);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDatabase::RefreshCache(const char *aSearchFolderUri, uint32_t aNumKeys, nsMsgKey *aNewHits, uint32_t *aNumBadHits, nsMsgKey **aStaleHits)
{
  nsCOMPtr <nsIMdbTable> table;
  nsresult err = GetSearchResultsTable(aSearchFolderUri, true, getter_AddRefs(table));
  NS_ENSURE_SUCCESS(err, err);
  // update the table so that it just contains aNewHits.
  // And, keep track of the headers in the original table but not in aNewHits, so we
  // can put those in aStaleHits.
  // both aNewHits and the db table are sorted by uid/key.
  // So, start at the beginning of the table and the aNewHits array.
  uint32_t newHitIndex = 0;
  uint32_t tableRowIndex = 0;

  uint32_t rowCount;
  table->GetCount(GetEnv(), &rowCount);
  nsTArray<nsMsgKey> staleHits;
  // should assert that each array is sorted
  while (newHitIndex < aNumKeys || tableRowIndex < rowCount)
  {
    mdbOid oid;
    nsMsgKey tableRowKey = nsMsgKey_None;
    if (tableRowIndex < rowCount)
    {
      nsresult ret = table->PosToOid (GetEnv(), tableRowIndex, &oid);
      if (NS_FAILED(ret))
      {
        tableRowIndex++;
        continue;
      }
      tableRowKey = oid.mOid_Id;  // ### TODO need the real key for the 0th key problem.
    }

    if (newHitIndex < aNumKeys && aNewHits[newHitIndex] == tableRowKey)
    {
      newHitIndex++;
      tableRowIndex++;
      continue;
    }
    else if (tableRowIndex >= rowCount || (newHitIndex < aNumKeys && aNewHits[newHitIndex] < tableRowKey))
    {
      nsCOMPtr <nsIMdbRow> hdrRow;
      mdbOid rowObjectId;

      rowObjectId.mOid_Id = aNewHits[newHitIndex];
      rowObjectId.mOid_Scope = m_hdrRowScopeToken;
      err = m_mdbStore->GetRow(GetEnv(), &rowObjectId, getter_AddRefs(hdrRow));
      if (hdrRow)
      {
        table->AddRow(GetEnv(), hdrRow);
        mdb_pos newPos;
        table->MoveRow(GetEnv(), hdrRow, rowCount, tableRowIndex, &newPos);
        rowCount++;
        tableRowIndex++;
      }
      newHitIndex++;
      continue;
    }
    else if (newHitIndex >= aNumKeys || aNewHits[newHitIndex] > tableRowKey)
    {
      staleHits.AppendElement(tableRowKey);
      table->CutOid(GetEnv(), &oid);
      rowCount--;
      continue; // don't increment tableRowIndex since we removed that row.
    }
   }
   *aNumBadHits = staleHits.Length();
   if (*aNumBadHits)
   {
     *aStaleHits = static_cast<nsMsgKey *>(moz_xmalloc(*aNumBadHits * sizeof(nsMsgKey)));
     if (!*aStaleHits)
       return NS_ERROR_OUT_OF_MEMORY;
     memcpy(*aStaleHits, staleHits.Elements(), *aNumBadHits * sizeof(nsMsgKey));
   }
   else
     *aStaleHits = nullptr;

#ifdef DEBUG_David_Bienvenu
  printf("after refreshing cache\n");
  // iterate over table and assert that it's in id order
  table->GetCount(GetEnv(), &rowCount);
  mdbOid oid;
  tableRowIndex = 0;
  mdb_id prevId = 0;
  while (tableRowIndex < rowCount)
  {
    nsresult ret = table->PosToOid (m_mdbEnv, tableRowIndex++, &oid);
    if (tableRowIndex > 1 && oid.mOid_Id <= prevId)
    {
      NS_ASSERTION(false, "inserting row into cached hits table, not sorted correctly");
      printf("key %lx is before or equal %lx\n", prevId, oid.mOid_Id);
    }
    prevId = oid.mOid_Id;
  }

#endif
  Commit(nsMsgDBCommitType::kLargeCommit);
  return NS_OK;
}

// search sorted table
mdb_pos nsMsgDatabase::FindInsertIndexInSortedTable(nsIMdbTable *table, mdb_id idToInsert)
{
  mdb_pos searchPos = 0;
  uint32_t rowCount;
  table->GetCount(GetEnv(), &rowCount);
  mdb_pos hi = rowCount;
  mdb_pos lo = 0;

  while (hi > lo)
  {
    mdbOid outOid;
    searchPos = (lo + hi - 1) / 2;
    table->PosToOid(GetEnv(), searchPos, &outOid);
    if (outOid.mOid_Id == idToInsert)
    {
      NS_ASSERTION(false, "id shouldn't be in table");
      return hi;
    }
    if (outOid.mOid_Id > idToInsert)
      hi = searchPos;
    else // if (outOid.mOid_Id <  idToInsert)
      lo = searchPos + 1;
  }
  return hi;
}
NS_IMETHODIMP
nsMsgDatabase::UpdateHdrInCache(const char *aSearchFolderUri, nsIMsgDBHdr *aHdr, bool aAdd)
{
  nsCOMPtr <nsIMdbTable> table;
  nsresult err = GetSearchResultsTable(aSearchFolderUri, true, getter_AddRefs(table));
  NS_ENSURE_SUCCESS(err, err);
  nsMsgKey key;
  err = aHdr->GetMessageKey(&key);
  nsMsgHdr* msgHdr = static_cast<nsMsgHdr*>(aHdr);  // closed system, so this is ok
  nsIMdbRow* hdrRow = msgHdr->GetMDBRow();
  if (NS_SUCCEEDED(err) && m_mdbStore && hdrRow)
  {
    if (!aAdd)
    {
      table->CutRow(m_mdbEnv, hdrRow);
    }
    else
    {
      mdbOid rowId;
      hdrRow->GetOid(m_mdbEnv, &rowId);
      mdb_pos insertPos = FindInsertIndexInSortedTable(table, rowId.mOid_Id);
      uint32_t rowCount;
      table->GetCount(m_mdbEnv, &rowCount);
      table->AddRow(m_mdbEnv, hdrRow);
      mdb_pos newPos;
      table->MoveRow(m_mdbEnv, hdrRow, rowCount, insertPos, &newPos);
    }
  }

//  if (aAdd)
 // if we need to add this hdr, we need to insert it in key order.
  return NS_OK;
}
NS_IMETHODIMP
nsMsgDatabase::HdrIsInCache(const char* aSearchFolderUri, nsIMsgDBHdr *aHdr, bool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  nsCOMPtr <nsIMdbTable> table;
  nsresult err = GetSearchResultsTable(aSearchFolderUri, true, getter_AddRefs(table));
  NS_ENSURE_SUCCESS(err, err);
  nsMsgKey key;
  aHdr->GetMessageKey(&key);
  mdbOid rowObjectId;
  rowObjectId.mOid_Id = key;
  rowObjectId.mOid_Scope = m_hdrRowScopeToken;
  mdb_bool hasOid;
  err =  table->HasOid(GetEnv(), &rowObjectId, &hasOid);
  *aResult = hasOid;
  return err;
}

