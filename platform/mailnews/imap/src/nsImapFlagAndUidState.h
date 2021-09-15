/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsImapFlagAndUidState_h___
#define nsImapFlagAndUidState_h___

#include "MailNewsTypes.h"
#include "nsTArray.h"
#include "nsIImapFlagAndUidState.h"
#include "mozilla/Mutex.h"

const int32_t kImapFlagAndUidStateSize =	100;

#include "nsBaseHashtable.h"
#include "nsDataHashtable.h"

class nsImapFlagAndUidState : public nsIImapFlagAndUidState
{
public:
    NS_DECL_THREADSAFE_ISUPPORTS
    explicit nsImapFlagAndUidState(int numberOfMessages);

    NS_DECL_NSIIMAPFLAGANDUIDSTATE

    int32_t               NumberOfDeletedMessages();
    
    imapMessageFlagsType  GetMessageFlagsFromUID(uint32_t uid, bool *foundIt, int32_t *ndx);

    bool         IsLastMessageUnseen(void);
    bool         GetPartialUIDFetch() {return fPartialUIDFetch;}
    void         SetPartialUIDFetch(bool isPartial) {fPartialUIDFetch = isPartial;}
    uint32_t     GetHighestNonDeletedUID();
    uint16_t     GetSupportedUserFlags() { return fSupportedUserFlags; }

private:
  virtual ~nsImapFlagAndUidState();

    nsTArray<nsMsgKey>      fUids;
    nsTArray<imapMessageFlagsType> fFlags;
    // Hash table, mapping uids to extra flags
    nsDataHashtable<nsUint32HashKey, nsCString> m_customFlagsHash;
    // Hash table, mapping UID+customAttributeName to customAttributeValue.
    nsDataHashtable<nsCStringHashKey, nsCString> m_customAttributesHash;
    uint16_t                fSupportedUserFlags;
    int32_t                 fNumberDeleted;
    bool                    fPartialUIDFetch;
    mozilla::Mutex mLock;
};




#endif
