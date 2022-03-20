/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "msgCore.h"    // precompiled header...
#include "MailNewsTypes.h"
#include "nntpCore.h"
#include "nsNetUtil.h"
#include "nsIMsgMailNewsUrl.h"
#include "nsIMsgHdr.h"
#include "nsNNTPProtocol.h"
#include "nsINNTPArticleList.h"
#include "nsIOutputStream.h"
#include "nsIMemory.h"
#include "nsIPipe.h"
#include "nsCOMPtr.h"
#include "nsMsgI18N.h"
#include "nsINNTPNewsgroupPost.h"
#include "nsMsgBaseCID.h"
#include "nsMsgNewsCID.h"

#include "nsINntpUrl.h"
#include "prmem.h"
#include "prtime.h"
#include "mozilla/Logging.h"
#include "prerror.h"
#include "nsStringGlue.h"
#include "mozilla/Attributes.h"
#include "mozilla/Services.h"
#include "mozilla/mailnews/MimeHeaderParser.h"

#include "prprf.h"

/* include event sink interfaces for news */

#include "nsIMsgSearchSession.h"
#include "nsIMsgSearchAdapter.h"
#include "nsIMsgStatusFeedback.h"

#include "nsMsgKeySet.h"

#include "nsNewsUtils.h"
#include "nsMsgUtils.h"

#include "nsIMsgIdentity.h"
#include "nsIMsgAccountManager.h"

#include "nsIPrompt.h"
#include "nsIMsgStatusFeedback.h"

#include "nsIMsgFolder.h"
#include "nsIMsgNewsFolder.h"
#include "nsIDocShell.h"

#include "nsIMsgFilterList.h"

// for the memory cache...
#include "nsICacheEntry.h"
#include "nsICacheStorage.h"
#include "nsIApplicationCache.h"
#include "nsIStreamListener.h"
#include "nsNetCID.h"

#include "nsIPrefBranch.h"
#include "nsIPrefService.h"

#include "nsIMsgWindow.h"
#include "nsIWindowWatcher.h"

#include "nsINntpService.h"
#include "nntpCore.h"
#include "nsIStreamConverterService.h"
#include "nsIStreamListenerTee.h"
#include "nsISocketTransport.h"
#include "nsIArray.h"
#include "nsArrayUtils.h"

#include "nsIInputStreamPump.h"
#include "nsIProxyInfo.h"
#include "nsContentSecurityManager.h"

#include <time.h>

#undef GetPort  // XXX Windows!
#undef SetPort  // XXX Windows!
#undef PostMessage // avoid to collision with WinUser.h

#define PREF_NEWS_CANCEL_CONFIRM  "news.cancel.confirm"
#define PREF_NEWS_CANCEL_ALERT_ON_SUCCESS "news.cancel.alert_on_success"
#define READ_NEWS_LIST_COUNT_MAX 500 /* number of groups to process at a time when reading the list from the server */
#define READ_NEWS_LIST_TIMEOUT 50  /* uSec to wait until doing more */
#define RATE_STR_BUF_LEN 32
#define UPDATE_THRESHHOLD 25600 /* only update every 25 KB */

using namespace mozilla::mailnews;
using namespace mozilla;

// NNTP extensions are supported yet
// until the extension code is ported,
// we'll skip right to the first nntp command
// after doing "mode reader"
// and "pushed" authentication (if necessary),
//#define HAVE_NNTP_EXTENSIONS

// quiet compiler warnings by defining these function prototypes
char *MSG_UnEscapeSearchUrl (const char *commandSpecificData);

/* Logging stuff */

PRLogModuleInfo* NNTP = NULL;
#define out     LogLevel::Info

#define NNTP_LOG_READ(buf) \
if (NNTP==NULL) \
    NNTP = PR_NewLogModule("NNTP"); \
MOZ_LOG(NNTP, out, ("(%p) Receiving: %s", this, buf)) ;

#define NNTP_LOG_WRITE(buf) \
if (NNTP==NULL) \
    NNTP = PR_NewLogModule("NNTP"); \
MOZ_LOG(NNTP, out, ("(%p) Sending: %s", this, buf)) ;

#define NNTP_LOG_NOTE(buf) \
if (NNTP==NULL) \
    NNTP = PR_NewLogModule("NNTP"); \
MOZ_LOG(NNTP, out, ("(%p) %s",this, buf)) ;

const char *const stateLabels[] = {
"NNTP_RESPONSE",
#ifdef BLOCK_UNTIL_AVAILABLE_CONNECTION
"NNTP_BLOCK_UNTIL_CONNECTIONS_ARE_AVAILABLE",
"NNTP_CONNECTIONS_ARE_AVAILABLE",
#endif
"NNTP_CONNECT",
"NNTP_CONNECT_WAIT",
"NNTP_LOGIN_RESPONSE",
"NNTP_SEND_MODE_READER",
"NNTP_SEND_MODE_READER_RESPONSE",
"SEND_LIST_EXTENSIONS",
"SEND_LIST_EXTENSIONS_RESPONSE",
"SEND_LIST_SEARCHES",
"SEND_LIST_SEARCHES_RESPONSE",
"NNTP_LIST_SEARCH_HEADERS",
"NNTP_LIST_SEARCH_HEADERS_RESPONSE",
"NNTP_GET_PROPERTIES",
"NNTP_GET_PROPERTIES_RESPONSE",
"SEND_LIST_SUBSCRIPTIONS",
"SEND_LIST_SUBSCRIPTIONS_RESPONSE",
"SEND_FIRST_NNTP_COMMAND",
"SEND_FIRST_NNTP_COMMAND_RESPONSE",
"SETUP_NEWS_STREAM",
"NNTP_BEGIN_AUTHORIZE",
"NNTP_AUTHORIZE_RESPONSE",
"NNTP_PASSWORD_RESPONSE",
"NNTP_READ_LIST_BEGIN",
"NNTP_READ_LIST",
"DISPLAY_NEWSGROUPS",
"NNTP_NEWGROUPS_BEGIN",
"NNTP_NEWGROUPS",
"NNTP_BEGIN_ARTICLE",
"NNTP_READ_ARTICLE",
"NNTP_XOVER_BEGIN",
"NNTP_FIGURE_NEXT_CHUNK",
"NNTP_XOVER_SEND",
"NNTP_XOVER_RESPONSE",
"NNTP_XOVER",
"NEWS_PROCESS_XOVER",
"NNTP_XHDR_SEND",
"NNTP_XHDR_RESPONSE",
"NNTP_READ_GROUP",
"NNTP_READ_GROUP_RESPONSE",
"NNTP_READ_GROUP_BODY",
"NNTP_SEND_GROUP_FOR_ARTICLE",
"NNTP_SEND_GROUP_FOR_ARTICLE_RESPONSE",
"NNTP_SEND_ARTICLE_NUMBER",
"NEWS_PROCESS_BODIES",
"NNTP_PRINT_ARTICLE_HEADERS",
"NNTP_SEND_POST_DATA",
"NNTP_SEND_POST_DATA_RESPONSE",
"NNTP_CHECK_FOR_MESSAGE",
"NEWS_START_CANCEL",
"NEWS_DO_CANCEL",
"NNTP_XPAT_SEND",
"NNTP_XPAT_RESPONSE",
"NNTP_SEARCH",
"NNTP_SEARCH_RESPONSE",
"NNTP_SEARCH_RESULTS",
"NNTP_LIST_PRETTY_NAMES",
"NNTP_LIST_PRETTY_NAMES_RESPONSE",
"NNTP_LIST_XACTIVE_RESPONSE",
"NNTP_LIST_XACTIVE",
"NNTP_LIST_GROUP",
"NNTP_LIST_GROUP_RESPONSE",
"NEWS_DONE",
"NEWS_POST_DONE",
"NEWS_ERROR",
"NNTP_ERROR",
"NEWS_FREE",
"NNTP_SUSPENDED"
};


/* end logging */

/* Forward declarations */

#define LIST_WANTED     0
#define ARTICLE_WANTED  1
#define CANCEL_WANTED   2
#define GROUP_WANTED    3
#define NEWS_POST       4
#define NEW_GROUPS      5
#define SEARCH_WANTED   6
#define IDS_WANTED      7

/* the output_buffer_size must be larger than the largest possible line
 * 2000 seems good for news
 *
 * jwz: I increased this to 4k since it must be big enough to hold the
 * entire button-bar HTML, and with the new "mailto" format, that can
 * contain arbitrarily long header fields like "references".
 *
 * fortezza: proxy auth is huge, buffer increased to 8k (sigh).
 */
#define OUTPUT_BUFFER_SIZE (4096*2)

/* the amount of time to subtract from the machine time
 * for the newgroup command sent to the nntp server
 */
#define NEWGROUPS_TIME_OFFSET 60L*60L*12L   /* 12 hours */

////////////////////////////////////////////////////////////////////////////////////////////
// TEMPORARY HARD CODED FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////////////
#define NET_IS_SPACE(x) ((x)==' ' || (x)=='\t')

// turn "\xx" (with xx being hex numbers) in string into chars
char *MSG_UnEscapeSearchUrl (const char *commandSpecificData)
{
  nsAutoCString result(commandSpecificData);
  int32_t slashpos = 0;
  while (slashpos = result.FindChar('\\', slashpos),
         slashpos != kNotFound)
  {
    nsAutoCString hex;
    hex.Assign(Substring(result, slashpos + 1, 2));
    int32_t ch;
    nsresult rv;
    ch = hex.ToInteger(&rv, 16);
    result.Replace(slashpos, 3, NS_SUCCEEDED(rv) && ch != 0 ? (char) ch : 'X');
    slashpos++;
  }
  return ToNewCString(result);
}

////////////////////////////////////////////////////////////////////////////////////////////
// END OF TEMPORARY HARD CODED FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS_INHERITED(nsNNTPProtocol, nsMsgProtocol, nsINNTPProtocol,
  nsITimerCallback, nsICacheEntryOpenCallback, nsIMsgAsyncPromptListener)

nsNNTPProtocol::nsNNTPProtocol(nsINntpIncomingServer *aServer, nsIURI *aURL,
                               nsIMsgWindow *aMsgWindow)
: nsMsgProtocol(aURL),
  m_connectionBusy(false),
  m_nntpServer(aServer)
{
  if (!NNTP)
    NNTP = PR_NewLogModule("NNTP");

  m_ProxyServer = nullptr;
  m_responseText = nullptr;
  m_dataBuf = nullptr;

  m_key = nsMsgKey_None;

  mBytesReceived = 0;
  mBytesReceivedSinceLastStatusUpdate = 0;
  m_startTime = PR_Now();

  if (aMsgWindow) {
    m_msgWindow = aMsgWindow;
  }

  m_runningURL = nullptr;
  m_fromCache = false;
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) creating",this));
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) initializing, so unset m_currentGroup",this));
  m_currentGroup.Truncate();
  m_lastActiveTimeStamp = 0;
}

nsNNTPProtocol::~nsNNTPProtocol()
{
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) destroying",this));
  if (m_nntpServer) {
    m_nntpServer->WriteNewsrcFile();
    m_nntpServer->RemoveConnection(this);
  }
  if (mUpdateTimer) {
    mUpdateTimer->Cancel();
    mUpdateTimer = nullptr;
  }
  Cleanup();
}

void nsNNTPProtocol::Cleanup()  //free char* member variables
{
  PR_FREEIF(m_responseText);
  PR_FREEIF(m_dataBuf);
}

NS_IMETHODIMP nsNNTPProtocol::Initialize(nsIURI *aURL, nsIMsgWindow *aMsgWindow)
{
  if (aMsgWindow) {
    m_msgWindow = aMsgWindow;
  }
  nsMsgProtocol::InitFromURI(aURL);

  nsCOMPtr<nsIMsgIncomingServer> server = do_QueryInterface(m_nntpServer);
  NS_ASSERTION(m_nntpServer, "nsNNTPProtocol need an m_nntpServer.");
  NS_ENSURE_TRUE(m_nntpServer, NS_ERROR_UNEXPECTED);

  nsresult rv = m_nntpServer->GetMaxArticles(&m_maxArticles);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t socketType;
  rv = server->GetSocketType(&socketType);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t port = 0;
  rv = m_url->GetPort(&port);
  if (NS_FAILED(rv) || (port<=0)) {
    rv = server->GetPort(&port);
    NS_ENSURE_SUCCESS(rv, rv);

    if (port<=0) {
      port = (socketType == nsMsgSocketType::SSL) ?
             nsINntpUrl::DEFAULT_NNTPS_PORT : nsINntpUrl::DEFAULT_NNTP_PORT;
    }

    rv = m_url->SetPort(port);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_PRECONDITION(m_url, "invalid URL passed into NNTP Protocol");

  m_runningURL = do_QueryInterface(m_url, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  SetIsBusy(true);

  nsCString group;

  // Initialize m_newsAction before possible use in ParseURL method
  m_runningURL->GetNewsAction(&m_newsAction);

  // parse url to get the msg folder and check if the message is in the folder's
  // local cache before opening a new socket and trying to download the message
  rv = ParseURL(m_url, group, m_messageID);

  if (NS_SUCCEEDED(rv) && m_runningURL)
  {
    nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(m_runningURL);
    if (mailnewsUrl)
    {
      if (aMsgWindow)
        mailnewsUrl->SetMsgWindow(aMsgWindow);

      if (m_newsAction == nsINntpUrl::ActionFetchArticle || m_newsAction == nsINntpUrl::ActionFetchPart
          || m_newsAction == nsINntpUrl::ActionSaveMessageToDisk)
      {
        // Look for the content length
        nsCOMPtr<nsIMsgMessageUrl> msgUrl(do_QueryInterface(m_runningURL));
        if (msgUrl)
        {
          nsCOMPtr<nsIMsgDBHdr> msgHdr;
          msgUrl->GetMessageHeader(getter_AddRefs(msgHdr));
          if (msgHdr)
          {
            // Note that for attachments, the messageSize is going to be the
            // size of the entire message
            uint32_t messageSize;
            msgHdr->GetMessageSize(&messageSize);
            SetContentLength(messageSize);
          }
        }

        bool msgIsInLocalCache = false;
        mailnewsUrl->GetMsgIsInLocalCache(&msgIsInLocalCache);
        if (msgIsInLocalCache || WeAreOffline())
          return NS_OK; // probably don't need to do anything else - definitely don't want
        // to open the socket.
      }
    }
  }
  else {
    return rv;
  }

  if (!m_socketIsOpen)
  {
    // When we are making a secure connection, we need to make sure that we
    // pass an interface requestor down to the socket transport so that PSM can
    // retrieve a nsIPrompt instance if needed.
    nsCOMPtr<nsIInterfaceRequestor> ir;
    if (socketType != nsMsgSocketType::plain && aMsgWindow)
    {
      nsCOMPtr<nsIDocShell> docShell;
      aMsgWindow->GetRootDocShell(getter_AddRefs(docShell));
      ir = do_QueryInterface(docShell);
    }

    // call base class to set up the transport

    int32_t port = 0;
    nsCString hostName;
    m_url->GetPort(&port);

    rv = server->GetRealHostName(hostName);
    NS_ENSURE_SUCCESS(rv, rv);

    MOZ_LOG(NNTP,  LogLevel::Info, ("(%p) opening connection to %s on port %d",
      this, hostName.get(), port));

    nsCOMPtr<nsIProxyInfo> proxyInfo;
    rv = MsgExamineForProxy(this, getter_AddRefs(proxyInfo));
    if (NS_FAILED(rv)) proxyInfo = nullptr;

    rv = OpenNetworkSocketWithInfo(hostName.get(), port,
           (socketType == nsMsgSocketType::SSL) ? "ssl" : nullptr,
           proxyInfo, ir);

    NS_ENSURE_SUCCESS(rv,rv);
    m_nextState = NNTP_LOGIN_RESPONSE;
  }
  else {
    m_nextState = SEND_FIRST_NNTP_COMMAND;
  }
  m_dataBuf = (char *) PR_Malloc(sizeof(char) * OUTPUT_BUFFER_SIZE);
  m_dataBufSize = OUTPUT_BUFFER_SIZE;

  if (!m_lineStreamBuffer)
    m_lineStreamBuffer = new nsMsgLineStreamBuffer(OUTPUT_BUFFER_SIZE, true /* create new lines */);

  m_typeWanted = 0;
  m_responseCode = 0;
  m_previousResponseCode = 0;
  m_responseText = nullptr;

  m_firstArticle = 0;
  m_lastArticle = 0;
  m_firstPossibleArticle = 0;
  m_lastPossibleArticle = 0;
  m_numArticlesLoaded = 0;
  m_numArticlesWanted = 0;

  m_key = nsMsgKey_None;

  m_articleNumber = 0;
  m_originalContentLength = 0;
  m_cancelID.Truncate();
  m_cancelFromHdr.Truncate();
  m_cancelNewsgroups.Truncate();
  m_cancelDistribution.Truncate();
  return NS_OK;
}

NS_IMETHODIMP nsNNTPProtocol::GetIsBusy(bool *aIsBusy)
{
  NS_ENSURE_ARG_POINTER(aIsBusy);
  *aIsBusy = m_connectionBusy;
  return NS_OK;
}

NS_IMETHODIMP nsNNTPProtocol::SetIsBusy(bool aIsBusy)
{
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) setting busy to %d",this, aIsBusy));
  m_connectionBusy = aIsBusy;
  
  // Maybe we could load another URI.
  if (!aIsBusy && m_nntpServer)
    m_nntpServer->PrepareForNextUrl(this);

  return NS_OK;
}

NS_IMETHODIMP nsNNTPProtocol::GetIsCachedConnection(bool *aIsCachedConnection)
{
  NS_ENSURE_ARG_POINTER(aIsCachedConnection);
  *aIsCachedConnection = m_fromCache;
  return NS_OK;
}

NS_IMETHODIMP nsNNTPProtocol::SetIsCachedConnection(bool aIsCachedConnection)
{
  m_fromCache = aIsCachedConnection;
  return NS_OK;
}

/* void GetLastActiveTimeStamp (out PRTime aTimeStamp); */
NS_IMETHODIMP nsNNTPProtocol::GetLastActiveTimeStamp(PRTime *aTimeStamp)
{
  NS_ENSURE_ARG_POINTER(aTimeStamp);
  *aTimeStamp = m_lastActiveTimeStamp;
  return NS_OK;
}

NS_IMETHODIMP nsNNTPProtocol::LoadNewsUrl(nsIURI * aURL, nsISupports * aConsumer)
{
  // clear the previous channel listener and use the new one....
  // don't reuse an existing channel listener
  m_channelListener = nullptr;
  m_channelListener = do_QueryInterface(aConsumer);
  nsCOMPtr<nsINntpUrl> newsUrl (do_QueryInterface(aURL));
  newsUrl->GetNewsAction(&m_newsAction);

  SetupPartExtractorListener(m_channelListener);
  return LoadUrl(aURL, aConsumer);
}


// WARNING: the cache stream listener is intended to be accessed from the UI thread!
// it will NOT create another proxy for the stream listener that gets passed in...
class nsNntpCacheStreamListener : public nsIStreamListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  nsNntpCacheStreamListener ();

  nsresult Init(nsIStreamListener * aStreamListener, nsIChannel* channel, nsIMsgMailNewsUrl *aRunningUrl);
protected:
  virtual ~nsNntpCacheStreamListener();
    nsCOMPtr<nsIChannel> mChannelToUse;
  nsCOMPtr<nsIStreamListener> mListener;
  nsCOMPtr<nsIMsgMailNewsUrl> mRunningUrl;
};

NS_IMPL_ADDREF(nsNntpCacheStreamListener)
NS_IMPL_RELEASE(nsNntpCacheStreamListener)

NS_INTERFACE_MAP_BEGIN(nsNntpCacheStreamListener)
   NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIStreamListener)
   NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
   NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
NS_INTERFACE_MAP_END

nsNntpCacheStreamListener::nsNntpCacheStreamListener()
{
}

nsNntpCacheStreamListener::~nsNntpCacheStreamListener()
{}

nsresult nsNntpCacheStreamListener::Init(nsIStreamListener * aStreamListener, nsIChannel* channel,
                                         nsIMsgMailNewsUrl *aRunningUrl)
{
  NS_ENSURE_ARG(aStreamListener);
  NS_ENSURE_ARG(channel);

  mChannelToUse = channel;

  mListener = aStreamListener;
  mRunningUrl = aRunningUrl;
  return NS_OK;
}

NS_IMETHODIMP
nsNntpCacheStreamListener::OnStartRequest(nsIRequest *request, nsISupports * aCtxt)
{
  nsCOMPtr <nsILoadGroup> loadGroup;
  nsCOMPtr <nsIRequest> ourRequest = do_QueryInterface(mChannelToUse);

  NS_ASSERTION(mChannelToUse, "null channel in OnStartRequest");
  if (mChannelToUse)
    mChannelToUse->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup)
    loadGroup->AddRequest(ourRequest, nullptr /* context isupports */);
  return (mListener) ? mListener->OnStartRequest(ourRequest, aCtxt) : NS_OK;
}

NS_IMETHODIMP
nsNntpCacheStreamListener::OnStopRequest(nsIRequest *request, nsISupports * aCtxt, nsresult aStatus)
{
  nsCOMPtr <nsIRequest> ourRequest = do_QueryInterface(mChannelToUse);
  nsresult rv = NS_OK;
  NS_ASSERTION(mListener, "this assertion is for Bug 531794 comment 7");
  if (mListener)
    mListener->OnStopRequest(ourRequest, aCtxt, aStatus);
  nsCOMPtr <nsILoadGroup> loadGroup;
  NS_ASSERTION(mChannelToUse, "null channel in OnStopRequest");
  if (mChannelToUse)
    mChannelToUse->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup)
    loadGroup->RemoveRequest(ourRequest, nullptr, aStatus);

  // clear out mem cache entry so we're not holding onto it.
  if (mRunningUrl)
    mRunningUrl->SetMemCacheEntry(nullptr);

  mListener = nullptr;
  nsCOMPtr <nsINNTPProtocol> nntpProtocol = do_QueryInterface(mChannelToUse);
  if (nntpProtocol) {
    rv = nntpProtocol->SetIsBusy(false);
    NS_ENSURE_SUCCESS(rv,rv);
  }
  mChannelToUse = nullptr;
  return rv;
}

NS_IMETHODIMP
nsNntpCacheStreamListener::OnDataAvailable(nsIRequest *request, nsISupports * aCtxt, nsIInputStream * aInStream, uint64_t aSourceOffset, uint32_t aCount)
{
    NS_ENSURE_STATE(mListener);
    nsCOMPtr <nsIRequest> ourRequest = do_QueryInterface(mChannelToUse);
    return mListener->OnDataAvailable(ourRequest, aCtxt, aInStream, aSourceOffset, aCount);
}

NS_IMETHODIMP nsNNTPProtocol::GetOriginalURI(nsIURI* *aURI)
{
    // News does not seem to have the notion of an original URI (See Bug #193317)
    *aURI = m_url;
    NS_IF_ADDREF(*aURI);
    return NS_OK;
}

NS_IMETHODIMP nsNNTPProtocol::SetOriginalURI(nsIURI* aURI)
{
    // News does not seem to have the notion of an original URI (See Bug #193317)
    return NS_OK;       // ignore
}

nsresult nsNNTPProtocol::SetupPartExtractorListener(nsIStreamListener * aConsumer)
{
  bool convertData = false;
  nsresult rv = NS_OK;

  if (m_newsAction == nsINntpUrl::ActionFetchArticle)
  {
    nsCOMPtr<nsIMsgMailNewsUrl> msgUrl = do_QueryInterface(m_runningURL, &rv);
    NS_ENSURE_SUCCESS(rv,rv);

    nsAutoCString queryStr;
    rv = msgUrl->GetQuery(queryStr);
    NS_ENSURE_SUCCESS(rv,rv);

    // check if this is a filter plugin requesting the message.
    // in that case, set up a text converter
    convertData = (queryStr.Find("header=filter") != kNotFound
      || queryStr.Find("header=attach") != kNotFound);
  }
  else
  {
    convertData = (m_newsAction == nsINntpUrl::ActionFetchPart);
  }
  if (convertData)
  {
    nsCOMPtr<nsIStreamConverterService> converter = do_GetService("@mozilla.org/streamConverters;1");
    if (converter && aConsumer)
    {
      nsCOMPtr<nsIStreamListener> newConsumer;
      nsCOMPtr<nsIChannel> channel;
      QueryInterface(NS_GET_IID(nsIChannel), getter_AddRefs(channel));
      converter->AsyncConvertData("message/rfc822", "*/*",
           aConsumer, channel, getter_AddRefs(newConsumer));
      if (newConsumer)
        m_channelListener = newConsumer;
    }
  }

  return rv;
}

nsresult nsNNTPProtocol::ReadFromMemCache(nsICacheEntry *entry)
{
  NS_ENSURE_ARG(entry);

  nsCOMPtr<nsIInputStream> cacheStream;
  nsresult rv = entry->OpenInputStream(0, getter_AddRefs(cacheStream));

  if (NS_SUCCEEDED(rv))
  {
    nsCOMPtr<nsIInputStreamPump> pump;
    rv = NS_NewInputStreamPump(getter_AddRefs(pump), cacheStream);
    if (NS_FAILED(rv)) return rv;

    nsCString group;
    // do this to get m_key set, so that marking the message read will work.
    rv = ParseURL(m_url, group, m_messageID);

    RefPtr<nsNntpCacheStreamListener> cacheListener =
      new nsNntpCacheStreamListener();

    SetLoadGroup(m_loadGroup);
    m_typeWanted = ARTICLE_WANTED;

    nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(m_runningURL);
    cacheListener->Init(m_channelListener, static_cast<nsIChannel *>(this), mailnewsUrl);

    mContentType = ""; // reset the content type for the upcoming read....

    rv = pump->AsyncRead(cacheListener, m_channelContext);

    if (NS_SUCCEEDED(rv)) // ONLY if we succeeded in actually starting the read should we return
    {
      // we're not calling nsMsgProtocol::AsyncRead(), which calls nsNNTPProtocol::LoadUrl, so we need to take care of some stuff it does.
      m_channelListener = nullptr;
      return rv;
    }
  }

  return rv;
}

nsresult nsNNTPProtocol::ReadFromNewsConnection()
{
  // we might end up here if we thought we had a news message offline
  // but it turned out not to be so. In which case, we need to
  // recall Initialize().
  if (!m_socketIsOpen || !m_dataBuf)
  {
    nsresult rv = Initialize(m_url, m_msgWindow);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return nsMsgProtocol::AsyncOpen(m_channelListener, m_channelContext);
}

// for messages stored in our offline cache, we have special code to handle that...
// If it's in the local cache, we return true and we can abort the download because
// this method does the rest of the work.
bool nsNNTPProtocol::ReadFromLocalCache()
{
  bool msgIsInLocalCache = false;
  nsresult rv = NS_OK;
  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(m_runningURL);
  mailnewsUrl->GetMsgIsInLocalCache(&msgIsInLocalCache);

  if (msgIsInLocalCache)
  {
    nsCOMPtr <nsIMsgFolder> folder = do_QueryInterface(m_newsFolder);
    if (folder && NS_SUCCEEDED(rv))
    {
    // we want to create a file channel and read the msg from there.
      nsCOMPtr<nsIInputStream> fileStream;
      int64_t offset=0;
      uint32_t size=0;
      rv = folder->GetOfflineFileStream(m_key, &offset, &size, getter_AddRefs(fileStream));

      // get the file stream from the folder, somehow (through the message or
      // folder sink?) We also need to set the transfer offset to the message offset
      if (fileStream && NS_SUCCEEDED(rv))
      {
        // dougt - This may break the ablity to "cancel" a read from offline mail reading.
        // fileChannel->SetLoadGroup(m_loadGroup);

        m_typeWanted = ARTICLE_WANTED;

        RefPtr<nsNntpCacheStreamListener> cacheListener =
          new nsNntpCacheStreamListener();

        cacheListener->Init(m_channelListener, static_cast<nsIChannel *>(this), mailnewsUrl);

        // create a stream pump that will async read the specified amount of data.
        // XXX make size 64-bit int
        nsCOMPtr<nsIInputStreamPump> pump;
        rv = NS_NewInputStreamPump(getter_AddRefs(pump),
                                   fileStream, offset, (int64_t) size);
        if (NS_SUCCEEDED(rv))
          rv = pump->AsyncRead(cacheListener, m_channelContext);

        if (NS_SUCCEEDED(rv)) // ONLY if we succeeded in actually starting the read should we return
        {
          mContentType.Truncate();
          m_channelListener = nullptr;
          NNTP_LOG_NOTE("Loading message from offline storage");
          return true;
        }
      }
      else
        mailnewsUrl->SetMsgIsInLocalCache(false);
    }
  }

  return false;
}

NS_IMETHODIMP
nsNNTPProtocol::OnCacheEntryAvailable(nsICacheEntry *entry, bool aNew, nsIApplicationCache* aAppCache, nsresult status)
{
  nsresult rv = NS_OK;

  if (NS_SUCCEEDED(status))
  {
    nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(m_runningURL, &rv);
    mailnewsUrl->SetMemCacheEntry(entry);

    // Insert a "stream T" into the flow so data gets written to both.
    if (aNew)
    {
      // use a stream listener Tee to force data into the cache and to our current channel listener...
      nsCOMPtr<nsIStreamListener> newListener;
      nsCOMPtr<nsIStreamListenerTee> tee = do_CreateInstance(NS_STREAMLISTENERTEE_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIOutputStream> outStream;
      rv = entry->OpenOutputStream(0, getter_AddRefs(outStream));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = tee->Init(m_channelListener, outStream, nullptr);
      m_channelListener = do_QueryInterface(tee);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else
    {
      rv = ReadFromMemCache(entry);
      if (NS_SUCCEEDED(rv)) {
        entry->MarkValid();
        return NS_OK; // kick out if reading from the cache succeeded...
      }
    }
  } // if we got a valid entry back from the cache...

  // if reading from the cache failed or if we are writing into the cache, default to ReadFromNewsConnection.
  return ReadFromNewsConnection();
}

NS_IMETHODIMP
nsNNTPProtocol::OnCacheEntryCheck(nsICacheEntry* entry, nsIApplicationCache* appCache,
                                  uint32_t* aResult)
{
  *aResult = nsICacheEntryOpenCallback::ENTRY_WANTED;
  return NS_OK;
}

nsresult nsNNTPProtocol::OpenCacheEntry()
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(m_runningURL, &rv);
  // get the cache session from our nntp service...
  nsCOMPtr <nsINntpService> nntpService = do_GetService(NS_NNTPSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsICacheStorage> cacheStorage;
  rv = nntpService->GetCacheStorage(getter_AddRefs(cacheStorage));
  NS_ENSURE_SUCCESS(rv, rv);

  // Open a cache entry with key = url, no extension.
  nsCOMPtr<nsIURI> uri;
  rv = mailnewsUrl->GetBaseURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  // Truncate of the query part so we don't duplicate urls in the cache for
  // various message parts.
  nsCOMPtr<nsIURI> newUri;
  uri->Clone(getter_AddRefs(newUri));
  nsAutoCString path;
  newUri->GetPath(path);
  int32_t pos = path.FindChar('?');
  if (pos != kNotFound) {
    path.SetLength(pos);
    newUri->SetPath(path);
  }
  return cacheStorage->AsyncOpenURI(newUri, EmptyCString(), nsICacheStorage::OPEN_NORMALLY, this);
}

NS_IMETHODIMP nsNNTPProtocol::AsyncOpen(nsIStreamListener *listener, nsISupports *ctxt)
{
  nsresult rv;
  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(m_runningURL, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  int32_t port;
  rv = mailnewsUrl->GetPort(&port);
  if (NS_FAILED(rv))
      return rv;

  rv = NS_CheckPortSafety(port, "news");
  if (NS_FAILED(rv))
      return rv;

  m_channelContext = ctxt;
  m_channelListener = listener;
  m_runningURL->GetNewsAction(&m_newsAction);

  // Before running through the connection, try to see if we can grab the data
  // from the offline storage or the memory cache. Only actions retrieving
  // messages can be cached.
  if (mailnewsUrl && (m_newsAction == nsINntpUrl::ActionFetchArticle ||
                      m_newsAction == nsINntpUrl::ActionFetchPart ||
                      m_newsAction == nsINntpUrl::ActionSaveMessageToDisk))
  {
    SetupPartExtractorListener(m_channelListener);

    // Attempt to get the message from the offline storage cache. If this
    // succeeds, we don't need to use our connection, so tell the server that we
    // are ready for the next URL.
    if (ReadFromLocalCache())
    {
      if (m_nntpServer)
        m_nntpServer->PrepareForNextUrl(this);
      return NS_OK;
    }

    // If it wasn't offline, try to get the cache from memory. If this call
    // succeeds, we probably won't need the connection, but the cache might fail
    // later on. The code there will determine if we need to fallback and will
    // handle informing the server of our readiness.
    if (NS_SUCCEEDED(OpenCacheEntry()))
      return NS_OK;
  }
  return nsMsgProtocol::AsyncOpen(listener, ctxt);
}

NS_IMETHODIMP nsNNTPProtocol::AsyncOpen2(nsIStreamListener *aListener)
{
    nsCOMPtr<nsIStreamListener> listener = aListener;
    nsresult rv = nsContentSecurityManager::doContentSecurityCheck(this, listener);
    NS_ENSURE_SUCCESS(rv, rv);
    return AsyncOpen(listener, nullptr);
}

nsresult nsNNTPProtocol::LoadUrl(nsIURI * aURL, nsISupports * aConsumer)
{
  NS_ENSURE_ARG_POINTER(aURL);

  nsCString group;
  mContentType.Truncate();
  nsresult rv = NS_OK;

  m_runningURL = do_QueryInterface(aURL, &rv);
  if (NS_FAILED(rv)) return rv;
  m_runningURL->GetNewsAction(&m_newsAction);

  SetIsBusy(true);

  rv = ParseURL(aURL, group, m_messageID);
  NS_ASSERTION(NS_SUCCEEDED(rv),"failed to parse news url");
  //if (NS_FAILED(rv)) return rv;
  // XXX group returned from ParseURL is assumed to be in UTF-8
  NS_ASSERTION(MsgIsUTF8(group), "newsgroup name is not in UTF-8");
  NS_ASSERTION(m_nntpServer, "Parsing must result in an m_nntpServer");

  MOZ_LOG(NNTP, LogLevel::Info,("(%p) m_messageID = %s", this, m_messageID.get()));
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) group = %s", this, group.get()));
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) m_key = %d",this,m_key));

  if (m_newsAction == nsINntpUrl::ActionFetchArticle ||
      m_newsAction == nsINntpUrl::ActionFetchPart ||
      m_newsAction == nsINntpUrl::ActionSaveMessageToDisk)
    m_typeWanted = ARTICLE_WANTED;
  else if (m_newsAction == nsINntpUrl::ActionCancelArticle)
    m_typeWanted = CANCEL_WANTED;
  else if (m_newsAction == nsINntpUrl::ActionPostArticle)
  {
    m_typeWanted = NEWS_POST;
    m_messageID = "";
  }
  else if (m_newsAction == nsINntpUrl::ActionListIds)
  {
    m_typeWanted = IDS_WANTED;
    rv = m_nntpServer->FindGroup(group, getter_AddRefs(m_newsFolder));
  }
  else if (m_newsAction == nsINntpUrl::ActionSearch)
  {
    m_typeWanted = SEARCH_WANTED;

    // Get the search data
    nsCString commandSpecificData;
    nsCOMPtr<nsIURL> url = do_QueryInterface(m_runningURL);
    rv = url->GetQuery(commandSpecificData);
    NS_ENSURE_SUCCESS(rv, rv);
    MsgUnescapeString(commandSpecificData, 0, m_searchData);

    rv = m_nntpServer->FindGroup(group, getter_AddRefs(m_newsFolder));
    if (!m_newsFolder)
      goto FAIL;
  }
  else if (m_newsAction == nsINntpUrl::ActionGetNewNews)
  {
    bool containsGroup = true;
    rv = m_nntpServer->ContainsNewsgroup(group, &containsGroup);
    if (NS_FAILED(rv))
      goto FAIL;

    if (!containsGroup)
    {
      // We have the name of a newsgroup which we're not subscribed to,
      // the next step is to ask the user whether we should subscribe to it.
      nsCOMPtr<nsIPrompt> dialog;

      if (m_msgWindow)
        m_msgWindow->GetPromptDialog(getter_AddRefs(dialog));

      if (!dialog)
      {
         nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));
         wwatch->GetNewPrompter(nullptr, getter_AddRefs(dialog));
      }

      nsString statusString, confirmText;
      nsCOMPtr<nsIStringBundle> bundle;
      nsCOMPtr<nsIStringBundleService> bundleService =
        mozilla::services::GetStringBundleService();

      // to handle non-ASCII newsgroup names, we store them internally
      // as escaped. decode and unescape the newsgroup name so we'll
      // display a proper name.

      nsAutoString unescapedName;
      rv = NS_MsgDecodeUnescapeURLPath(group, unescapedName);
      NS_ENSURE_SUCCESS(rv,rv);

      bundleService->CreateBundle(NEWS_MSGS_URL, getter_AddRefs(bundle));
      const char16_t *formatStrings[1] = { unescapedName.get() };

      rv = bundle->FormatStringFromName(
        u"autoSubscribeText", formatStrings, 1,
        getter_Copies(confirmText));
      NS_ENSURE_SUCCESS(rv,rv);

      bool confirmResult = false;
      rv = dialog->Confirm(nullptr, confirmText.get(), &confirmResult);
      NS_ENSURE_SUCCESS(rv, rv);

      if (confirmResult)
      {
         rv = m_nntpServer->SubscribeToNewsgroup(group);
         containsGroup = true;
      }
      else
      {
        // XXX FIX ME
        // the way news is current written, we've already opened the socket
        // and initialized the connection.
        //
        // until that is fixed, when the user cancels an autosubscribe, we've got to close it and clean up after ourselves
        //
        // see bug http://bugzilla.mozilla.org/show_bug.cgi?id=108293
        // another problem, autosubscribe urls are ending up as cache entries
        // because the default action on nntp urls is ActionFetchArticle
        //
        // see bug http://bugzilla.mozilla.org/show_bug.cgi?id=108294
        if (m_runningURL)
          FinishMemCacheEntry(false); // cleanup mem cache entry

        return CloseConnection();
      }
    }

    // If we have a group (since before, or just subscribed), set the m_newsFolder.
    if (containsGroup)
    {
      rv = m_nntpServer->FindGroup(group, getter_AddRefs(m_newsFolder));
      if (!m_newsFolder)
        goto FAIL;
    }
    m_typeWanted = GROUP_WANTED;
  }
  else if (m_newsAction == nsINntpUrl::ActionListGroups)
    m_typeWanted = LIST_WANTED;
  else if (m_newsAction == nsINntpUrl::ActionListNewGroups)
    m_typeWanted = NEW_GROUPS;
  else if (!m_messageID.IsEmpty() || m_key != nsMsgKey_None)
    m_typeWanted = ARTICLE_WANTED;
  else
  {
    NS_NOTREACHED("Unknown news action");
    rv = NS_ERROR_FAILURE;
  }

    // if this connection comes from the cache, we need to initialize the
    // load group here, by generating the start request notification. nsMsgProtocol::OnStartRequest
    // ignores the first parameter (which is supposed to be the channel) so we'll pass in null.
    if (m_fromCache)
      nsMsgProtocol::OnStartRequest(nullptr, aURL);

      /* At this point, we're all done parsing the URL, and know exactly
      what we want to do with it.
    */

FAIL:
    if (NS_FAILED(rv))
    {
      AlertError(0, nullptr);
      return rv;
    }
    else
    {
      if (!m_socketIsOpen)
      {
        m_nextStateAfterResponse = m_nextState;
        m_nextState = NNTP_RESPONSE;
      }
      rv = nsMsgProtocol::LoadUrl(aURL, aConsumer);
    }

    // Make sure that we have the information we need to be able to run the
    // URLs
    NS_ASSERTION(m_nntpServer, "Parsing must result in an m_nntpServer");
    if (m_typeWanted == ARTICLE_WANTED)
    {
      if (m_key != nsMsgKey_None)
        NS_ASSERTION(m_newsFolder, "ARTICLE_WANTED needs m_newsFolder w/ key");
      else
        NS_ASSERTION(!m_messageID.IsEmpty(), "ARTICLE_WANTED needs m_messageID w/o key");
    }
    else if (m_typeWanted == CANCEL_WANTED)
    {
      NS_ASSERTION(!m_messageID.IsEmpty(), "CANCEL_WANTED needs m_messageID");
      NS_ASSERTION(m_newsFolder, "CANCEL_WANTED needs m_newsFolder");
      NS_ASSERTION(m_key != nsMsgKey_None, "CANCEL_WANTED needs m_key");
    }
    else if (m_typeWanted == GROUP_WANTED)
      NS_ASSERTION(m_newsFolder, "GROUP_WANTED needs m_newsFolder");
    else if (m_typeWanted == SEARCH_WANTED)
      NS_ASSERTION(!m_searchData.IsEmpty(), "SEARCH_WANTED needs m_searchData");
    else if (m_typeWanted == IDS_WANTED)
      NS_ASSERTION(m_newsFolder, "IDS_WANTED needs m_newsFolder");

    return rv;
}

void nsNNTPProtocol::FinishMemCacheEntry(bool valid)
{
  nsCOMPtr <nsICacheEntry> memCacheEntry;
  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(m_runningURL);
  if (mailnewsurl)
    mailnewsurl->GetMemCacheEntry(getter_AddRefs(memCacheEntry));
  if (memCacheEntry)
  {
    if (valid)
      memCacheEntry->MarkValid();
    else
      memCacheEntry->AsyncDoom(nullptr);
  }
}

// stop binding is a "notification" informing us that the stream associated with aURL is going away.
NS_IMETHODIMP nsNNTPProtocol::OnStopRequest(nsIRequest *request, nsISupports * aContext, nsresult aStatus)
{
    // either remove mem cache entry, or mark it valid if url successful and
    // command succeeded
    FinishMemCacheEntry(NS_SUCCEEDED(aStatus)
      && MK_NNTP_RESPONSE_TYPE(m_responseCode) == MK_NNTP_RESPONSE_TYPE_OK);

    nsMsgProtocol::OnStopRequest(request, aContext, aStatus);

    // nsMsgProtocol::OnStopRequest() has called m_channelListener. There is
    // no need to be called again in CloseSocket(). Let's clear it here.
    if (m_channelListener) {
        m_channelListener = nullptr;
    }

  // okay, we've been told that the send is done and the connection is going away. So
  // we need to release all of our state
  return CloseSocket();
}

NS_IMETHODIMP nsNNTPProtocol::Cancel(nsresult status)  // handle stop button
{
    m_nextState = NNTP_ERROR;
    return nsMsgProtocol::Cancel(NS_BINDING_ABORTED);
}

nsresult
nsNNTPProtocol::ParseURL(nsIURI *aURL, nsCString &aGroup, nsCString &aMessageID)
{
    NS_ENSURE_ARG_POINTER(aURL);

    MOZ_LOG(NNTP, LogLevel::Info,("(%p) ParseURL",this));

    nsresult rv;
    nsCOMPtr <nsIMsgFolder> folder;
    nsCOMPtr <nsINntpService> nntpService = do_GetService(NS_NNTPSERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv,rv);

    nsCOMPtr<nsIMsgMessageUrl> msgUrl = do_QueryInterface(m_runningURL, &rv);
    NS_ENSURE_SUCCESS(rv,rv);

    nsCOMPtr<nsIMsgMailNewsUrl> mailnewsUrl = do_QueryInterface(msgUrl, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCString spec;
    rv = msgUrl->GetOriginalSpec(getter_Copies(spec));
    NS_ENSURE_SUCCESS(rv,rv);

    // if the original spec is non empty, use it to determine m_newsFolder and m_key
    if (!spec.IsEmpty()) {
        MOZ_LOG(NNTP, LogLevel::Info,("(%p) original message spec = %s",this,spec.get()));

        rv = nntpService->DecomposeNewsURI(spec.get(), getter_AddRefs(folder), &m_key);
        NS_ENSURE_SUCCESS(rv,rv);

        // since we are reading a message in this folder, we can set m_newsFolder
        m_newsFolder = do_QueryInterface(folder, &rv);
        NS_ENSURE_SUCCESS(rv,rv);

        // if we are cancelling, we aren't done.  we still need to parse out the messageID from the url
        // later, we'll use m_newsFolder and m_key to delete the message from the DB, if the cancel is successful.
        if (m_newsAction != nsINntpUrl::ActionCancelArticle) {
            return NS_OK;
        }
    }
    else {
        // clear this, we'll set it later.
        m_newsFolder = nullptr;
        m_currentGroup.Truncate();
    }

  // Load the values from the URL for parsing.
  rv = m_runningURL->GetGroup(aGroup);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = m_runningURL->GetMessageID(aMessageID);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(aMessageID.IsEmpty() || aMessageID != aGroup, "something not null");

  // If we are cancelling, we've got our message id, m_key, and m_newsFolder.
  // Bail out now to prevent messing those up.
  if (m_newsAction == nsINntpUrl::ActionCancelArticle)
    return NS_OK;

  rv = m_runningURL->GetKey(&m_key);
  NS_ENSURE_SUCCESS(rv, rv);

  // Check if the key is in the local cache.
  // It's possible that we're have a server/group/key combo that doesn't exist
  // (think nntp://server/group/key), so not having the folder isn't a bad
  // thing.
  if (m_key != nsMsgKey_None)
  {
    rv = mailnewsUrl->GetFolder(getter_AddRefs(folder));
    m_newsFolder = do_QueryInterface(folder);

    if (NS_SUCCEEDED(rv) && m_newsFolder)
    {
      bool useLocalCache = false;
      rv = folder->HasMsgOffline(m_key, &useLocalCache);
      NS_ENSURE_SUCCESS(rv,rv);

      // set message is in local cache
      rv = mailnewsUrl->SetMsgIsInLocalCache(useLocalCache);
      NS_ENSURE_SUCCESS(rv,rv);
    }
  }

  return NS_OK;
}
/*
 * Writes the data contained in dataBuffer into the current output stream. It also informs
 * the transport layer that this data is now available for transmission.
 * Returns a positive number for success, 0 for failure (not all the bytes were written to the
 * stream, etc). We need to make another pass through this file to install an error system (mscott)
 */

nsresult nsNNTPProtocol::SendData(const char * dataBuffer, bool aSuppressLogging)
{
    if (!aSuppressLogging) {
        NNTP_LOG_WRITE(dataBuffer);
    }
    else {
        MOZ_LOG(NNTP, out, ("(%p) Logging suppressed for this command (it probably contained authentication information)", this));
    }

  return nsMsgProtocol::SendData(dataBuffer); // base class actually transmits the data
}

/* gets the response code from the nntp server and the
 * response line
 *
 * returns the TCP return code from the read
 */
nsresult nsNNTPProtocol::NewsResponse(nsIInputStream *inputStream, uint32_t length)
{
  uint32_t status = 0;

  NS_PRECONDITION(nullptr != inputStream, "invalid input stream");

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

  NNTP_LOG_READ(line);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if(!line)
    return NS_ERROR_FAILURE;

  ClearFlag(NNTP_PAUSE_FOR_READ);  /* don't pause if we got a line */

  /* almost correct */
  if(status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;
  }

  m_previousResponseCode = m_responseCode;

  PR_sscanf(line, "%d", &m_responseCode);

  if (m_responseCode && PL_strlen(line) > 3)
    NS_MsgSACopy(&m_responseText, line + 4);
  else
    NS_MsgSACopy(&m_responseText, line);

  /* authentication required can come at any time
  */
  if (MK_NNTP_RESPONSE_AUTHINFO_REQUIRE == m_responseCode ||
    MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_REQUIRE == m_responseCode)
  {
    m_nextState = NNTP_BEGIN_AUTHORIZE;
  }
  else {
    m_nextState = m_nextStateAfterResponse;
  }

  PR_FREEIF(line);
  return NS_OK;
}

/* interpret the server response after the connect
 *
 * returns negative if the server responds unexpectedly
 */

nsresult nsNNTPProtocol::LoginResponse()
{
  bool postingAllowed = m_responseCode == MK_NNTP_RESPONSE_POSTING_ALLOWED;

  if(MK_NNTP_RESPONSE_TYPE(m_responseCode)!=MK_NNTP_RESPONSE_TYPE_OK)
  {
    AlertError(MK_NNTP_ERROR_MESSAGE, m_responseText);

    m_nextState = NNTP_ERROR;
    return NS_ERROR_FAILURE;
  }

  m_nntpServer->SetPostingAllowed(postingAllowed);
  m_nextState = NNTP_SEND_MODE_READER;
  return NS_OK;
}

nsresult nsNNTPProtocol::SendModeReader()
{
  nsresult rv = NS_OK;

  rv = SendData(NNTP_CMD_MODE_READER);
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_SEND_MODE_READER_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);

    NS_ENSURE_SUCCESS(rv,rv);
    return rv;
}

nsresult nsNNTPProtocol::SendModeReaderResponse()
{
  SetFlag(NNTP_READER_PERFORMED);

  /* ignore the response code and continue
   */
  bool pushAuth = false;
  nsresult rv = NS_OK;

  NS_ASSERTION(m_nntpServer, "no server, see bug #107797");
  if (m_nntpServer) {
    rv = m_nntpServer->GetPushAuth(&pushAuth);
  }
  if (NS_SUCCEEDED(rv) && pushAuth) {
    /* if the news host is set up to require volunteered (pushed) authentication,
    * do that before we do anything else
    */
    m_nextState = NNTP_BEGIN_AUTHORIZE;
  }
  else {
#ifdef HAVE_NNTP_EXTENSIONS
    m_nextState = SEND_LIST_EXTENSIONS;
#else
    m_nextState = SEND_FIRST_NNTP_COMMAND;
#endif  /* HAVE_NNTP_EXTENSIONS */
  }

  return NS_OK;
}

nsresult nsNNTPProtocol::SendListExtensions()
{
  nsresult rv = SendData(NNTP_CMD_LIST_EXTENSIONS);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = SEND_LIST_EXTENSIONS_RESPONSE;
  ClearFlag(NNTP_PAUSE_FOR_READ);
  return rv;
}

nsresult nsNNTPProtocol::SendListExtensionsResponse(nsIInputStream * inputStream, uint32_t length)
{
  nsresult rv = NS_OK;

  if (MK_NNTP_RESPONSE_TYPE(m_responseCode) == MK_NNTP_RESPONSE_TYPE_OK)
  {
    uint32_t status = 0;
    bool pauseForMoreData = false;
    char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

    if(pauseForMoreData)
    {
      SetFlag(NNTP_PAUSE_FOR_READ);
      return NS_OK;
    }
    if (!line)
      return rv;  /* no line yet */

        if ('.' != line[0]) {
            m_nntpServer->AddExtension(line);
        }
    else
    {
      /* tell libmsg that it's ok to ask this news host for extensions */
      m_nntpServer->SetSupportsExtensions(true);
      /* all extensions received */
      m_nextState = SEND_LIST_SEARCHES;
      ClearFlag(NNTP_PAUSE_FOR_READ);
    }
  }
  else
  {
    /* LIST EXTENSIONS not recognized
     * tell libmsg not to ask for any more extensions and move on to
     * the real NNTP command we were trying to do. */

     m_nntpServer->SetSupportsExtensions(false);
     m_nextState = SEND_FIRST_NNTP_COMMAND;
  }

  return NS_OK;
}

nsresult nsNNTPProtocol::SendListSearches()
{
    nsresult rv;
    bool searchable=false;

    rv = m_nntpServer->QueryExtension("SEARCH",&searchable);
    if (NS_SUCCEEDED(rv) && searchable)
  {
    rv = SendData(NNTP_CMD_LIST_SEARCHES);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = SEND_LIST_SEARCHES_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);
  }
  else
  {
    /* since SEARCH isn't supported, move on to GET */
    m_nextState = NNTP_GET_PROPERTIES;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }

  return rv;
}

nsresult nsNNTPProtocol::SendListSearchesResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;
  nsresult rv = NS_OK;

  NS_PRECONDITION(inputStream, "invalid input stream");

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  NNTP_LOG_READ(line);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  if (!line)
    return rv;  /* no line yet */

  if ('.' != line[0])
  {
        nsAutoCString charset;
        nsAutoString lineUtf16;
        if (NS_FAILED(m_nntpServer->GetCharset(charset)) ||
            NS_FAILED(nsMsgI18NConvertToUnicode(charset.get(),
                                                nsDependentCString(line),
                                                lineUtf16, true)))
            CopyUTF8toUTF16(nsDependentCString(line), lineUtf16);

    m_nntpServer->AddSearchableGroup(lineUtf16);
  }
  else
  {
    /* all searchable groups received */
    /* LIST SRCHFIELDS is legal if the server supports the SEARCH extension, which */
    /* we already know it does */
    m_nextState = NNTP_LIST_SEARCH_HEADERS;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }

  PR_FREEIF(line);
  return rv;
}

nsresult nsNNTPProtocol::SendListSearchHeaders()
{
  nsresult rv = SendData(NNTP_CMD_LIST_SEARCH_FIELDS);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_LIST_SEARCH_HEADERS_RESPONSE;
  SetFlag(NNTP_PAUSE_FOR_READ);

  return rv;
}

nsresult nsNNTPProtocol::SendListSearchHeadersResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;
  nsresult rv;

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  if (!line)
    return rv;  /* no line yet */

  if ('.' != line[0])
        m_nntpServer->AddSearchableHeader(line);
  else
  {
    m_nextState = NNTP_GET_PROPERTIES;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }

  PR_FREEIF(line);
  return rv;
}

nsresult nsNNTPProtocol::GetProperties()
{
    nsresult rv;
    bool setget=false;

    rv = m_nntpServer->QueryExtension("SETGET",&setget);
    if (NS_SUCCEEDED(rv) && setget)
  {
    rv = SendData(NNTP_CMD_GET_PROPERTIES);
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_GET_PROPERTIES_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);
  }
  else
  {
    /* since GET isn't supported, move on LIST SUBSCRIPTIONS */
    m_nextState = SEND_LIST_SUBSCRIPTIONS;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }
  return rv;
}

nsresult nsNNTPProtocol::GetPropertiesResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;
  nsresult rv;

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  if (!line)
    return rv;  /* no line yet */

  if ('.' != line[0])
  {
    char *propertyName = NS_strdup(line);
    if (propertyName)
    {
      char *space = PL_strchr(propertyName, ' ');
      if (space)
      {
        char *propertyValue = space + 1;
        *space = '\0';
                m_nntpServer->AddPropertyForGet(propertyName, propertyValue);
      }
      PR_Free(propertyName);
    }
  }
  else
  {
    /* all GET properties received, move on to LIST SUBSCRIPTIONS */
    m_nextState = SEND_LIST_SUBSCRIPTIONS;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }

  PR_FREEIF(line);
  return rv;
}

nsresult nsNNTPProtocol::SendListSubscriptions()
{
    nsresult rv = NS_OK;
/* TODO: is this needed for anything?
#if 0
    bool searchable=false;
    rv = m_nntpServer->QueryExtension("LISTSUBSCR",&listsubscr);
    if (NS_SUCCEEDED(rv) && listsubscr)
#else
  if (0)
#endif
  {
    rv = SendData(NNTP_CMD_LIST_SUBSCRIPTIONS);
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = SEND_LIST_SUBSCRIPTIONS_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);
  }
  else
*/
  {
    /* since LIST SUBSCRIPTIONS isn't supported, move on to real work */
    m_nextState = SEND_FIRST_NNTP_COMMAND;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }

  return rv;
}

nsresult nsNNTPProtocol::SendListSubscriptionsResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;
  nsresult rv;

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  if (!line)
    return rv;  /* no line yet */

  if ('.' != line[0])
  {
        NS_ERROR("fix me");
#if 0
    char *url = PR_smprintf ("%s//%s/%s", NEWS_SCHEME, m_hostName, line);
    if (url)
      MSG_AddSubscribedNewsgroup (cd->pane, url);
#endif
  }
  else
  {
    /* all default subscriptions received */
    m_nextState = SEND_FIRST_NNTP_COMMAND;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }

  PR_FREEIF(line);
  return rv;
}

/* figure out what the first command is and send it
 *
 * returns the status from the NETWrite */

nsresult nsNNTPProtocol::SendFirstNNTPCommand(nsIURI * url)
{
    char *command=0;

    if (m_typeWanted == ARTICLE_WANTED) {
        if (m_key != nsMsgKey_None) {
            nsresult rv;
            nsCString newsgroupName;
            if (m_newsFolder) {
                rv = m_newsFolder->GetRawName(newsgroupName);
                NS_ENSURE_SUCCESS(rv,rv);
            }
            MOZ_LOG(NNTP, LogLevel::Info,
                   ("(%p) current group = %s, desired group = %s", this,
                   m_currentGroup.get(), newsgroupName.get()));
            // if the current group is the desired group, we can just issue the ARTICLE command
            // if not, we have to do a GROUP first
            if (newsgroupName.Equals(m_currentGroup))
              m_nextState = NNTP_SEND_ARTICLE_NUMBER;
            else
              m_nextState = NNTP_SEND_GROUP_FOR_ARTICLE;

            ClearFlag(NNTP_PAUSE_FOR_READ);
            return NS_OK;
        }
    }

    if(m_typeWanted == NEWS_POST)
    {  /* posting to the news group */
        NS_MsgSACopy(&command, "POST");
    }
  else if (m_typeWanted == NEW_GROUPS)
  {
      uint32_t last_update;
      nsresult rv;

      if (!m_nntpServer)
      {
        NNTP_LOG_NOTE("m_nntpServer is null, panic!");
        return NS_ERROR_FAILURE;
      }
      rv = m_nntpServer->GetLastUpdatedTime(&last_update);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!last_update)
    {
        NS_MsgSACopy(&command, "LIST");
      }
    else
      {
        char small_buf[64];
        PRExplodedTime  expandedTime;
        PRTime t_usec = (PRTime)last_update * PR_USEC_PER_SEC;
        PR_ExplodeTime(t_usec, PR_LocalTimeParameters, &expandedTime);
        PR_FormatTimeUSEnglish(small_buf, sizeof(small_buf),
                               "NEWGROUPS %y%m%d %H%M%S", &expandedTime);
        NS_MsgSACopy(&command, small_buf);
      }
  }
    else if(m_typeWanted == LIST_WANTED)
    {
      nsresult rv;

    ClearFlag(NNTP_USE_FANCY_NEWSGROUP);

        NS_ASSERTION(m_nntpServer, "no m_nntpServer");
    if (!m_nntpServer) {
          NNTP_LOG_NOTE("m_nntpServer is null, panic!");
          return NS_ERROR_FAILURE;
    }

      bool xactive=false;
      rv = m_nntpServer->QueryExtension("XACTIVE",&xactive);
      if (NS_SUCCEEDED(rv) && xactive)
      {
        NS_MsgSACopy(&command, "LIST XACTIVE");
        SetFlag(NNTP_USE_FANCY_NEWSGROUP);
      }
      else
      {
        NS_MsgSACopy(&command, "LIST");
      }
  }
  else if(m_typeWanted == GROUP_WANTED)
    {
        nsresult rv = NS_ERROR_NULL_POINTER;

        NS_ASSERTION(m_newsFolder, "m_newsFolder is null, panic!");
        if (!m_newsFolder) return NS_ERROR_FAILURE;

        nsCString group_name;
        rv = m_newsFolder->GetRawName(group_name);
        NS_ASSERTION(NS_SUCCEEDED(rv),"failed to get newsgroup name");
        NS_ENSURE_SUCCESS(rv, rv);

        m_firstArticle = 0;
        m_lastArticle = 0;

        NS_MsgSACopy(&command, "GROUP ");
        NS_MsgSACat(&command, group_name.get());
      }
  else if (m_typeWanted == SEARCH_WANTED)
  {
    nsresult rv;
    MOZ_LOG(NNTP, LogLevel::Info,("(%p) doing GROUP for XPAT", this));
    nsCString group_name;

    /* for XPAT, we have to GROUP into the group before searching */
    if (!m_newsFolder) {
        NNTP_LOG_NOTE("m_newsFolder is null, panic!");
        return NS_ERROR_FAILURE;
    }
    rv = m_newsFolder->GetRawName(group_name);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_MsgSACopy(&command, "GROUP ");
    NS_MsgSACat (&command, group_name.get());

    // force a GROUP next time
    m_currentGroup.Truncate();
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_XPAT_SEND;
  }
  else if (m_typeWanted == IDS_WANTED)
  {
    m_nextState = NNTP_LIST_GROUP;
    return NS_OK;
  }
  else  /* article or cancel */
  {
    NS_ASSERTION(!m_messageID.IsEmpty(), "No message ID, bailing!");
    if (m_messageID.IsEmpty()) return NS_ERROR_FAILURE;

    if (m_typeWanted == CANCEL_WANTED)
      NS_MsgSACopy(&command, "HEAD ");
    else {
      NS_ASSERTION(m_typeWanted == ARTICLE_WANTED, "not cancel, and not article");
      NS_MsgSACopy(&command, "ARTICLE ");
    }

    if (m_messageID[0] != '<')
      NS_MsgSACat(&command,"<");

    NS_MsgSACat(&command, m_messageID.get());

    if (PL_strchr(command+8, '>')==0)
      NS_MsgSACat(&command,">");
  }

  NS_MsgSACat(&command, CRLF);
  nsresult rv = SendData(command);
  PR_Free(command);

  m_nextState = NNTP_RESPONSE;
  if (m_typeWanted != SEARCH_WANTED)
    m_nextStateAfterResponse = SEND_FIRST_NNTP_COMMAND_RESPONSE;
  SetFlag(NNTP_PAUSE_FOR_READ);
  return rv;
} /* sent first command */


/* interprets the server response from the first command sent
 *
 * returns negative if the server responds unexpectedly
 */

nsresult nsNNTPProtocol::SendFirstNNTPCommandResponse()
{
  int32_t major_opcode = MK_NNTP_RESPONSE_TYPE(m_responseCode);

  if((major_opcode == MK_NNTP_RESPONSE_TYPE_CONT &&
    m_typeWanted == NEWS_POST)
    || (major_opcode == MK_NNTP_RESPONSE_TYPE_OK &&
    m_typeWanted != NEWS_POST) )
  {

    m_nextState = SETUP_NEWS_STREAM;
    SetFlag(NNTP_SOME_PROTOCOL_SUCCEEDED);
    return NS_OK;
  }
  else
  {
    nsresult rv = NS_OK;

    nsString group_name;
    NS_ASSERTION(m_newsFolder, "no newsFolder");
    if (m_newsFolder)
      rv = m_newsFolder->GetUnicodeName(group_name);

    if (m_responseCode == MK_NNTP_RESPONSE_GROUP_NO_GROUP &&
      m_typeWanted == GROUP_WANTED) {
      MOZ_LOG(NNTP, LogLevel::Info,("(%p) group (%s) not found, so unset"
                                 " m_currentGroup", this,
                                 NS_ConvertUTF16toUTF8(group_name).get()));
      m_currentGroup.Truncate();

      m_nntpServer->GroupNotFound(m_msgWindow, group_name, true /* opening */);
    }

    /* if the server returned a 400 error then it is an expected
    * error.  the NEWS_ERROR state will not sever the connection
    */
    if(major_opcode == MK_NNTP_RESPONSE_TYPE_CANNOT)
      m_nextState = NEWS_ERROR;
    else
      m_nextState = NNTP_ERROR;
    // if we have no channel listener, then we're likely downloading
    // the message for offline use (or at least not displaying it)
    bool savingArticleOffline = (m_channelListener == nullptr);

    if (m_runningURL)
      FinishMemCacheEntry(false);  // cleanup mem cache entry

    if (NS_SUCCEEDED(rv) && !group_name.IsEmpty() && !savingArticleOffline) {
      nsString titleStr;
      rv = GetNewsStringByName("htmlNewsErrorTitle", getter_Copies(titleStr));
      NS_ENSURE_SUCCESS(rv,rv);

      nsString newsErrorStr;
      rv = GetNewsStringByName("htmlNewsError", getter_Copies(newsErrorStr));
      NS_ENSURE_SUCCESS(rv,rv);
      nsAutoString errorHtml;
      errorHtml.Append(newsErrorStr);

      errorHtml.AppendLiteral("<b>");
      errorHtml.Append(NS_ConvertASCIItoUTF16(m_responseText));
      errorHtml.AppendLiteral("</b><p>");

      rv = GetNewsStringByName("articleExpired", getter_Copies(newsErrorStr));
      NS_ENSURE_SUCCESS(rv,rv);
      errorHtml.Append(newsErrorStr);

      char outputBuffer[OUTPUT_BUFFER_SIZE];

      if ((m_key != nsMsgKey_None) && m_newsFolder) {
        nsCString messageID;
        rv = m_newsFolder->GetMessageIdForKey(m_key, messageID);
        if (NS_SUCCEEDED(rv)) {
          PR_snprintf(outputBuffer, OUTPUT_BUFFER_SIZE,"<P>&lt;%.512s&gt; (%lu)", messageID.get(), m_key);
          errorHtml.Append(NS_ConvertASCIItoUTF16(outputBuffer));
        }
      }

      if (m_newsFolder) {
        nsCOMPtr <nsIMsgFolder> folder = do_QueryInterface(m_newsFolder, &rv);
        if (NS_SUCCEEDED(rv) && folder) {
          nsCString folderURI;
          rv = folder->GetURI(folderURI);
          if (NS_SUCCEEDED(rv)) {
            PR_snprintf(outputBuffer,OUTPUT_BUFFER_SIZE,"<P> <A HREF=\"%s?list-ids\">", folderURI.get());
          }
        }
      }

      errorHtml.Append(NS_ConvertASCIItoUTF16(outputBuffer));

      rv = GetNewsStringByName("removeExpiredArtLinkText", getter_Copies(newsErrorStr));
      NS_ENSURE_SUCCESS(rv,rv);
      errorHtml.Append(newsErrorStr);
      errorHtml.AppendLiteral("</A> </P>");

      if (!m_msgWindow) {
        nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(m_runningURL);
        if (mailnewsurl) {
          rv = mailnewsurl->GetMsgWindow(getter_AddRefs(m_msgWindow));
          NS_ENSURE_SUCCESS(rv,rv);
        }
      }
      if (!m_msgWindow) return NS_ERROR_FAILURE;

      // note, this will cause us to close the connection.
      // this will call nsDocShell::LoadURI(), which will
      // call nsDocShell::Stop(STOP_NETWORK), which will eventually
      // call nsNNTPProtocol::Cancel(), which will close the socket.
      // we need to fix this, since the connection is still valid.
      rv = m_msgWindow->DisplayHTMLInMessagePane(titleStr, errorHtml, true);
      NS_ENSURE_SUCCESS(rv,rv);
    }
    // let's take the opportunity of removing the hdr from the db so we don't try to download
    // it again.
    else if (savingArticleOffline)
    {
      if ((m_key != nsMsgKey_None) && (m_newsFolder)) {
         rv = m_newsFolder->RemoveMessage(m_key);
      }
    }
    return NS_ERROR_FAILURE;
  }

}

nsresult nsNNTPProtocol::SendGroupForArticle()
{
  nsresult rv;

  nsCString groupname;
  rv = m_newsFolder->GetRawName(groupname);
  NS_ASSERTION(NS_SUCCEEDED(rv) && !groupname.IsEmpty(), "no group name");

  char outputBuffer[OUTPUT_BUFFER_SIZE];

  PR_snprintf(outputBuffer,
      OUTPUT_BUFFER_SIZE,
      "GROUP %.512s" CRLF,
      groupname.get());

  rv = SendData(outputBuffer);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_SEND_GROUP_FOR_ARTICLE_RESPONSE;
  SetFlag(NNTP_PAUSE_FOR_READ);
  return rv;
}

nsresult
nsNNTPProtocol::SetCurrentGroup()
{
  nsCString groupname;
  NS_ASSERTION(m_newsFolder, "no news folder");
  if (!m_newsFolder) {
    m_currentGroup.Truncate();
    return NS_ERROR_UNEXPECTED;
  }

  mozilla::DebugOnly<nsresult> rv = m_newsFolder->GetRawName(groupname);
  NS_ASSERTION(NS_SUCCEEDED(rv) && !groupname.IsEmpty(), "no group name");
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) SetCurrentGroup to %s",this, groupname.get()));
  m_currentGroup = groupname;
  return NS_OK;
}

nsresult nsNNTPProtocol::SendGroupForArticleResponse()
{
  /* ignore the response code and continue
   */
  m_nextState = NNTP_SEND_ARTICLE_NUMBER;

  return SetCurrentGroup();
}


nsresult nsNNTPProtocol::SendArticleNumber()
{
  char outputBuffer[OUTPUT_BUFFER_SIZE];
  PR_snprintf(outputBuffer, OUTPUT_BUFFER_SIZE, "ARTICLE %lu" CRLF, m_key);

  nsresult rv = SendData(outputBuffer);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = SEND_FIRST_NNTP_COMMAND_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);

  return rv;
}

nsresult nsNNTPProtocol::BeginArticle()
{
  if (m_typeWanted != ARTICLE_WANTED && m_typeWanted != CANCEL_WANTED)
    return NS_OK;

  /*  Set up the HTML stream
   */

#ifdef NO_ARTICLE_CACHEING
  ce->format_out = CLEAR_CACHE_BIT (ce->format_out);
#endif

  // if we have a channel listener,
  // create a pipe to pump the message into...the output will go to whoever
  // is consuming the message display
  //
  // the pipe must have an unlimited length since we are going to be filling
  // it on the main thread while reading it from the main thread.  iow, the
  // write must not block!! (see bug 190988)
  //
  if (m_channelListener) {
      nsCOMPtr<nsIPipe> pipe = do_CreateInstance("@mozilla.org/pipe;1");
      nsresult rv = pipe->Init(false, false, 4096, PR_UINT32_MAX);
      NS_ENSURE_SUCCESS(rv, rv);

      // These always succeed because the pipe is initialized above.
      MOZ_ALWAYS_SUCCEEDS(pipe->GetInputStream(getter_AddRefs(mDisplayInputStream)));
      MOZ_ALWAYS_SUCCEEDS(pipe->GetOutputStream(getter_AddRefs(mDisplayOutputStream)));
  }

  m_nextState = NNTP_READ_ARTICLE;

  return NS_OK;
}

nsresult nsNNTPProtocol::DisplayArticle(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t line_length = 0;

  bool pauseForMoreData = false;
  if (m_channelListener)
  {
    nsresult rv = NS_OK;
    char *line = m_lineStreamBuffer->ReadNextLine(inputStream, line_length, pauseForMoreData, &rv, true);
    if (pauseForMoreData)
    {
      uint64_t inlength = 0;
      mDisplayInputStream->Available(&inlength);
      if (inlength > 0) // broadcast our batched up ODA changes
        m_channelListener->OnDataAvailable(this, m_channelContext, mDisplayInputStream, 0, std::min(inlength, uint64_t(PR_UINT32_MAX)));
      SetFlag(NNTP_PAUSE_FOR_READ);
      PR_Free(line);
      return rv;
    }

    if (m_newsFolder)
      m_newsFolder->NotifyDownloadedLine(line, m_key);

    // line only contains a single dot -> message end
    if (line_length == 1 + MSG_LINEBREAK_LEN && line[0] == '.')
    {
      m_nextState = NEWS_DONE;

      ClearFlag(NNTP_PAUSE_FOR_READ);

      uint64_t inlength = 0;
      mDisplayInputStream->Available(&inlength);
      if (inlength > 0) // broadcast our batched up ODA changes
        m_channelListener->OnDataAvailable(this, m_channelContext, mDisplayInputStream, 0, std::min(inlength, uint64_t(PR_UINT32_MAX)));
      PR_Free(line);
      return rv;
    }
    else // we aren't finished with the message yet
    {
      uint32_t count = 0;

      // skip over the quoted '.'
      if (line_length > 1 && line[0] == '.' && line[1] == '.')
        mDisplayOutputStream->Write(line+1, line_length-1, &count);
      else
        mDisplayOutputStream->Write(line, line_length, &count);
    }

    PR_Free(line);
  }

  return NS_OK;
}

nsresult nsNNTPProtocol::ReadArticle(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;
  nsresult rv;
  char *outputBuffer;

  bool pauseForMoreData = false;

  // if we have a channel listener, spool directly to it....
  // otherwise we must be doing something like save to disk or cancel
  // in which case we are doing the work.
  if (m_channelListener)
    return DisplayArticle(inputStream, length);


  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv, true);
  if (m_newsFolder && line)
  {
    const char *unescapedLine = line;
    // lines beginning with '.' are escaped by nntp server
    // or is it just '.' on a line by itself?
    if (line[0] == '.' && line[1] == '.')
      unescapedLine++;
    m_newsFolder->NotifyDownloadedLine(unescapedLine, m_key);

  }

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  if(status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;
  }

  if(!line)
    return rv;  /* no line yet or error */

  nsCOMPtr<nsISupports> ctxt = do_QueryInterface(m_runningURL);

  if (m_typeWanted == CANCEL_WANTED && m_responseCode != MK_NNTP_RESPONSE_ARTICLE_HEAD)
  {
    /* HEAD command failed. */
    PR_FREEIF(line);
    return NS_ERROR_FAILURE;
  }

  if (line[0] == '.' && line[MSG_LINEBREAK_LEN + 1] == 0)
  {
    if (m_typeWanted == CANCEL_WANTED)
      m_nextState = NEWS_START_CANCEL;
    else
      m_nextState = NEWS_DONE;

    ClearFlag(NNTP_PAUSE_FOR_READ);
  }
  else
  {
    if (line[0] == '.')
      outputBuffer = line + 1;
    else
      outputBuffer = line;

      /* Don't send content-type to mime parser if we're doing a cancel
      because it confuses mime parser into not parsing.
      */
    if (m_typeWanted != CANCEL_WANTED || strncmp(outputBuffer, "Content-Type:", 13))
    {
      // if we are attempting to cancel, we want to snarf the headers and save the aside, which is what
      // ParseHeaderForCancel() does.
      if (m_typeWanted == CANCEL_WANTED) {
        ParseHeaderForCancel(outputBuffer);
      }

    }
  }

  PR_Free(line);

  return NS_OK;
}

void nsNNTPProtocol::ParseHeaderForCancel(char *buf)
{
    static int lastHeader = 0;
    nsAutoCString header(buf);
    if (header.First() == ' ' || header.First() == '\t') {
        header.StripWhitespace();
        // Add folded line to header if needed.
        switch (lastHeader) {
        case 1:
            m_cancelFromHdr += header;
            break;
        case 2:
            m_cancelID += header;
            break;
        case 3:
            m_cancelNewsgroups += header;
            break;
        case 4:
            m_cancelDistribution += header;
            break;
        }
        // Other folded lines are of no interest.
        return;
    }

    lastHeader = 0;
    int32_t colon = header.FindChar(':');
    if (!colon)
    return;

    nsCString value(Substring(header, colon + 1));
    value.StripWhitespace();

    switch (header.First()) {
    case 'F': case 'f':
        if (header.Find("From", CaseInsensitiveCompare) == 0) {
            m_cancelFromHdr = value;
            lastHeader = 1;
        }
        break;
    case 'M': case 'm':
        if (header.Find("Message-ID", CaseInsensitiveCompare) == 0) {
            m_cancelID = value;
            lastHeader = 2;
        }
        break;
    case 'N': case 'n':
        if (header.Find("Newsgroups", CaseInsensitiveCompare) == 0) {
            m_cancelNewsgroups = value;
            lastHeader = 3;
        }
        break;
     case 'D': case 'd':
        if (header.Find("Distributions", CaseInsensitiveCompare) == 0) {
            m_cancelDistribution = value;
            lastHeader = 4;
        }
        break;
    }

  return;
}

nsresult nsNNTPProtocol::BeginAuthorization()
{
  char * command = 0;
  nsresult rv = NS_OK;

  if (!m_newsFolder && m_nntpServer) {
    nsCOMPtr<nsIMsgIncomingServer> server = do_QueryInterface(m_nntpServer);
    if (m_nntpServer) {
      nsCOMPtr<nsIMsgFolder> rootFolder;
      rv = server->GetRootFolder(getter_AddRefs(rootFolder));
      if (NS_SUCCEEDED(rv) && rootFolder) {
        m_newsFolder = do_QueryInterface(rootFolder);
      }
    }
  }

  NS_ASSERTION(m_newsFolder, "no m_newsFolder");
  if (!m_newsFolder)
    return NS_ERROR_FAILURE;

  // We want to get authentication credentials, but it is possible that the
  // master password prompt will end up being synchronous. In that case, check
  // to see if we already have the credentials stored.
  nsCString username, password;
  rv = m_newsFolder->GetGroupUsername(username);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = m_newsFolder->GetGroupPassword(password);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we don't have either a username or a password, queue an asynchronous
  // prompt.
  if (username.IsEmpty() || password.IsEmpty())
  {
    nsCOMPtr<nsIMsgAsyncPrompter> asyncPrompter =
      do_GetService(NS_MSGASYNCPROMPTER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // Get the key to coalesce auth prompts.
    bool singleSignon = false;
    m_nntpServer->GetSingleSignon(&singleSignon);
    
    nsCString queueKey;
    nsCOMPtr<nsIMsgIncomingServer> server = do_QueryInterface(m_nntpServer);
    server->GetKey(queueKey);
    if (!singleSignon)
    {
      nsCString groupName;
      m_newsFolder->GetRawName(groupName);
      queueKey += groupName;
    }

    // If we were called back from HandleAuthenticationFailure, we must have
    // been handling the response of an authorization state. In that case,
    // let's hurry up on the auth request
    bool didAuthFail = m_nextStateAfterResponse == NNTP_AUTHORIZE_RESPONSE ||
      m_nextStateAfterResponse == NNTP_PASSWORD_RESPONSE;
    rv = asyncPrompter->QueueAsyncAuthPrompt(queueKey, didAuthFail, this);
    NS_ENSURE_SUCCESS(rv, rv);

    m_nextState = NNTP_SUSPENDED;
    if (m_request)
      m_request->Suspend();
    return NS_OK;
  }

  NS_MsgSACopy(&command, "AUTHINFO user ");
  MOZ_LOG(NNTP,  LogLevel::Info,("(%p) use %s as the username", this, username.get()));
  NS_MsgSACat(&command, username.get());
  NS_MsgSACat(&command, CRLF);

  rv = SendData(command);

  PR_Free(command);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_AUTHORIZE_RESPONSE;

  SetFlag(NNTP_PAUSE_FOR_READ);

  return rv;
}

nsresult nsNNTPProtocol::AuthorizationResponse()
{
  nsresult rv = NS_OK;

  if (MK_NNTP_RESPONSE_AUTHINFO_OK == m_responseCode ||
    MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_OK == m_responseCode)
  {
    /* successful login */
#ifdef HAVE_NNTP_EXTENSIONS
    bool pushAuth;
    /* If we're here because the host demanded authentication before we
    * even sent a single command, then jump back to the beginning of everything
    */
    rv = m_nntpServer->GetPushAuth(&pushAuth);

    if (!TestFlag(NNTP_READER_PERFORMED))
      m_nextState = NNTP_SEND_MODE_READER;
      /* If we're here because the host needs pushed authentication, then we
      * should jump back to SEND_LIST_EXTENSIONS
    */
    else if (NS_SUCCEEDED(rv) && pushAuth)
      m_nextState = SEND_LIST_EXTENSIONS;
    else
      /* Normal authentication */
      m_nextState = SEND_FIRST_NNTP_COMMAND;
#else
    if (!TestFlag(NNTP_READER_PERFORMED))
      m_nextState = NNTP_SEND_MODE_READER;
    else
      m_nextState = SEND_FIRST_NNTP_COMMAND;
#endif /* HAVE_NNTP_EXTENSIONS */

    return NS_OK;
  }
  else if (MK_NNTP_RESPONSE_AUTHINFO_CONT == m_responseCode)
  {
    char * command = 0;

    // Since we had to have called BeginAuthorization to get here, we've already
    // prompted for the authorization credentials. Just grab them without a
    // further prompt.
    nsCString password;
    rv = m_newsFolder->GetGroupPassword(password);
    if (NS_FAILED(rv) || password.IsEmpty())
      return NS_ERROR_FAILURE;

    NS_MsgSACopy(&command, "AUTHINFO pass ");
    NS_MsgSACat(&command, password.get());
    NS_MsgSACat(&command, CRLF);

    rv = SendData(command, true);

    PR_FREEIF(command);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_PASSWORD_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);

    return rv;
  }
  else
  {
    /* login failed */
    HandleAuthenticationFailure();
    return NS_OK;
  }

  NS_ERROR("should never get here");
  return NS_ERROR_FAILURE;

}

nsresult nsNNTPProtocol::PasswordResponse()
{
  if (MK_NNTP_RESPONSE_AUTHINFO_OK == m_responseCode ||
    MK_NNTP_RESPONSE_AUTHINFO_SIMPLE_OK == m_responseCode)
  {
    /* successful login */
#ifdef HAVE_NNTP_EXTENSIONS
    bool pushAuth;
    /* If we're here because the host demanded authentication before we
    * even sent a single command, then jump back to the beginning of everything
    */
    nsresult rv = m_nntpServer->GetPushAuth(&pushAuth);

    if (!TestFlag(NNTP_READER_PERFORMED))
      m_nextState = NNTP_SEND_MODE_READER;
      /* If we're here because the host needs pushed authentication, then we
      * should jump back to SEND_LIST_EXTENSIONS
    */
    else if (NS_SUCCEEDED(rv) && pushAuth)
      m_nextState = SEND_LIST_EXTENSIONS;
    else
      /* Normal authentication */
      m_nextState = SEND_FIRST_NNTP_COMMAND;
#else
    if (!TestFlag(NNTP_READER_PERFORMED))
      m_nextState = NNTP_SEND_MODE_READER;
    else
      m_nextState = SEND_FIRST_NNTP_COMMAND;
#endif /* HAVE_NNTP_EXTENSIONS */
    return NS_OK;
  }
  else
  {
    HandleAuthenticationFailure();
    return NS_OK;
  }

  NS_ERROR("should never get here");
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsNNTPProtocol::OnPromptStartAsync(nsIMsgAsyncPromptCallback *aCallback)
{
  bool result = false;
  OnPromptStart(&result);
  return aCallback->OnAuthResult(result);
}

NS_IMETHODIMP nsNNTPProtocol::OnPromptStart(bool *authAvailable)
{
  NS_ENSURE_ARG_POINTER(authAvailable);
  NS_ENSURE_STATE(m_nextState == NNTP_SUSPENDED);
  
  if (!m_newsFolder)
  {
    // If we don't have a news folder, we may have been closed already.
    NNTP_LOG_NOTE("Canceling queued authentication prompt");
    *authAvailable = false;
    return NS_OK;
  }

  bool didAuthFail = m_nextState == NNTP_AUTHORIZE_RESPONSE ||
    m_nextState == NNTP_PASSWORD_RESPONSE;
  nsresult rv = m_newsFolder->GetAuthenticationCredentials(m_msgWindow,
    true, didAuthFail, authAvailable);
  NS_ENSURE_SUCCESS(rv, rv);

  // What we do depends on whether or not we have valid credentials
  return *authAvailable ? OnPromptAuthAvailable() : OnPromptCanceled();
}

NS_IMETHODIMP nsNNTPProtocol::OnPromptAuthAvailable()
{
  NS_ENSURE_STATE(m_nextState == NNTP_SUSPENDED);

  // We previously suspended the request; now resume it to read input
  if (m_request)
    m_request->Resume();

  // Now we have our password details accessible from the group, so just call
  // into the state machine to start the process going again.
  m_nextState = NNTP_BEGIN_AUTHORIZE;
  return ProcessProtocolState(nullptr, nullptr, 0, 0);
}

NS_IMETHODIMP nsNNTPProtocol::OnPromptCanceled()
{
  NS_ENSURE_STATE(m_nextState == NNTP_SUSPENDED);
 
  // We previously suspended the request; now resume it to read input
  if (m_request)
    m_request->Resume();

  // Since the prompt was canceled, we can no longer continue the connection.
  // Thus, we need to go to the NNTP_ERROR state.
  m_nextState = NNTP_ERROR;
  return ProcessProtocolState(nullptr, nullptr, 0, 0);
}

nsresult nsNNTPProtocol::DisplayNewsgroups()
{
  m_nextState = NEWS_DONE;
  ClearFlag(NNTP_PAUSE_FOR_READ);

  MOZ_LOG(NNTP, LogLevel::Info,("(%p) DisplayNewsgroups()",this));

  return NS_OK;
}

nsresult nsNNTPProtocol::BeginNewsgroups()
{
  m_nextState = NNTP_NEWGROUPS;
  mBytesReceived = 0;
    mBytesReceivedSinceLastStatusUpdate = 0;
    m_startTime = PR_Now();
  return NS_OK;
}

nsresult nsNNTPProtocol::ProcessNewsgroups(nsIInputStream * inputStream, uint32_t length)
{
  char *line, *lineToFree, *s, *s1=NULL, *s2=NULL;
  uint32_t status = 0;
  nsresult rv = NS_OK;

  bool pauseForMoreData = false;
  line = lineToFree = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if(!line)
    return rv;  /* no line yet */

                     /* End of list?
   */
  if (line[0]=='.' && line[1]=='\0')
  {
    ClearFlag(NNTP_PAUSE_FOR_READ);
    bool xactive=false;
    rv = m_nntpServer->QueryExtension("XACTIVE",&xactive);
    if (NS_SUCCEEDED(rv) && xactive)
    {
      nsAutoCString groupName;
      rv = m_nntpServer->GetFirstGroupNeedingExtraInfo(groupName);
      if (NS_SUCCEEDED(rv)) {
        rv = m_nntpServer->FindGroup(groupName, getter_AddRefs(m_newsFolder));
        NS_ASSERTION(NS_SUCCEEDED(rv), "FindGroup failed");
        m_nextState = NNTP_LIST_XACTIVE;
        MOZ_LOG(NNTP, LogLevel::Info,("(%p) listing xactive for %s", this,
                                   groupName.get()));
        PR_Free(lineToFree);
        return NS_OK;
      }
    }
    m_nextState = NEWS_DONE;

    PR_Free(lineToFree);
    if(status > 0)
      return NS_OK;
    else
      return rv;
  }
  else if (line [0] == '.' && line [1] == '.')
    /* The NNTP server quotes all lines beginning with "." by doubling it. */
    line++;

    /* almost correct
  */
  if(status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;
  }

  /* format is "rec.arts.movies.past-films 7302 7119 y"
   */
  s = PL_strchr (line, ' ');
  if (s)
  {
    *s = 0;
    s1 = s+1;
    s = PL_strchr (s1, ' ');
    if (s)
    {
      *s = 0;
      s2 = s+1;
      s = PL_strchr (s2, ' ');
      if (s)
      {
        *s = 0;
      }
    }
  }

  mBytesReceived += status;
  mBytesReceivedSinceLastStatusUpdate += status;

  NS_ASSERTION(m_nntpServer, "no nntp incoming server");
  if (m_nntpServer) {
    rv = m_nntpServer->AddNewsgroupToList(line);
    NS_ASSERTION(NS_SUCCEEDED(rv),"failed to add to subscribe ds");
  }

  bool xactive=false;
  rv = m_nntpServer->QueryExtension("XACTIVE",&xactive);
  if (NS_SUCCEEDED(rv) && xactive)
  {
    nsAutoCString charset;
    nsAutoString lineUtf16;
    if (NS_SUCCEEDED(m_nntpServer->GetCharset(charset)) &&
        NS_SUCCEEDED(nsMsgI18NConvertToUnicode(charset.get(),
                                               nsDependentCString(line),
                                               lineUtf16, true)))
      m_nntpServer->SetGroupNeedsExtraInfo(NS_ConvertUTF16toUTF8(lineUtf16),
                                           true);
    else
      m_nntpServer->SetGroupNeedsExtraInfo(nsDependentCString(line), true);
  }

  PR_Free(lineToFree);
  return rv;
}

/* Ahhh, this like print's out the headers and stuff
 *
 * always returns 0
 */

nsresult nsNNTPProtocol::BeginReadNewsList()
{
  m_readNewsListCount = 0;
    mNumGroupsListed = 0;
    m_nextState = NNTP_READ_LIST;

    mBytesReceived = 0;
    mBytesReceivedSinceLastStatusUpdate = 0;
    m_startTime = PR_Now();

  return NS_OK;
}

#define RATE_CONSTANT 976.5625      /* PR_USEC_PER_SEC / 1024 bytes */

static void ComputeRate(int32_t bytes, PRTime startTime, float *rate)
{
  // rate = (bytes / USECS since start) * RATE_CONSTANT

  // compute usecs since we started.
  int32_t delta = (int32_t)(PR_Now() - startTime);

  // compute rate
  if (delta > 0) {
    *rate = (float) ((bytes * RATE_CONSTANT) / delta);
  }
  else {
    *rate = 0.0;
  }
}

/* display a list of all or part of the newsgroups list
 * from the news server
 */
nsresult nsNNTPProtocol::ReadNewsList(nsIInputStream * inputStream, uint32_t length)
{
  nsresult rv = NS_OK;
  int32_t i=0;
  uint32_t status = 1;

  bool pauseForMoreData = false;
  char *line, *lineToFree;
  line = lineToFree = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if (pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    PR_Free(lineToFree);
    return NS_OK;
  }

  if (!line)
    return rv;  /* no line yet */

  /* End of list? */
  if (line[0]=='.' && line[1]=='\0')
  {
    bool listpnames=false;
    NS_ASSERTION(m_nntpServer, "no nntp incoming server");
    if (m_nntpServer) {
      rv = m_nntpServer->QueryExtension("LISTPNAMES",&listpnames);
    }
    if (NS_SUCCEEDED(rv) && listpnames)
      m_nextState = NNTP_LIST_PRETTY_NAMES;
    else
      m_nextState = DISPLAY_NEWSGROUPS;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    PR_Free(lineToFree);
    return NS_OK;
  }
  else if (line[0] == '.')
  {
    if ((line[1] == ' ') || (line[1] == '.' && line [2] == '.' && line[3] == ' '))
    {
      // some servers send "... 0000000001 0000000002 y"
      // and some servers send ". 0000000001 0000000002 y"
      // just skip that those lines
      // see bug #69231 and #123560
      PR_Free(lineToFree);
      return rv;
    }
    // The NNTP server quotes all lines beginning with "." by doubling it, so unquote
    line++;
  }

  /* almost correct
  */
  if(status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;

    if ((mBytesReceivedSinceLastStatusUpdate > UPDATE_THRESHHOLD) && m_msgWindow) {
      mBytesReceivedSinceLastStatusUpdate = 0;

      nsCOMPtr <nsIMsgStatusFeedback> msgStatusFeedback;

      rv = m_msgWindow->GetStatusFeedback(getter_AddRefs(msgStatusFeedback));
      NS_ENSURE_SUCCESS(rv, rv);

      nsString statusString;

      nsCOMPtr<nsIStringBundleService> bundleService =
        mozilla::services::GetStringBundleService();
      NS_ENSURE_TRUE(bundleService, NS_ERROR_UNEXPECTED);

      nsCOMPtr<nsIStringBundle> bundle;
      rv = bundleService->CreateBundle(NEWS_MSGS_URL, getter_AddRefs(bundle));
      NS_ENSURE_SUCCESS(rv, rv);

      nsAutoString bytesStr;
      bytesStr.AppendInt(mBytesReceived / 1024);

      // compute the rate, and then convert it have one
      // decimal precision.
      float rate = 0.0;
      ComputeRate(mBytesReceived, m_startTime, &rate);
      char rate_buf[RATE_STR_BUF_LEN];
      PR_snprintf(rate_buf,RATE_STR_BUF_LEN,"%.1f", rate);

      nsAutoString numGroupsStr;
      numGroupsStr.AppendInt(mNumGroupsListed);
      NS_ConvertASCIItoUTF16 rateStr(rate_buf);

      const char16_t *formatStrings[3] = { numGroupsStr.get(), bytesStr.get(), rateStr.get()};
      rv = bundle->FormatStringFromName(u"bytesReceived",
        formatStrings, 3,
        getter_Copies(statusString));

      rv = msgStatusFeedback->ShowStatusString(statusString);
      if (NS_FAILED(rv)) {
        PR_Free(lineToFree);
        return rv;
      }
    }
  }

  /* find whitespace separator if it exits */
  for(i=0; line[i] != '\0' && !NET_IS_SPACE(line[i]); i++)
    ;  /* null body */

  line[i] = 0; /* terminate group name */

  /* store all the group names */
  NS_ASSERTION(m_nntpServer, "no nntp incoming server");
  if (m_nntpServer) {
    m_readNewsListCount++;
    mNumGroupsListed++;
    rv = m_nntpServer->AddNewsgroupToList(line);
//    NS_ASSERTION(NS_SUCCEEDED(rv),"failed to add to subscribe ds");
    // since it's not fatal, don't let this error stop the LIST command.
    rv = NS_OK;
  }
  else
    rv = NS_ERROR_FAILURE;

  if (m_readNewsListCount == READ_NEWS_LIST_COUNT_MAX) {
    m_readNewsListCount = 0;
    if (mUpdateTimer) {
      mUpdateTimer->Cancel();
      mUpdateTimer = nullptr;
    }
    mUpdateTimer = do_CreateInstance("@mozilla.org/timer;1", &rv);
    NS_ASSERTION(NS_SUCCEEDED(rv),"failed to create timer");
    if (NS_FAILED(rv)) {
      PR_Free(lineToFree);
      return rv;
    }

    mInputStream = inputStream;

    const uint32_t kUpdateTimerDelay = READ_NEWS_LIST_TIMEOUT;
    rv = mUpdateTimer->InitWithCallback(static_cast<nsITimerCallback*>(this), kUpdateTimerDelay,
      nsITimer::TYPE_ONE_SHOT);
    NS_ASSERTION(NS_SUCCEEDED(rv),"failed to init timer");
    if (NS_FAILED(rv)) {
      PR_Free(lineToFree);
      return rv;
    }

    m_nextState = NNTP_SUSPENDED;

    // suspend necko request until timeout
    // might not have a request if someone called CloseSocket()
    // see bug #195440
    if (m_request)
      m_request->Suspend();
  }

  PR_Free(lineToFree);
  return rv;
}

NS_IMETHODIMP
nsNNTPProtocol::Notify(nsITimer *timer)
{
  NS_ASSERTION(timer == mUpdateTimer.get(), "Hey, this ain't my timer!");
  mUpdateTimer = nullptr;    // release my hold
  TimerCallback();
  return NS_OK;
}

void nsNNTPProtocol::TimerCallback()
{
  MOZ_LOG(NNTP, LogLevel::Info,("nsNNTPProtocol::TimerCallback\n"));
  m_nextState = NNTP_READ_LIST;

  // process whatever is already in the buffer at least once.
  //
  // NOTE: while downloading, it would almost be enough to just
  // resume necko since it will call us again with data.  however,
  // if we are at the end of the data stream then we must call
  // ProcessProtocolState since necko will not call us again.
  //
  // NOTE: this function may Suspend necko.  Suspend is a reference
  // counted (i.e., two suspends requires two resumes before the
  // request will actually be resumed).
  //
  ProcessProtocolState(nullptr, mInputStream, 0,0);

  // resume necko request
  // might not have a request if someone called CloseSocket()
  // see bug #195440
  if (m_request)
    m_request->Resume();

  return;
}

void nsNNTPProtocol::HandleAuthenticationFailure()
{
  nsCOMPtr<nsIMsgIncomingServer> server(do_QueryInterface(m_nntpServer));
  nsCString hostname;
  server->GetRealHostName(hostname);
  int32_t choice = 1;
  MsgPromptLoginFailed(m_msgWindow, hostname, &choice);

  if (choice == 1) // Cancel
  {
    // When the user requests to cancel the connection, we can't do anything
    // anymore.
    NNTP_LOG_NOTE("Password failed, user opted to cancel connection");
    m_nextState = NNTP_ERROR;
    return;
  }

  if (choice == 2) // New password
  {
    NNTP_LOG_NOTE("Password failed, user opted to enter new password");
    NS_ASSERTION(m_newsFolder, "no newsFolder");
    m_newsFolder->ForgetAuthenticationCredentials();
  }
  else if (choice == 0) // Retry
  {
    NNTP_LOG_NOTE("Password failed, user opted to retry");
  }

  // At this point, we've either forgotten the password or opted to retry. In
  // both cases, we need to try to auth with the password again, so return to
  // the authentication state.
  m_nextState = NNTP_BEGIN_AUTHORIZE;
}

///////////////////////////////////////////////////////////////////////////////
// XOVER, XHDR, and HEAD processing code
// Used for filters
// State machine explanation located in doxygen comments for nsNNTPProtocol
///////////////////////////////////////////////////////////////////////////////

nsresult nsNNTPProtocol::BeginReadXover()
{
  int32_t count;     /* Response fields */
  nsresult rv = NS_OK;

  rv = SetCurrentGroup();
  NS_ENSURE_SUCCESS(rv, rv);

  /* Make sure we never close and automatically reopen the connection at this
  point; we'll confuse libmsg too much... */

  SetFlag(NNTP_SOME_PROTOCOL_SUCCEEDED);

  /* We have just issued a GROUP command and read the response.
  Now parse that response to help decide which articles to request
  xover data for.
   */
  PR_sscanf(m_responseText,
    "%d %d %d",
    &count,
    &m_firstPossibleArticle,
    &m_lastPossibleArticle);

  m_newsgroupList = do_CreateInstance(NS_NNTPNEWSGROUPLIST_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = m_newsgroupList->Initialize(m_runningURL, m_newsFolder);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = m_newsFolder->UpdateSummaryFromNNTPInfo(m_firstPossibleArticle, m_lastPossibleArticle, count);
  NS_ENSURE_SUCCESS(rv, rv);

  m_numArticlesLoaded = 0;

  // if the user sets max_articles to a bogus value, get them everything
  m_numArticlesWanted = m_maxArticles > 0 ? m_maxArticles : 1L << 30;

  m_nextState = NNTP_FIGURE_NEXT_CHUNK;
  ClearFlag(NNTP_PAUSE_FOR_READ);
  return NS_OK;
}

nsresult nsNNTPProtocol::FigureNextChunk()
{
    nsresult rv = NS_OK;
  int32_t status = 0;

  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(m_runningURL);
  if (m_firstArticle > 0)
  {
      MOZ_LOG(NNTP, LogLevel::Info,("(%p) add to known articles:  %d - %d", this, m_firstArticle, m_lastArticle));

      if (NS_SUCCEEDED(rv) && m_newsgroupList) {
          rv = m_newsgroupList->AddToKnownArticles(m_firstArticle,
                                                 m_lastArticle);
      }

    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (m_numArticlesLoaded >= m_numArticlesWanted)
  {
    m_nextState = NEWS_PROCESS_XOVER;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

    NS_ASSERTION(m_newsgroupList, "no newsgroupList");
    if (!m_newsgroupList) return NS_ERROR_FAILURE;

    bool getOldMessages = false;
    if (m_runningURL) {
      rv = m_runningURL->GetGetOldMessages(&getOldMessages);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    rv = m_newsgroupList->SetGetOldMessages(getOldMessages);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = m_newsgroupList->GetRangeOfArtsToDownload(m_msgWindow,
      m_firstPossibleArticle,
      m_lastPossibleArticle,
      m_numArticlesWanted - m_numArticlesLoaded,
      &(m_firstArticle),
      &(m_lastArticle),
      &status);

  NS_ENSURE_SUCCESS(rv, rv);

  if (m_firstArticle <= 0 || m_firstArticle > m_lastArticle)
  {
    /* Nothing more to get. */
    m_nextState = NEWS_PROCESS_XOVER;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  MOZ_LOG(NNTP, LogLevel::Info,("(%p) Chunk will be (%d-%d)", this, m_firstArticle, m_lastArticle));

  m_articleNumber = m_firstArticle;

    /* was MSG_InitXOVER() */
    if (m_newsgroupList) {
        rv = m_newsgroupList->InitXOVER(m_firstArticle, m_lastArticle);
  }

  NS_ENSURE_SUCCESS(rv, rv);

  ClearFlag(NNTP_PAUSE_FOR_READ);
  if (TestFlag(NNTP_NO_XOVER_SUPPORT))
    m_nextState = NNTP_READ_GROUP;
  else
    m_nextState = NNTP_XOVER_SEND;

  return NS_OK;
}

nsresult nsNNTPProtocol::XoverSend()
{
  char outputBuffer[OUTPUT_BUFFER_SIZE];

    PR_snprintf(outputBuffer,
        OUTPUT_BUFFER_SIZE,
        "XOVER %d-%d" CRLF,
        m_firstArticle,
        m_lastArticle);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_XOVER_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);

  return SendData(outputBuffer);
}

/* see if the xover response is going to return us data
 * if the proper code isn't returned then assume xover
 * isn't supported and use
 * normal read_group
 */

nsresult nsNNTPProtocol::ReadXoverResponse()
{
#ifdef TEST_NO_XOVER_SUPPORT
  m_responseCode = MK_NNTP_RESPONSE_CHECK_ERROR; /* pretend XOVER generated an error */
#endif

    if(m_responseCode != MK_NNTP_RESPONSE_XOVER_OK)
    {
        /* If we didn't get back "224 data follows" from the XOVER request,
       then that must mean that this server doesn't support XOVER.  Or
       maybe the server's XOVER support is busted or something.  So,
       in that case, fall back to the very slow HEAD method.

       But, while debugging here at HQ, getting into this state means
       something went very wrong, since our servers do XOVER.  Thus
       the assert.
         */
    /*NS_ASSERTION (0,"something went very wrong");*/
    m_nextState = NNTP_READ_GROUP;
    SetFlag(NNTP_NO_XOVER_SUPPORT);
    }
    else
    {
        m_nextState = NNTP_XOVER;
    }

    return NS_OK;  /* continue */
}

/* process the xover list as it comes from the server
 * and load it into the sort list.
 */

nsresult nsNNTPProtocol::ReadXover(nsIInputStream * inputStream, uint32_t length)
{
  char *line, *lineToFree;
  nsresult rv;
  uint32_t status = 1;

  bool pauseForMoreData = false;
  line = lineToFree = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if(!line)
    return rv;  /* no line yet or TCP error */

  if(line[0] == '.' && line[1] == '\0')
  {
    m_nextState = NNTP_XHDR_SEND;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    PR_Free(lineToFree);
    return NS_OK;
  }
  else if (line [0] == '.' && line [1] == '.')
    /* The NNTP server quotes all lines beginning with "." by doubling it. */
    line++;

    /* almost correct
  */
  if(status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;
  }

  rv = m_newsgroupList->ProcessXOVERLINE(line, &status);
  NS_ASSERTION(NS_SUCCEEDED(rv), "failed to process the XOVERLINE");

  m_numArticlesLoaded++;
  PR_Free(lineToFree);
  return rv;
}

/* Finished processing all the XOVER data.
*/

nsresult nsNNTPProtocol::ProcessXover()
{
  nsresult rv;

  /* xover_parse_state stored in MSG_Pane cd->pane */
  NS_ASSERTION(m_newsgroupList, "no newsgroupList");
  if (!m_newsgroupList) return NS_ERROR_FAILURE;

  // Some people may use the notifications in CallFilters to close the cached
  // connections, which will clear m_newsgroupList. So we keep a copy for
  // ourselves to ward off this threat.
  nsCOMPtr<nsINNTPNewsgroupList> list(m_newsgroupList);
  list->CallFilters();
  int32_t status = 0;
  rv = list->FinishXOVERLINE(0, &status);
  m_newsgroupList = nullptr;
  if (NS_SUCCEEDED(rv) && status < 0) return NS_ERROR_FAILURE;

  m_nextState = NEWS_DONE;

  return NS_OK;
}

nsresult nsNNTPProtocol::XhdrSend()
{
  nsCString header;
  m_newsgroupList->InitXHDR(header);
  if (header.IsEmpty())
  {
    m_nextState = NNTP_FIGURE_NEXT_CHUNK;
    return NS_OK;
  }
  
  char outputBuffer[OUTPUT_BUFFER_SIZE];
  PR_snprintf(outputBuffer, OUTPUT_BUFFER_SIZE, "XHDR %s %d-%d" CRLF,
              header.get(), m_firstArticle, m_lastArticle);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_XHDR_RESPONSE;
  SetFlag(NNTP_PAUSE_FOR_READ);

  return SendData(outputBuffer);
}

nsresult nsNNTPProtocol::XhdrResponse(nsIInputStream *inputStream)
{
  if (m_responseCode != MK_NNTP_RESPONSE_XHDR_OK)
  {
    m_nextState = NNTP_READ_GROUP;
    // The reasoning behind setting this flag and not an XHDR flag is that we
    // are going to have to use HEAD instead. At that point, using XOVER as
    // well is just wasting bandwidth.
    SetFlag(NNTP_NO_XOVER_SUPPORT);
    return NS_OK;
  }
  
  char *line, *lineToFree;
  nsresult rv;
  uint32_t status = 1;

  bool pauseForMoreData = false;
  line = lineToFree = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if (pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if (!line)
    return rv;  /* no line yet or TCP error */

  if (line[0] == '.' && line[1] == '\0')
  {
    m_nextState = NNTP_XHDR_SEND;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    PR_Free(lineToFree);
    return NS_OK;
  }

  if (status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;
  }

  rv = m_newsgroupList->ProcessXHDRLine(nsDependentCString(line));
  NS_ASSERTION(NS_SUCCEEDED(rv), "failed to process the XHDRLINE");

  m_numArticlesLoaded++;
  PR_Free(lineToFree);
  return rv;
}

nsresult nsNNTPProtocol::ReadHeaders()
{
  if(m_articleNumber > m_lastArticle)
  {  /* end of groups */

    m_newsgroupList->InitHEAD(-1);
    m_nextState = NNTP_FIGURE_NEXT_CHUNK;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  else
  {
    m_newsgroupList->InitHEAD(m_articleNumber);

    char outputBuffer[OUTPUT_BUFFER_SIZE];
    PR_snprintf(outputBuffer,
      OUTPUT_BUFFER_SIZE,
      "HEAD %ld" CRLF,
      m_articleNumber++);
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_READ_GROUP_RESPONSE;

    SetFlag(NNTP_PAUSE_FOR_READ);
    return SendData(outputBuffer);
  }
}

/* See if the "HEAD" command was successful
*/

nsresult nsNNTPProtocol::ReadNewsgroupResponse()
{
  if (m_responseCode == MK_NNTP_RESPONSE_ARTICLE_HEAD)
  {     /* Head follows - parse it:*/
    m_nextState = NNTP_READ_GROUP_BODY;

    return NS_OK;
  }
  else
  {
    m_newsgroupList->HEADFailed(m_articleNumber);
    m_nextState = NNTP_READ_GROUP;
    return NS_OK;
  }
}

/* read the body of the "HEAD" command
*/
nsresult nsNNTPProtocol::ReadNewsgroupBody(nsIInputStream * inputStream, uint32_t length)
{
  char *line, *lineToFree;
  nsresult rv;
  uint32_t status = 1;

  bool pauseForMoreData = false;
  line = lineToFree = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  /* if TCP error of if there is not a full line yet return
  */
  if(!line)
    return rv;

  MOZ_LOG(NNTP, LogLevel::Info,("(%p) read_group_body: got line: %s|",this,line));

  /* End of body? */
  if (line[0]=='.' && line[1]=='\0')
  {
    m_nextState = NNTP_READ_GROUP;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  else if (line [0] == '.' && line [1] == '.')
    /* The NNTP server quotes all lines beginning with "." by doubling it. */
    line++;

  nsCString safe_line(line);
  rv = m_newsgroupList->ProcessHEADLine(safe_line);
  PR_Free(lineToFree);
  return rv;
}


nsresult nsNNTPProtocol::GetNewsStringByID(int32_t stringID, char16_t **aString)
{
  nsresult rv;
  nsAutoString resultString(NS_LITERAL_STRING("???"));

  if (!m_stringBundle)
  {
    nsCOMPtr<nsIStringBundleService> bundleService =
      mozilla::services::GetStringBundleService();
    NS_ENSURE_TRUE(bundleService, NS_ERROR_UNEXPECTED);

    rv = bundleService->CreateBundle(NEWS_MSGS_URL, getter_AddRefs(m_stringBundle));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (m_stringBundle) {
    char16_t *ptrv = nullptr;
    rv = m_stringBundle->GetStringFromID(stringID, &ptrv);

    if (NS_FAILED(rv)) {
      resultString.AssignLiteral("[StringID");
      resultString.AppendInt(stringID);
      resultString.AppendLiteral("?]");
      *aString = ToNewUnicode(resultString);
    }
    else {
      *aString = ptrv;
    }
  }
  else {
    rv = NS_OK;
    *aString = ToNewUnicode(resultString);
  }
  return rv;
}

nsresult nsNNTPProtocol::GetNewsStringByName(const char *aName, char16_t **aString)
{
  nsresult rv;
  nsAutoString resultString(NS_LITERAL_STRING("???"));
  if (!m_stringBundle)
  {
    nsCOMPtr<nsIStringBundleService> bundleService =
      mozilla::services::GetStringBundleService();
    NS_ENSURE_TRUE(bundleService, NS_ERROR_UNEXPECTED);

    rv = bundleService->CreateBundle(NEWS_MSGS_URL, getter_AddRefs(m_stringBundle));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (m_stringBundle)
  {
    nsAutoString unicodeName;
    CopyASCIItoUTF16(nsDependentCString(aName), unicodeName);

    char16_t *ptrv = nullptr;
    rv = m_stringBundle->GetStringFromName(unicodeName.get(), &ptrv);

    if (NS_FAILED(rv))
    {
      resultString.AssignLiteral("[StringName");
      resultString.Append(NS_ConvertASCIItoUTF16(aName));
      resultString.AppendLiteral("?]");
      *aString = ToNewUnicode(resultString);
    }
    else
    {
      *aString = ptrv;
    }
  }
  else
  {
    rv = NS_OK;
    *aString = ToNewUnicode(resultString);
  }
  return rv;
}

// sspitzer:  PostMessageInFile is derived from nsSmtpProtocol::SendMessageInFile()
nsresult nsNNTPProtocol::PostMessageInFile(nsIFile *postMessageFile)
{
    nsCOMPtr<nsIURI> url = do_QueryInterface(m_runningURL);
    if (url && postMessageFile)
        nsMsgProtocol::PostMessage(url, postMessageFile);

    SetFlag(NNTP_PAUSE_FOR_READ);

    // for now, we are always done at this point..we aren't making multiple
    // calls to post data...

    // always issue a '.' and CRLF when we are done...
    PL_strcpy(m_dataBuf, "." CRLF);
    SendData(m_dataBuf);
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_SEND_POST_DATA_RESPONSE;
    return NS_OK;
}

nsresult nsNNTPProtocol::PostData()
{
    /* returns 0 on done and negative on error
     * positive if it needs to continue.
     */
    NNTP_LOG_NOTE("nsNNTPProtocol::PostData()");
    nsresult rv = NS_OK;

    nsCOMPtr <nsINNTPNewsgroupPost> message;
    rv = m_runningURL->GetMessageToPost(getter_AddRefs(message));
    if (NS_SUCCEEDED(rv))
    {
        nsCOMPtr<nsIFile> filePath;
        rv = message->GetPostMessageFile(getter_AddRefs(filePath));
        if (NS_SUCCEEDED(rv))
            PostMessageInFile(filePath);
     }

    return NS_OK;
}


/* interpret the response code from the server
 * after the post is done
 */
nsresult nsNNTPProtocol::PostDataResponse()
{
  if (m_responseCode != MK_NNTP_RESPONSE_POST_OK)
  {
    AlertError(MK_NNTP_ERROR_MESSAGE,m_responseText);
    m_nextState = NEWS_ERROR;
    return NS_ERROR_FAILURE;
  }
    m_nextState = NEWS_POST_DONE;
  ClearFlag(NNTP_PAUSE_FOR_READ);
  return NS_OK;
}

nsresult nsNNTPProtocol::CheckForArticle()
{
  m_nextState = NEWS_ERROR;
  if (m_responseCode >= 220 && m_responseCode <= 223) {
    /* Yes, this article is already there, we're all done. */
    return NS_OK;
  }
  else
  {
  /* The article isn't there, so the failure we had earlier wasn't due to
     a duplicate message-id.  Return the error from that previous
     posting attempt (which is already in ce->URL_s->error_msg). */
    return NS_ERROR_FAILURE;
  }
}

nsresult nsNNTPProtocol::StartCancel()
{
  nsresult rv = SendData(NNTP_CMD_POST);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NEWS_DO_CANCEL;
  SetFlag(NNTP_PAUSE_FOR_READ);
  return rv;
}

void nsNNTPProtocol::CheckIfAuthor(nsIMsgIdentity *aIdentity, const nsCString &aOldFrom, nsCString &aFrom)
{
  nsAutoCString from;
  nsresult rv = aIdentity->GetEmail(from);
  if (NS_FAILED(rv))
    return;
  MOZ_LOG(NNTP, LogLevel::Info,("from = %s", from.get()));

  nsCString us;
  nsCString them;
  ExtractEmail(EncodedHeader(from), us);
  ExtractEmail(EncodedHeader(aOldFrom), them);

  MOZ_LOG(NNTP, LogLevel::Info,("us = %s, them = %s", us.get(), them.get()));

  if (us.Equals(them, nsCaseInsensitiveCStringComparator()))
    aFrom = from;
}

nsresult nsNNTPProtocol::DoCancel()
{
    int32_t status = 0;
    bool failure = false;
    nsresult rv = NS_OK;
    bool requireConfirmationForCancel = true;
    bool showAlertAfterCancel = true;

  /* #### Should we do a more real check than this?  If the POST command
     didn't respond with "MK_NNTP_RESPONSE_POST_SEND_NOW Ok", then it's not ready for us to throw a
     message at it...   But the normal posting code doesn't do this check.
     Why?
   */
  NS_ASSERTION (m_responseCode == MK_NNTP_RESPONSE_POST_SEND_NOW, "code != POST_SEND_NOW");

  nsCOMPtr<nsIStringBundleService> bundleService =
    mozilla::services::GetStringBundleService();
  NS_ENSURE_TRUE(bundleService, NS_ERROR_OUT_OF_MEMORY);

  nsCOMPtr<nsIStringBundle> brandBundle;
  bundleService->CreateBundle("chrome://branding/locale/brand.properties",
                              getter_AddRefs(brandBundle));
  NS_ENSURE_TRUE(brandBundle, NS_ERROR_FAILURE);

  nsString brandFullName;
  rv = brandBundle->GetStringFromName(u"brandFullName",
                                      getter_Copies(brandFullName));
  NS_ENSURE_SUCCESS(rv,rv);
  NS_ConvertUTF16toUTF8 appName(brandFullName);

  nsCString newsgroups(m_cancelNewsgroups);
  nsCString distribution (m_cancelDistribution);
  nsCString id (m_cancelID);
  nsCString oldFrom(m_cancelFromHdr);

  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  nsCOMPtr<nsIPrompt> dialog;
  if (m_runningURL)
  {
    nsCOMPtr<nsIMsgMailNewsUrl> msgUrl (do_QueryInterface(m_runningURL));
    rv = GetPromptDialogFromUrl(msgUrl, getter_AddRefs(dialog));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (id.IsEmpty() || newsgroups.IsEmpty())
    return NS_ERROR_FAILURE;

  m_cancelNewsgroups.Truncate();
  m_cancelDistribution.Truncate();
  m_cancelFromHdr.Truncate();
  m_cancelID.Truncate();

  nsString alertText;
  nsString confirmText;
  int32_t confirmCancelResult = 0;

  // A little early to declare, but the goto causes problems
  nsAutoCString otherHeaders;

  /* Make sure that this loser isn't cancelling someone else's posting.
     Yes, there are occasionally good reasons to do so.  Those people
     capable of making that decision (news admins) have other tools with
     which to cancel postings (like telnet.)

     Don't do this if server tells us it will validate user. DMB 3/19/97
   */
  bool cancelchk=false;
  rv = m_nntpServer->QueryExtension("CANCELCHK",&cancelchk);
  nsCString from;
  if (NS_SUCCEEDED(rv) && !cancelchk)
  {
    NNTP_LOG_NOTE("CANCELCHK not supported");

    // get the current identity from the news session....
    nsCOMPtr<nsIMsgAccountManager> accountManager =
      do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv) && accountManager) {
      nsCOMPtr<nsIArray> identities;
      rv = accountManager->GetAllIdentities(getter_AddRefs(identities));
      NS_ENSURE_SUCCESS(rv, rv);

      uint32_t length;
      rv = identities->GetLength(&length);
      NS_ENSURE_SUCCESS(rv, rv);

      for (uint32_t i = 0; i < length && from.IsEmpty(); ++i)
      {
        nsCOMPtr<nsIMsgIdentity> identity(do_QueryElementAt(identities, i, &rv));
        if (NS_SUCCEEDED(rv))
          CheckIfAuthor(identity, oldFrom, from);
      }
    }

    if (from.IsEmpty())
    {
      GetNewsStringByName("cancelDisallowed", getter_Copies(alertText));
      rv = dialog->Alert(nullptr, alertText.get());
      // XXX:  todo, check rv?

      /* After the cancel is disallowed, Make the status update to be the same as though the
         cancel was allowed, otherwise, the newsgroup is not able to take further requests as
         reported here */
      status = MK_NNTP_CANCEL_DISALLOWED;
      m_nextState = NNTP_RESPONSE;
      m_nextStateAfterResponse = NNTP_SEND_POST_DATA_RESPONSE;
      SetFlag(NNTP_PAUSE_FOR_READ);
      failure = true;
      goto FAIL;
    }
    else
    {
      MOZ_LOG(NNTP, LogLevel::Info,("(%p) CANCELCHK not supported, so post the cancel message as %s", this, from.get()));
    }
  }
  else
    NNTP_LOG_NOTE("CANCELCHK supported, don't do the us vs. them test");

  // QA needs to be able to disable this confirm dialog, for the automated tests.  see bug #31057
  rv = prefBranch->GetBoolPref(PREF_NEWS_CANCEL_CONFIRM, &requireConfirmationForCancel);
  if (NS_FAILED(rv) || requireConfirmationForCancel) {
    /* Last chance to cancel the cancel.*/
    GetNewsStringByName("cancelConfirm", getter_Copies(confirmText));
    bool dummyValue = false;
    rv = dialog->ConfirmEx(nullptr, confirmText.get(), nsIPrompt::STD_YES_NO_BUTTONS,
                           nullptr, nullptr, nullptr, nullptr, &dummyValue, &confirmCancelResult);
    if (NS_FAILED(rv))
    	confirmCancelResult = 1; // Default to No.
  }
  else
    confirmCancelResult = 0; // Default to Yes.
    
  if (confirmCancelResult != 0) {
      // they cancelled the cancel
      status = MK_NNTP_NOT_CANCELLED;
      failure = true;
      goto FAIL;
  }

  otherHeaders.AppendLiteral("Control: cancel ");
  otherHeaders += id;
  otherHeaders.AppendLiteral(CRLF);
  if (!distribution.IsEmpty()) {
    otherHeaders.AppendLiteral("Distribution: ");
    otherHeaders += distribution;
    otherHeaders.AppendLiteral(CRLF);
  }

  m_cancelStatus = 0;

  {
    /* NET_BlockingWrite() should go away soon? I think. */
    /* The following are what we really need to cancel a posted message */
    char *data;
    data = PR_smprintf("From: %s" CRLF
                       "Newsgroups: %s" CRLF
                       "Subject: cancel %s" CRLF
                       "References: %s" CRLF
                       "%s" /* otherHeaders, already with CRLF */
                       CRLF /* body separator */
                       "This message was cancelled from within %s." CRLF /* body */
                       "." CRLF, /* trailing message terminator "." */
                       from.get(), newsgroups.get(), id.get(), id.get(),
                       otherHeaders.get(), appName.get());

    rv = SendData(data);
    PR_Free (data);
    if (NS_FAILED(rv)) {
      nsAutoCString errorText;
      errorText.AppendInt(status);
      AlertError(MK_TCP_WRITE_ERROR, errorText.get());
      failure = true;
      goto FAIL;
    }

    SetFlag(NNTP_PAUSE_FOR_READ);
    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_SEND_POST_DATA_RESPONSE;

    // QA needs to be able to turn this alert off, for the automate tests.  see bug #31057
    rv = prefBranch->GetBoolPref(PREF_NEWS_CANCEL_ALERT_ON_SUCCESS, &showAlertAfterCancel);
    if (NS_FAILED(rv) || showAlertAfterCancel) {
      GetNewsStringByName("messageCancelled", getter_Copies(alertText));
      rv = dialog->Alert(nullptr, alertText.get());
      // XXX:  todo, check rv?
    }

    if (!m_runningURL) return NS_ERROR_FAILURE;

    // delete the message from the db here.
    NS_ASSERTION(NS_SUCCEEDED(rv) && m_newsFolder && (m_key != nsMsgKey_None), "need more to remove this message from the db");
    if ((m_key != nsMsgKey_None) && (m_newsFolder))
       rv = m_newsFolder->RemoveMessage(m_key);

  }

FAIL:
  NS_ASSERTION(m_newsFolder,"no news folder");
  if (m_newsFolder)
    rv = ( failure ) ? m_newsFolder->CancelFailed()
                     : m_newsFolder->CancelComplete();

  return rv;
}

nsresult nsNNTPProtocol::XPATSend()
{
  nsresult rv = NS_OK;
  int32_t slash = m_searchData.FindChar('/');

  if (slash >= 0)
  {
    /* extract the XPAT encoding for one query term */
    /* char *next_search = NULL; */
    char *command = NULL;
    char *unescapedCommand = NULL;
    char *endOfTerm = NULL;
    NS_MsgSACopy (&command, m_searchData.get() + slash + 1);
    endOfTerm = PL_strchr(command, '/');
    if (endOfTerm)
      *endOfTerm = '\0';
    NS_MsgSACat(&command, CRLF);

    unescapedCommand = MSG_UnEscapeSearchUrl(command);

    /* send one term off to the server */
    rv = SendData(unescapedCommand);

    m_nextState = NNTP_RESPONSE;
    m_nextStateAfterResponse = NNTP_XPAT_RESPONSE;
    SetFlag(NNTP_PAUSE_FOR_READ);

    PR_Free(command);
    PR_Free(unescapedCommand);
  }
  else
  {
    m_nextState = NEWS_DONE;
  }
  return rv;
}

nsresult nsNNTPProtocol::XPATResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 1;
  nsresult rv;

  if (m_responseCode != MK_NNTP_RESPONSE_XPAT_OK)
  {
    AlertError(MK_NNTP_ERROR_MESSAGE,m_responseText);
    m_nextState = NNTP_ERROR;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_ERROR_FAILURE;
  }

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  NNTP_LOG_READ(line);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if (line)
  {
    if (line[0] != '.')
    {
      long articleNumber;
      PR_sscanf(line, "%ld", &articleNumber);
      nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(m_runningURL);
      if (mailnewsurl)
      {
        nsCOMPtr <nsIMsgSearchSession> searchSession;
        nsCOMPtr <nsIMsgSearchAdapter> searchAdapter;
        mailnewsurl->GetSearchSession(getter_AddRefs(searchSession));
        if (searchSession)
        {
          searchSession->GetRunningAdapter(getter_AddRefs(searchAdapter));
          if (searchAdapter)
            searchAdapter->AddHit((uint32_t) articleNumber);
        }
      }
    }
    else
    {
      /* set up the next term for next time around */
      int32_t slash = m_searchData.FindChar('/');

      if (slash >= 0)
        m_searchData.Cut(0, slash + 1);
      else
        m_searchData.Truncate();

      m_nextState = NNTP_XPAT_SEND;
      ClearFlag(NNTP_PAUSE_FOR_READ);
      PR_FREEIF(line);
      return NS_OK;
    }
  }
  PR_FREEIF(line);
  return NS_OK;
}

nsresult nsNNTPProtocol::ListPrettyNames()
{

  nsCString group_name;
  char outputBuffer[OUTPUT_BUFFER_SIZE];

  m_newsFolder->GetRawName(group_name);
  PR_snprintf(outputBuffer,
    OUTPUT_BUFFER_SIZE,
    "LIST PRETTYNAMES %.512s" CRLF,
    group_name.get());

  nsresult rv = SendData(outputBuffer);
  NNTP_LOG_NOTE(outputBuffer);
  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_LIST_PRETTY_NAMES_RESPONSE;

  return rv;
}

nsresult nsNNTPProtocol::ListPrettyNamesResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;

  if (m_responseCode != MK_NNTP_RESPONSE_LIST_OK)
  {
    m_nextState = DISPLAY_NEWSGROUPS;
    /*    m_nextState = NEWS_DONE; */
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

  NNTP_LOG_READ(line);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if (line)
  {
    if (line[0] != '.')
    {
#if 0 // SetPrettyName is not yet implemented. No reason to bother
      int i;
      /* find whitespace separator if it exits */
      for (i=0; line[i] != '\0' && !NET_IS_SPACE(line[i]); i++)
        ;  /* null body */

      char *prettyName;
      if(line[i] == '\0')
        prettyName = &line[i];
      else
        prettyName = &line[i+1];

      line[i] = 0; /* terminate group name */
      if (i > 0) {
        nsAutoCString charset;
        nsAutoString lineUtf16, prettyNameUtf16;
        if (NS_FAILED(m_nntpServer->GetCharset(charset) ||
            NS_FAILED(ConvertToUnicode(charset, line, lineUtf16)) ||
            NS_FAILED(ConvertToUnicode(charset, prettyName, prettyNameUtf16)))) {
          CopyUTF8toUTF16(line, lineUtf16);
          CopyUTF8toUTF16(prettyName, prettyNameUtf16);
        }
        m_nntpServer->SetPrettyNameForGroup(lineUtf16, prettyNameUtf16);

        MOZ_LOG(NNTP, LogLevel::Info,("(%p) adding pretty name %s", this,
               NS_ConvertUTF16toUTF8(prettyNameUtf16).get()));
      }
#endif
    }
    else
    {
      m_nextState = DISPLAY_NEWSGROUPS;  /* this assumes we were doing a list */
      /*      m_nextState = NEWS_DONE;   */ /* ### dmb - don't really know */
      ClearFlag(NNTP_PAUSE_FOR_READ);
      PR_FREEIF(line);
      return NS_OK;
    }
  }
  PR_FREEIF(line);
  return NS_OK;
}

nsresult nsNNTPProtocol::ListXActive()
{
  nsCString group_name;
  nsresult rv;
  rv = m_newsFolder->GetRawName(group_name);
  NS_ENSURE_SUCCESS(rv, rv);

  char outputBuffer[OUTPUT_BUFFER_SIZE];

  PR_snprintf(outputBuffer,
    OUTPUT_BUFFER_SIZE,
    "LIST XACTIVE %.512s" CRLF,
    group_name.get());

  rv = SendData(outputBuffer);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_LIST_XACTIVE_RESPONSE;

  return rv;
}

nsresult nsNNTPProtocol::ListXActiveResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;
  nsresult rv;

  NS_ASSERTION(m_responseCode == MK_NNTP_RESPONSE_LIST_OK, "code != LIST_OK");
  if (m_responseCode != MK_NNTP_RESPONSE_LIST_OK)
  {
    m_nextState = DISPLAY_NEWSGROUPS;
    /*    m_nextState = NEWS_DONE; */
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

  NNTP_LOG_READ(line);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

   /* almost correct */
  if(status > 1)
  {
    mBytesReceived += status;
    mBytesReceivedSinceLastStatusUpdate += status;
  }

  if (line)
  {
    if (line[0] != '.')
    {
      char *s = line;
      /* format is "rec.arts.movies.past-films 7302 7119 csp"
      */
      while (*s && !NET_IS_SPACE(*s))
        s++;
      if (*s)
      {
        char flags[32];  /* ought to be big enough */
        *s = 0;
        PR_sscanf(s + 1,
          "%d %d %31s",
          &m_firstPossibleArticle,
          &m_lastPossibleArticle,
          flags);


        NS_ASSERTION(m_nntpServer, "no nntp incoming server");
        if (m_nntpServer) {
          rv = m_nntpServer->AddNewsgroupToList(line);
          NS_ASSERTION(NS_SUCCEEDED(rv),"failed to add to subscribe ds");
        }

        /* we're either going to list prettynames first, or list
        all prettynames every time, so we won't care so much
        if it gets interrupted. */
        MOZ_LOG(NNTP, LogLevel::Info,("(%p) got xactive for %s of %s", this, line, flags));
        /*  This isn't required, because the extra info is
        initialized to false for new groups. And it's
        an expensive call.
        */
        /* MSG_SetGroupNeedsExtraInfo(cd->host, line, false); */
      }
    }
    else
    {
      bool xactive=false;
      rv = m_nntpServer->QueryExtension("XACTIVE",&xactive);
      if (m_typeWanted == NEW_GROUPS &&
        NS_SUCCEEDED(rv) && xactive)
      {
        nsCOMPtr <nsIMsgNewsFolder> old_newsFolder;
        old_newsFolder = m_newsFolder;
        nsCString groupName;

        rv = m_nntpServer->GetFirstGroupNeedingExtraInfo(groupName);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = m_nntpServer->FindGroup(groupName,
                                     getter_AddRefs(m_newsFolder));
        NS_ENSURE_SUCCESS(rv, rv);

        // see if we got a different group
        if (old_newsFolder && m_newsFolder &&
          (old_newsFolder.get() != m_newsFolder.get()))
          /* make sure we're not stuck on the same group */
        {
          MOZ_LOG(NNTP, LogLevel::Info,("(%p) listing xactive for %s", this, groupName.get()));
          m_nextState = NNTP_LIST_XACTIVE;
          ClearFlag(NNTP_PAUSE_FOR_READ);
          PR_FREEIF(line);
          return NS_OK;
        }
        else
        {
          m_newsFolder = nullptr;
        }
      }
      bool listpname;
      rv = m_nntpServer->QueryExtension("LISTPNAME",&listpname);
      if (NS_SUCCEEDED(rv) && listpname)
        m_nextState = NNTP_LIST_PRETTY_NAMES;
      else
        m_nextState = DISPLAY_NEWSGROUPS;  /* this assumes we were doing a list - who knows? */
      /*      m_nextState = NEWS_DONE;   */ /* ### dmb - don't really know */
      ClearFlag(NNTP_PAUSE_FOR_READ);
      PR_FREEIF(line);
      return NS_OK;
    }
  }
  PR_FREEIF(line);
  return NS_OK;
}

nsresult nsNNTPProtocol::SendListGroup()
{
  nsresult rv;
  char outputBuffer[OUTPUT_BUFFER_SIZE];

  NS_ASSERTION(m_newsFolder,"no newsFolder");
  if (!m_newsFolder) return NS_ERROR_FAILURE;
  nsCString newsgroupName;

  rv = m_newsFolder->GetRawName(newsgroupName);
  NS_ENSURE_SUCCESS(rv,rv);

  PR_snprintf(outputBuffer,
    OUTPUT_BUFFER_SIZE,
    "listgroup %.512s" CRLF,
    newsgroupName.get());

  m_articleList = do_CreateInstance(NS_NNTPARTICLELIST_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  rv = m_articleList->Initialize(m_newsFolder);
  NS_ENSURE_SUCCESS(rv,rv);

  rv = SendData(outputBuffer);

  m_nextState = NNTP_RESPONSE;
  m_nextStateAfterResponse = NNTP_LIST_GROUP_RESPONSE;
  SetFlag(NNTP_PAUSE_FOR_READ);

  return rv;
}

nsresult nsNNTPProtocol::SendListGroupResponse(nsIInputStream * inputStream, uint32_t length)
{
  uint32_t status = 0;

  NS_ASSERTION(m_responseCode == MK_NNTP_RESPONSE_GROUP_SELECTED, "code != GROUP_SELECTED");
  if (m_responseCode != MK_NNTP_RESPONSE_GROUP_SELECTED)
  {
    m_nextState = NEWS_DONE;
    ClearFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }

  if (line)
  {
    mozilla::DebugOnly<nsresult> rv;
    if (line[0] != '.')
    {
      nsMsgKey found_id = nsMsgKey_None;
      PR_sscanf(line, "%ld", &found_id);
      rv = m_articleList->AddArticleKey(found_id);
      NS_ASSERTION(NS_SUCCEEDED(rv), "add article key failed");
    }
    else
    {
      rv = m_articleList->FinishAddingArticleKeys();
      NS_ASSERTION(NS_SUCCEEDED(rv), "finish adding article key failed");
      m_articleList = nullptr;
      m_nextState = NEWS_DONE;   /* ### dmb - don't really know */
      ClearFlag(NNTP_PAUSE_FOR_READ);
      PR_FREEIF(line);
      return NS_OK;
    }
  }
  PR_FREEIF(line);
  return NS_OK;
}


nsresult nsNNTPProtocol::Search()
{
  NS_ERROR("Search not implemented");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsNNTPProtocol::SearchResponse()
{
  if (MK_NNTP_RESPONSE_TYPE(m_responseCode) == MK_NNTP_RESPONSE_TYPE_OK)
    m_nextState = NNTP_SEARCH_RESULTS;
  else
    m_nextState = NEWS_DONE;
  ClearFlag(NNTP_PAUSE_FOR_READ);
  return NS_OK;
}

nsresult nsNNTPProtocol::SearchResults(nsIInputStream *inputStream, uint32_t length)
{
  uint32_t status = 1;
  nsresult rv;

  bool pauseForMoreData = false;
  char *line = m_lineStreamBuffer->ReadNextLine(inputStream, status, pauseForMoreData, &rv);

  if(pauseForMoreData)
  {
    SetFlag(NNTP_PAUSE_FOR_READ);
    return NS_OK;
  }
  if (!line)
    return rv;  /* no line yet */

  if ('.' == line[0])
  {
    /* all overview lines received */
    m_nextState = NEWS_DONE;
    ClearFlag(NNTP_PAUSE_FOR_READ);
  }
  PR_FREEIF(line);
  return rv;
}

/* Sets state for the transfer. This used to be known as net_setup_news_stream */
nsresult nsNNTPProtocol::SetupForTransfer()
{
  if (m_typeWanted == NEWS_POST)
  {
    m_nextState = NNTP_SEND_POST_DATA;
  }
  else if(m_typeWanted == LIST_WANTED)
  {
    if (TestFlag(NNTP_USE_FANCY_NEWSGROUP))
      m_nextState = NNTP_LIST_XACTIVE_RESPONSE;
    else
      m_nextState = NNTP_READ_LIST_BEGIN;
  }
  else if(m_typeWanted == GROUP_WANTED)
    m_nextState = NNTP_XOVER_BEGIN;
  else if(m_typeWanted == NEW_GROUPS)
    m_nextState = NNTP_NEWGROUPS_BEGIN;
  else if(m_typeWanted == ARTICLE_WANTED ||
    m_typeWanted== CANCEL_WANTED)
    m_nextState = NNTP_BEGIN_ARTICLE;
  else if (m_typeWanted== SEARCH_WANTED)
    m_nextState = NNTP_XPAT_SEND;
  else
  {
    NS_ERROR("unexpected");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// The following method is used for processing the news state machine.
// It returns a negative number (mscott: we'll change this to be an enumerated type which we'll coordinate
// with the netlib folks?) when we are done processing.
//////////////////////////////////////////////////////////////////////////////////////////////////////////
nsresult nsNNTPProtocol::ProcessProtocolState(nsIURI * url, nsIInputStream * inputStream,
                                              uint64_t sourceOffset, uint32_t length)
{
  nsresult status = NS_OK;
  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(m_runningURL);
  if (inputStream && (!mailnewsurl || !m_nntpServer))
  {
    // In these cases, we are going to return since our data is effectively
    // invalid. However, nsInputStream would really rather that we at least read
    // some of our input data (even if not all of it). Therefore, we'll read a
    // little bit.
    char buffer[128];
    uint32_t readData = 0;
    inputStream->Read(buffer, 127, &readData);
    buffer[readData] = '\0';
    MOZ_LOG(NNTP, LogLevel::Debug, ("(%p) Ignoring data: %s", this, buffer));
  }

  if (!mailnewsurl)
    return NS_OK; // probably no data available - it's OK.

  if (!m_nntpServer)
  {
    // Parsing must result in our m_nntpServer being set, so we should never
    // have a case where m_nntpServer being false is safe. Most likely, we have
    // already closed our socket and we are merely flushing out the socket
    // receive queue. Since the user told us to stop, don't process any more
    // input.
    return inputStream ? inputStream->Close() : NS_OK;
  }

  ClearFlag(NNTP_PAUSE_FOR_READ);

  while(!TestFlag(NNTP_PAUSE_FOR_READ))
  {
    MOZ_LOG(NNTP, LogLevel::Info,("(%p) Next state: %s",this, stateLabels[m_nextState]));
    // examine our current state and call an appropriate handler for that state.....
    switch(m_nextState)
    {
    case NNTP_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = NewsResponse(inputStream, length);
      break;

      // mscott: I've removed the states involving connections on the assumption
      // that core netlib will now be managing that information.

    case NNTP_LOGIN_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = LoginResponse();
      break;

    case NNTP_SEND_MODE_READER:
      status = SendModeReader();
      break;

    case NNTP_SEND_MODE_READER_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendModeReaderResponse();
      break;

    case SEND_LIST_EXTENSIONS:
      status = SendListExtensions();
      break;
    case SEND_LIST_EXTENSIONS_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendListExtensionsResponse(inputStream, length);
      break;
    case SEND_LIST_SEARCHES:
      status = SendListSearches();
      break;
    case SEND_LIST_SEARCHES_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendListSearchesResponse(inputStream, length);
      break;
    case NNTP_LIST_SEARCH_HEADERS:
      status = SendListSearchHeaders();
      break;
    case NNTP_LIST_SEARCH_HEADERS_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendListSearchHeadersResponse(inputStream, length);
      break;
    case NNTP_GET_PROPERTIES:
      status = GetProperties();
      break;
    case NNTP_GET_PROPERTIES_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = GetPropertiesResponse(inputStream, length);
      break;
    case SEND_LIST_SUBSCRIPTIONS:
      status = SendListSubscriptions();
      break;
    case SEND_LIST_SUBSCRIPTIONS_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendListSubscriptionsResponse(inputStream, length);
      break;

    case SEND_FIRST_NNTP_COMMAND:
      status = SendFirstNNTPCommand(url);
      break;
    case SEND_FIRST_NNTP_COMMAND_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendFirstNNTPCommandResponse();
      break;

    case NNTP_SEND_GROUP_FOR_ARTICLE:
      status = SendGroupForArticle();
      break;
    case NNTP_SEND_GROUP_FOR_ARTICLE_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendGroupForArticleResponse();
      break;
    case NNTP_SEND_ARTICLE_NUMBER:
      status = SendArticleNumber();
      break;

    case SETUP_NEWS_STREAM:
      status = SetupForTransfer();
      break;

    case NNTP_BEGIN_AUTHORIZE:
      status = BeginAuthorization();
      break;

    case NNTP_AUTHORIZE_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = AuthorizationResponse();
      break;

    case NNTP_PASSWORD_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = PasswordResponse();
      break;

      // read list
    case NNTP_READ_LIST_BEGIN:
      status = BeginReadNewsList();
      break;
    case NNTP_READ_LIST:
      status = ReadNewsList(inputStream, length);
      break;

      // news group
    case DISPLAY_NEWSGROUPS:
      status = DisplayNewsgroups();
      break;
    case NNTP_NEWGROUPS_BEGIN:
      status = BeginNewsgroups();
      break;
    case NNTP_NEWGROUPS:
      status = ProcessNewsgroups(inputStream, length);
      break;

      // article specific
    case NNTP_BEGIN_ARTICLE:
      status = BeginArticle();
      break;

    case NNTP_READ_ARTICLE:
      status = ReadArticle(inputStream, length);
      break;

    case NNTP_XOVER_BEGIN:
      status = BeginReadXover();
      break;

    case NNTP_FIGURE_NEXT_CHUNK:
      status = FigureNextChunk();
      break;

    case NNTP_XOVER_SEND:
      status = XoverSend();
      break;

    case NNTP_XOVER:
      status = ReadXover(inputStream, length);
      break;

    case NNTP_XOVER_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = ReadXoverResponse();
      break;

    case NEWS_PROCESS_XOVER:
    case NEWS_PROCESS_BODIES:
      status = ProcessXover();
      break;

    case NNTP_XHDR_SEND:
      status = XhdrSend();
      break;

    case NNTP_XHDR_RESPONSE:
      status = XhdrResponse(inputStream);
      break;

    case NNTP_READ_GROUP:
      status = ReadHeaders();
      break;

    case NNTP_READ_GROUP_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = ReadNewsgroupResponse();
      break;

    case NNTP_READ_GROUP_BODY:
      status = ReadNewsgroupBody(inputStream, length);
      break;

    case NNTP_SEND_POST_DATA:
      status = PostData();
      break;
    case NNTP_SEND_POST_DATA_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = PostDataResponse();
      break;

    case NNTP_CHECK_FOR_MESSAGE:
      status = CheckForArticle();
      break;

      // cancel
    case NEWS_START_CANCEL:
      status = StartCancel();
      break;

    case NEWS_DO_CANCEL:
      status = DoCancel();
      break;

      // XPAT
    case NNTP_XPAT_SEND:
      status = XPATSend();
      break;
    case NNTP_XPAT_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = XPATResponse(inputStream, length);
      break;

      // search
    case NNTP_SEARCH:
      status = Search();
      break;
    case NNTP_SEARCH_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SearchResponse();
      break;
    case NNTP_SEARCH_RESULTS:
      status = SearchResults(inputStream, length);
      break;


    case NNTP_LIST_PRETTY_NAMES:
      status = ListPrettyNames();
      break;
    case NNTP_LIST_PRETTY_NAMES_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = ListPrettyNamesResponse(inputStream, length);
      break;
    case NNTP_LIST_XACTIVE:
      status = ListXActive();
      break;
    case NNTP_LIST_XACTIVE_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = ListXActiveResponse(inputStream, length);
      break;
    case NNTP_LIST_GROUP:
      status = SendListGroup();
      break;
    case NNTP_LIST_GROUP_RESPONSE:
      if (inputStream == nullptr)
        SetFlag(NNTP_PAUSE_FOR_READ);
      else
        status = SendListGroupResponse(inputStream, length);
      break;
    case NEWS_DONE:
      m_nextState = NEWS_FREE;
      break;
    case NEWS_POST_DONE:
      NNTP_LOG_NOTE("NEWS_POST_DONE");
      mailnewsurl->SetUrlState(false, NS_OK);
      m_nextState = NEWS_FREE;
      break;
    case NEWS_ERROR:
      NNTP_LOG_NOTE("NEWS_ERROR");
      if (m_responseCode == MK_NNTP_RESPONSE_ARTICLE_NOTFOUND || m_responseCode == MK_NNTP_RESPONSE_ARTICLE_NONEXIST)
        mailnewsurl->SetUrlState(false, NS_MSG_NEWS_ARTICLE_NOT_FOUND);
      else
        mailnewsurl->SetUrlState(false, NS_ERROR_FAILURE);
      m_nextState = NEWS_FREE;
      break;
    case NNTP_ERROR:
      // XXX do we really want to remove the connection from
      // the cache on error?
      /* check if this connection came from the cache or if it was
      * a new connection.  If it was not new lets start it over
      * again.  But only if we didn't have any successful protocol
      * dialog at all.
      */
      FinishMemCacheEntry(false);  // cleanup mem cache entry
      if (m_responseCode != MK_NNTP_RESPONSE_ARTICLE_NOTFOUND && m_responseCode != MK_NNTP_RESPONSE_ARTICLE_NONEXIST)
        return CloseConnection();
      MOZ_FALLTHROUGH;
    case NEWS_FREE:
      // Remember when we last used this connection
      m_lastActiveTimeStamp = PR_Now();
      CleanupAfterRunningUrl();
      MOZ_FALLTHROUGH;
    case NNTP_SUSPENDED:
      return NS_OK;
      break;
    default:
      /* big error */
      return NS_ERROR_FAILURE;

    } // end switch

    if (NS_FAILED(status) && m_nextState != NEWS_ERROR &&
        m_nextState != NNTP_ERROR && m_nextState != NEWS_FREE)
    {
      m_nextState = NNTP_ERROR;
      ClearFlag(NNTP_PAUSE_FOR_READ);
    }

  } /* end big while */

  return NS_OK; /* keep going */
}

NS_IMETHODIMP nsNNTPProtocol::CloseConnection()
{
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) ClosingConnection",this));
  SendData(NNTP_CMD_QUIT); // this will cause OnStopRequest get called, which will call CloseSocket()
  // break some cycles
  CleanupNewsgroupList();

  if (m_nntpServer) {
    m_nntpServer->RemoveConnection(this);
    m_nntpServer = nullptr;
  }
  CloseSocket();
  m_newsFolder = nullptr;

  if (m_articleList) {
    m_articleList->FinishAddingArticleKeys();
    m_articleList = nullptr;
  }

  m_key = nsMsgKey_None;
  return NS_OK;
}

nsresult nsNNTPProtocol::CleanupNewsgroupList()
{
    nsresult rv;
    if (!m_newsgroupList) return NS_OK;
  int32_t status = 0;
    rv = m_newsgroupList->FinishXOVERLINE(0,&status);
    m_newsgroupList = nullptr;
    NS_ASSERTION(NS_SUCCEEDED(rv), "FinishXOVERLINE failed");
    return rv;
}

nsresult nsNNTPProtocol::CleanupAfterRunningUrl()
{
  /* do we need to know if we're parsing xover to call finish xover?  */
  /* yes, I think we do! Why did I think we should??? */
  /* If we've gotten to NEWS_FREE and there is still XOVER
  data, there was an error or we were interrupted or
  something.  So, tell libmsg there was an abnormal
  exit so that it can free its data. */

  MOZ_LOG(NNTP, LogLevel::Info,("(%p) CleanupAfterRunningUrl()", this));

  // send StopRequest notification after we've cleaned up the protocol
  // because it can synchronously causes a new url to get run in the
  // protocol - truly evil, but we're stuck at the moment.
  if (m_channelListener)
    (void) m_channelListener->OnStopRequest(this, m_channelContext, NS_OK);

  if (m_loadGroup)
    (void) m_loadGroup->RemoveRequest(static_cast<nsIRequest *>(this), nullptr, NS_OK);
  CleanupNewsgroupList();

  // clear out mem cache entry so we're not holding onto it.
  if (m_runningURL)
  {
    nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(m_runningURL);
    if (mailnewsurl)
    {
      mailnewsurl->SetUrlState(false, NS_OK);
      mailnewsurl->SetMemCacheEntry(nullptr);
    }
  }

  Cleanup();

  mDisplayInputStream = nullptr;
  mDisplayOutputStream = nullptr;
  mProgressEventSink = nullptr;
  SetOwner(nullptr);

  m_channelContext = nullptr;
  m_channelListener = nullptr;
  m_loadGroup = nullptr;
  mCallbacks = nullptr;

  // disable timeout before caching.
  nsCOMPtr<nsISocketTransport> strans = do_QueryInterface(m_transport);
  if (strans)
    strans->SetTimeout(nsISocketTransport::TIMEOUT_READ_WRITE, PR_UINT32_MAX);

  // don't mark ourselves as not busy until we are done cleaning up the connection. it should be the
  // last thing we do.
  SetIsBusy(false);

  return NS_OK;
}

nsresult nsNNTPProtocol::CloseSocket()
{
  MOZ_LOG(NNTP, LogLevel::Info,("(%p) ClosingSocket()",this));

  if (m_nntpServer) {
    m_nntpServer->RemoveConnection(this);
    m_nntpServer = nullptr;
  }

  CleanupAfterRunningUrl(); // is this needed?
  return nsMsgProtocol::CloseSocket();
}

void nsNNTPProtocol::SetProgressBarPercent(uint32_t aProgress, uint32_t aProgressMax)
{
  // XXX 64-bit
  if (mProgressEventSink)
    mProgressEventSink->OnProgress(this, m_channelContext, uint64_t(aProgress),
                                   uint64_t(aProgressMax));
}

nsresult
nsNNTPProtocol::SetProgressStatus(const char16_t *aMessage)
{
  nsresult rv = NS_OK;
  if (mProgressEventSink)
    rv = mProgressEventSink->OnStatus(this, m_channelContext, NS_OK, aMessage);
  return rv;
}

NS_IMETHODIMP nsNNTPProtocol::GetContentType(nsACString &aContentType)
{

  // if we've been set with a content type, then return it....
  // this happens when we go through libmime now as it sets our new content type
  if (!mContentType.IsEmpty())
  {
    aContentType = mContentType;
    return NS_OK;
  }

  // otherwise do what we did before...

  if (m_typeWanted == GROUP_WANTED)
    aContentType.AssignLiteral("x-application-newsgroup");
  else if (m_typeWanted == IDS_WANTED)
    aContentType.AssignLiteral("x-application-newsgroup-listids");
  else
    aContentType.AssignLiteral("message/rfc822");
  return NS_OK;
}

nsresult
nsNNTPProtocol::AlertError(int32_t errorCode, const char *text)
{
  nsresult rv = NS_OK;

  // get the prompt from the running url....
  if (m_runningURL) {
    nsCOMPtr<nsIMsgMailNewsUrl> msgUrl (do_QueryInterface(m_runningURL));
    nsCOMPtr<nsIPrompt> dialog;
    rv = GetPromptDialogFromUrl(msgUrl, getter_AddRefs(dialog));
    NS_ENSURE_SUCCESS(rv, rv);

    nsString alertText;
    rv = GetNewsStringByID(MK_NNTP_ERROR_MESSAGE, getter_Copies(alertText));
    NS_ENSURE_SUCCESS(rv,rv);
    if (text) {
      alertText.Append(' ');
      alertText.Append(NS_ConvertASCIItoUTF16(text));
    }
    rv = dialog->Alert(nullptr, alertText.get());
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return rv;
}

NS_IMETHODIMP nsNNTPProtocol::GetCurrentFolder(nsIMsgFolder **aFolder)
{
  nsresult rv = NS_ERROR_NULL_POINTER;
  NS_ENSURE_ARG_POINTER(aFolder);
  if (m_newsFolder)
    rv = m_newsFolder->QueryInterface(NS_GET_IID(nsIMsgFolder), (void **) aFolder);
  return rv;
}

