/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsMsgAttachmentHandler_H_
#define _nsMsgAttachmentHandler_H_

#include "nsIURL.h"
#include "nsMsgCompFields.h"
#include "nsIMsgStatusFeedback.h"
#include "nsIChannel.h"
#include "nsIMsgSend.h"
#include "nsIFileStreams.h"
#include "nsIStreamConverter.h"
#include "nsAutoPtr.h"
#include "nsIMsgAttachmentHandler.h"

namespace mozilla {
namespace mailnews {
class MimeEncoder;
}
}

//
// This is a class that deals with processing remote attachments. It implements
// an nsIStreamListener interface to deal with incoming data
//
class nsMsgAttachmentHandler : public nsIMsgAttachmentHandler
{

  typedef mozilla::mailnews::MimeEncoder MimeEncoder;
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIMSGATTACHMENTHANDLER

  nsMsgAttachmentHandler();
public:
  nsresult              SnarfAttachment(nsMsgCompFields *compFields);
  nsresult              PickEncoding(const char *charset, nsIMsgSend* mime_delivery_state);
  nsresult              PickCharset();
  void                  AnalyzeSnarfedFile ();      // Analyze a previously-snarfed file.
                                                    // (Currently only used for plaintext
                                                    // converted from HTML.) 
  nsresult              Abort();
  nsresult              UrlExit(nsresult status, const char16_t* aMsg);
  
  // if there's an intermediate temp file left, takes care to remove it from disk.
  //
  // NOTE: this takes care of the mEncodedWorkingFile temp file, but not mTmpFile which seems
  // to be used by lots of other classes at the moment.
  void                  CleanupTempFile();

private:
  virtual ~nsMsgAttachmentHandler();

  // use when a message (e.g. original message in a reply) is attached as a rfc822 attachment.
  nsresult              SnarfMsgAttachment(nsMsgCompFields *compFields);
  bool                  UseUUEncode_p(void);
  void                  AnalyzeDataChunk (const char *chunk, int32_t chunkSize);
  nsresult              LoadDataFromFile(nsIFile *file, nsString &sigData, bool charsetConversion); //A similar function already exist in nsMsgCompose!

  //
public:
  nsCOMPtr<nsIURI> mURL;
  nsCOMPtr<nsIFile>        mTmpFile;         // The temp file to which we save it 
  nsCOMPtr<nsIOutputStream>  mOutFile;          
  nsCOMPtr<nsIRequest> mRequest; // The live request used while fetching an attachment
  nsMsgCompFields       *mCompFields;       // Message composition fields for the sender
  bool                  m_bogus_attachment; // This is to catch problem children...
  
  nsCString m_xMacType;      // Mac file type
  nsCString m_xMacCreator;   // Mac file creator

  bool m_done;
  nsCString m_charset;         // charset name 
  nsCString m_contentId;      // This is for mutipart/related Content-ID's
  nsCString m_type;            // The real type, once we know it.
  nsCString m_typeParam;      // Any addition parameters to add to the content-type (other than charset, macType and maccreator)
  nsCString m_overrideType;   // The type we should assume it to be
                                            // or 0, if we should get it from the
                                            // server)
  nsCString m_overrideEncoding; // Goes along with override_type 

  nsCString m_desiredType;    // The type it should be converted to. 
  nsCString m_description;     // For Content-Description header
  nsCString m_realName;       // The name for the headers, if different
                                            // from the URL. 
  nsCString m_encoding;        // The encoding, once we've decided. */
  bool                  m_already_encoded_p; // If we attach a document that is already
                                             // encoded, we just pass it through.

  bool                  mDeleteFile;      // If this is true, Delete the file...its 
                                          // NOT the original file!

  bool                  mMHTMLPart;           // This is true if its an MHTML part, otherwise, false
  bool                  mPartUserOmissionOverride;  // This is true if the user send send the email without this part
  bool                  mMainBody;            // True if this is a main body.
   // true if this should be sent as a link to a file.
  bool                  mSendViaCloud;
  nsString              mHtmlAnnotation;
  nsCString             mCloudProviderKey;
  nsCString             mCloudUrl;
  int32_t mNodeIndex; //If this is an embedded image, this is the index of the
                      // corresponding domNode in the editor's
                      //GetEmbeddedObjects. Otherwise, it will be -1.
  //
  // Vars for analyzing file data...
  //
  uint32_t              m_size;         /* Some state used while filtering it */
  uint32_t              m_unprintable_count;
  uint32_t              m_highbit_count;
  uint32_t              m_ctl_count;
  uint32_t              m_null_count;
  uint8_t               m_have_cr, m_have_lf, m_have_crlf; 
  bool                  m_prev_char_was_cr;
  uint32_t              m_current_column;
  uint32_t              m_max_column;
  uint32_t              m_lines;
  bool                  m_file_analyzed;

  nsAutoPtr<MimeEncoder> m_encoder;
  nsCString             m_uri; // original uri string

  nsresult              GetMimeDeliveryState(nsIMsgSend** _retval);
  nsresult              SetMimeDeliveryState(nsIMsgSend* mime_delivery_state);
private:
  nsCOMPtr<nsIMsgSend>  m_mime_delivery_state;
  nsCOMPtr<nsIStreamConverter> m_mime_parser;
  nsCOMPtr<nsIChannel>  m_converter_channel;
};


#endif /* _nsMsgAttachmentHandler_H_ */
