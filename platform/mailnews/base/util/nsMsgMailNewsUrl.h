/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMsgMailNewsUrl_h___
#define nsMsgMailNewsUrl_h___

#include "nscore.h"
#include "nsISupports.h"
#include "nsIUrlListener.h"
#include "nsTObserverArray.h"
#include "nsIMsgWindow.h"
#include "nsIMsgStatusFeedback.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsIMimeHeaders.h"
#include "nsIMsgMailNewsUrl.h"
#include "nsIURL.h"
#include "nsIURIWithPrincipal.h"
#include "nsILoadGroup.h"
#include "nsIMsgSearchSession.h"
#include "nsICacheEntry.h"
#include "nsICacheSession.h"
#include "nsIMimeMiscStatus.h"
#include "nsWeakReference.h"
#include "nsStringGlue.h"

///////////////////////////////////////////////////////////////////////////////////
// Okay, I found that all of the mail and news url interfaces needed to support
// several common interfaces (in addition to those provided through nsIURI). 
// So I decided to group them all in this implementation so we don't have to
// duplicate the code.
//
//////////////////////////////////////////////////////////////////////////////////

class NS_MSG_BASE nsMsgMailNewsUrl : public nsIMsgMailNewsUrl,
                                     public nsIURIWithPrincipal
{
public:
    nsMsgMailNewsUrl();

    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSIMSGMAILNEWSURL
    NS_DECL_NSIURI
    NS_DECL_NSIURL
    NS_DECL_NSIURIWITHPRINCIPAL

protected:
  virtual ~nsMsgMailNewsUrl();

  nsCOMPtr<nsIURL> m_baseURL;
  nsCOMPtr<nsIPrincipal> m_principal;
  nsWeakPtr m_statusFeedbackWeak;
  nsWeakPtr m_msgWindowWeak;
  nsWeakPtr m_loadGroupWeak;
  nsCOMPtr<nsIMimeHeaders> mMimeHeaders;
  nsCOMPtr<nsIMsgSearchSession> m_searchSession;
  nsCOMPtr<nsICacheEntry> m_memCacheEntry;
  nsCOMPtr<nsIMsgHeaderSink> mMsgHeaderSink;
  char *m_errorMessage;
  int64_t mMaxProgress;
  bool m_runningUrl;
  bool m_updatingFolder;
  bool m_msgIsInLocalCache;
  bool m_suppressErrorMsgs;
  bool m_isPrincipalURL;

  // the following field is really a bit of a hack to make 
  // open attachments work. The external applications code sometimes tries to figure out the right
  // handler to use by looking at the file extension of the url we are trying to load. Unfortunately,
  // the attachment file name really isn't part of the url string....so we'll store it here...and if 
  // the url we are running is an attachment url, we'll set it here. Then when the helper apps code
  // asks us for it, we'll return the right value.
  nsCString mAttachmentFileName;

  nsTObserverArray<nsCOMPtr<nsIUrlListener> > mUrlListeners;
};

#endif /* nsMsgMailNewsUrl_h___ */
