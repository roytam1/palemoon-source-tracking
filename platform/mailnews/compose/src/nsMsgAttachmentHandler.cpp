/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIPrefLocalizedString.h"
#include "nsILineInputStream.h"
#include "nsMsgAttachmentHandler.h"
#include "prmem.h"
#include "nsMsgCopy.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsMsgSend.h"
#include "nsMsgCompUtils.h"
#include "nsMsgI18N.h"
#include "nsURLFetcher.h"
#include "nsMimeTypes.h"
#include "nsMsgCompCID.h"
#include "nsIMsgMessageService.h"
#include "nsMsgUtils.h"
#include "nsMsgPrompts.h"
#include "nsTextFormatter.h"
#include "nsIPrompt.h"
#include "nsITextToSubURI.h"
#include "nsIURL.h"
#include "nsIFileURL.h"
#include "nsNetCID.h"
#include "nsIMimeStreamConverter.h"
#include "nsMsgMimeCID.h"
#include "nsNetUtil.h"
#include "nsNativeCharsetUtils.h"
#include "nsComposeStrings.h"
#include "nsIZipWriter.h"
#include "nsIDirectoryEnumerator.h"
#include "mozilla/Services.h"
#include "mozilla/mailnews/MimeEncoder.h"
#include "nsIPrincipal.h"

//
// Class implementation...
//
nsMsgAttachmentHandler::nsMsgAttachmentHandler() :
  mRequest(nullptr),
  mCompFields(nullptr),   // Message composition fields for the sender
  m_bogus_attachment(false),
  m_done(false),
  m_already_encoded_p(false),
  mDeleteFile(false),
  mMHTMLPart(false),
  mPartUserOmissionOverride(false),
  mMainBody(false),
  mSendViaCloud(false),
  mNodeIndex(-1),
  // For analyzing the attachment file...
  m_size(0),
  m_unprintable_count(0),
  m_highbit_count(0),
  m_ctl_count(0),
  m_null_count(0),
  m_have_cr(0), 
  m_have_lf(0), 
  m_have_crlf(0),
  m_prev_char_was_cr(false),
  m_current_column(0),
  m_max_column(0),
  m_lines(0),
  m_file_analyzed(false),

  // Mime
  m_encoder(nullptr)
{
}

nsMsgAttachmentHandler::~nsMsgAttachmentHandler()
{
  if (mTmpFile && mDeleteFile)
    mTmpFile->Remove(false);

  if (mOutFile)
    mOutFile->Close();

  CleanupTempFile();
}

NS_IMPL_ISUPPORTS(nsMsgAttachmentHandler, nsIMsgAttachmentHandler)

// nsIMsgAttachmentHandler implementation.

NS_IMETHODIMP nsMsgAttachmentHandler::GetType(nsACString& aType)
{
  aType.Assign(m_type);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetUri(nsACString& aUri)
{
  nsAutoCString turl;
  if (!mURL)
  {
    if (!m_uri.IsEmpty())
      turl = m_uri;
  }
  else
  {
    nsresult rv = mURL->GetSpec(turl);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  aUri.Assign(turl);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetTmpFile(nsIFile **aTmpFile)
{
  NS_ENSURE_ARG_POINTER(aTmpFile);
  if (!mTmpFile)
    return NS_ERROR_FAILURE;
  NS_ADDREF(*aTmpFile = mTmpFile);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetName(nsACString& aName)
{
  aName.Assign(m_realName);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetSize(uint32_t *aSize)
{
  NS_ENSURE_ARG_POINTER(aSize);
  *aSize = m_size;
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetContentId(nsACString& aContentId)
{
  aContentId.Assign(m_contentId);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetSendViaCloud(bool* aSendViaCloud)
{
  NS_ENSURE_ARG_POINTER(aSendViaCloud);
  *aSendViaCloud = mSendViaCloud;
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetCharset(nsACString& aCharset)
{
  aCharset.Assign(m_charset);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetEncoding(nsACString& aEncoding)
{
  aEncoding.Assign(m_encoding);
  return NS_OK;
}

NS_IMETHODIMP nsMsgAttachmentHandler::GetAlreadyEncoded(bool* aAlreadyEncoded)
{
  NS_ENSURE_ARG_POINTER(aAlreadyEncoded);
  *aAlreadyEncoded = m_already_encoded_p;
  return NS_OK;
}

// Local methods.

void
nsMsgAttachmentHandler::CleanupTempFile()
{
/** Mac Stub **/
}

void
nsMsgAttachmentHandler::AnalyzeDataChunk(const char *chunk, int32_t length)
{
  unsigned char *s = (unsigned char *) chunk;
  unsigned char *end = s + length;
  for (; s < end; s++)
  {
    if (*s > 126)
    {
      m_highbit_count++;
      m_unprintable_count++;
    }
    else if (*s < ' ' && *s != '\t' && *s != '\r' && *s != '\n')
    {
      m_unprintable_count++;
      m_ctl_count++;
      if (*s == 0)
        m_null_count++;
    }

    if (*s == '\r' || *s == '\n')
    {
      if (*s == '\r')
      {
        if (m_prev_char_was_cr)
          m_have_cr = 1;
        else
          m_prev_char_was_cr = true;
      }
      else
      {
        if (m_prev_char_was_cr)
        {
          if (m_current_column == 0)
          {
            m_have_crlf = 1;
            m_lines--;
          }
          else
            m_have_cr = m_have_lf = 1;
          m_prev_char_was_cr = false;
        }
        else
          m_have_lf = 1;
      }
      if (m_max_column < m_current_column)
        m_max_column = m_current_column;
      m_current_column = 0;
      m_lines++;
    }
    else
    {
      m_current_column++;
    }
  }
  // Check one last time for the last line. This is also important if there
  // is only one line that doesn't terminate in \n.
  if (m_max_column < m_current_column)
    m_max_column = m_current_column;
}

void
nsMsgAttachmentHandler::AnalyzeSnarfedFile(void)
{
  char chunk[1024];
  uint32_t numRead = 0;

  if (m_file_analyzed)
    return;

  if (mTmpFile)
  {
    int64_t fileSize;
    mTmpFile->GetFileSize(&fileSize);
    m_size = (uint32_t) fileSize;
    nsCOMPtr <nsIInputStream> inputFile;
    nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputFile), mTmpFile);
    if (NS_FAILED(rv))
      return;
    {
      do
      {
        rv = inputFile->Read(chunk, sizeof(chunk), &numRead);
        if (numRead)
          AnalyzeDataChunk(chunk, numRead);
      }
      while (numRead && NS_SUCCEEDED(rv));
      if (m_prev_char_was_cr)
        m_have_cr = 1;

      inputFile->Close();
      m_file_analyzed = true;
    }
  }
}

//
// Given a content-type and some info about the contents of the document,
// decide what encoding it should have.
//
nsresult
nsMsgAttachmentHandler::PickEncoding(const char *charset, nsIMsgSend *mime_delivery_state)
{
  nsCOMPtr<nsIPrefBranch> pPrefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID));

  bool needsB64 = false;
  bool forceB64 = false;
  bool isUsingQP = false;

  if (mSendViaCloud)
  {
    m_encoding = ENCODING_7BIT;
    return NS_OK;
  }
  if (m_already_encoded_p)
    goto DONE;

  AnalyzeSnarfedFile();

  // Allow users to override our percentage-wise guess on whether
  // the file is text or binary.
  if (pPrefBranch)
    pPrefBranch->GetBoolPref ("mail.file_attach_binary", &forceB64);

  // If the content-type is "image/" or something else known to be binary or
  // several flavors of newlines are present, use base64 unless we're attaching
  // a message (so that we don't get confused by newline conversions).
  if (!mMainBody &&
      (forceB64 || mime_type_requires_b64_p(m_type.get()) ||
                   m_have_cr + m_have_lf + m_have_crlf != 1) &&
      !m_type.LowerCaseEqualsLiteral(MESSAGE_RFC822))
  {
    needsB64 = true;
  }
  else
  {
    // Otherwise, we need to pick an encoding based on the contents of the
    // document.
    bool encode_p;
    bool force_p = false;

    // Force quoted-printable if the sender does not allow conversion to 7bit.
    if (mCompFields) {
      if (mCompFields->GetForceMsgEncoding())
        force_p = true;
    } else if (mime_delivery_state) {
      if (((nsMsgComposeAndSend *)mime_delivery_state)->mCompFields->GetForceMsgEncoding()) {
        force_p = true;
      }
    }

    if (force_p || (m_max_column > LINELENGTH_ENCODING_THRESHOLD)) {
      encode_p = true;
    } else if (UseQuotedPrintable() && m_unprintable_count) {
      encode_p = true;
    } else if (m_null_count) {
      // If there are nulls, we must always encode, because sendmail will
      // blow up.
      encode_p = true;
    } else {
      encode_p = false;
    }

    // MIME requires a special case that these types never be encoded.
    if (StringBeginsWith(m_type, NS_LITERAL_CSTRING("message"),
                         nsCaseInsensitiveCStringComparator()) ||
        StringBeginsWith(m_type, NS_LITERAL_CSTRING("multipart"),
                         nsCaseInsensitiveCStringComparator()))
    {
      encode_p = false;
      if (m_desiredType.LowerCaseEqualsLiteral(TEXT_PLAIN))
        m_desiredType.Truncate();
    }

    // If the Mail charset is multibyte, we force it to use Base64 for attachments.
    if ((!mMainBody && charset && nsMsgI18Nmultibyte_charset(charset)) &&
        (m_type.LowerCaseEqualsLiteral(TEXT_HTML) ||
         m_type.LowerCaseEqualsLiteral(TEXT_MDL) ||
         m_type.LowerCaseEqualsLiteral(TEXT_PLAIN) ||
         m_type.LowerCaseEqualsLiteral(TEXT_RICHTEXT) ||
         m_type.LowerCaseEqualsLiteral(TEXT_ENRICHED) ||
         m_type.LowerCaseEqualsLiteral(TEXT_VCARD) ||
         m_type.LowerCaseEqualsLiteral(APPLICATION_DIRECTORY) || /* text/x-vcard synonym */
         m_type.LowerCaseEqualsLiteral(TEXT_CSS) ||
         m_type.LowerCaseEqualsLiteral(TEXT_JSSS))) {
      needsB64 = true;
    } else if (charset && nsMsgI18Nstateful_charset(charset)) {
      m_encoding = ENCODING_7BIT;
    } else if (encode_p &&
      m_unprintable_count > (m_size / 10)) {
      // If the document contains more than 10% unprintable characters,
      // then that seems like a good candidate for base64 instead of
      // quoted-printable.
      needsB64 = true;
    } else if (encode_p) {
      m_encoding = ENCODING_QUOTED_PRINTABLE;
      isUsingQP = true;
    } else if (m_highbit_count > 0) {
      m_encoding = ENCODING_8BIT;
    } else {
      m_encoding = ENCODING_7BIT;
    }
  }

  // Always base64 binary data.
  if (needsB64)
    m_encoding = ENCODING_BASE64;

  // According to RFC 821 we must always have lines shorter than 998 bytes.
  // To encode "long lines" use a CTE that will transmit shorter lines.
  // Switch to base64 if we are not already using "quoted printable".

  // We don't do this for message/rfc822 attachments, since we can't
  // change the original Content-Transfer-Encoding of the message we're
  // attaching. We rely on the original message complying with RFC 821,
  // if it doesn't we won't either. Not ideal.
  if (!m_type.LowerCaseEqualsLiteral(MESSAGE_RFC822) &&
      m_max_column > LINELENGTH_ENCODING_THRESHOLD && !isUsingQP)
    m_encoding = ENCODING_BASE64;

  // Now that we've picked an encoding, initialize the filter.
  NS_ASSERTION(!m_encoder, "not-null m_encoder");
  if (m_encoding.LowerCaseEqualsLiteral(ENCODING_BASE64))
  {
    m_encoder = MimeEncoder::GetBase64Encoder(mime_encoder_output_fn,
      mime_delivery_state);
  }
  else if (m_encoding.LowerCaseEqualsLiteral(ENCODING_QUOTED_PRINTABLE))
  {
    m_encoder = MimeEncoder::GetQPEncoder(mime_encoder_output_fn,
      mime_delivery_state);
  }
  else
  {
    m_encoder = nullptr;
  }

  /* Do some cleanup for documents with unknown content type.
    There are two issues: how they look to MIME users, and how they look to
    non-MIME users.

      If the user attaches a "README" file, which has unknown type because it
      has no extension, we still need to send it with no encoding, so that it
      is readable to non-MIME users.

        But if the user attaches some random binary file, then base64 encoding
        will have been chosen for it (above), and in this case, it won't be
        immediately readable by non-MIME users.  However, if we type it as
        text/plain instead of application/octet-stream, it will show up inline
        in a MIME viewer, which will probably be ugly, and may possibly have
        bad charset things happen as well.

          So, the heuristic we use is, if the type is unknown, then the type is
          set to application/octet-stream for data which needs base64 (binary data)
          and is set to text/plain for data which didn't need base64 (unencoded or
          lightly encoded data.)
  */
DONE:
  if (m_type.IsEmpty() || m_type.LowerCaseEqualsLiteral(UNKNOWN_CONTENT_TYPE))
  {
    if (m_already_encoded_p)
      m_type = APPLICATION_OCTET_STREAM;
    else if (m_encoding.LowerCaseEqualsLiteral(ENCODING_BASE64) ||
             m_encoding.LowerCaseEqualsLiteral(ENCODING_UUENCODE))
      m_type = APPLICATION_OCTET_STREAM;
    else
      m_type = TEXT_PLAIN;
  }
  return NS_OK;
}

nsresult
nsMsgAttachmentHandler::PickCharset()
{
  if (!m_charset.IsEmpty() ||
      !StringBeginsWith(m_type, NS_LITERAL_CSTRING("text/"),
                        nsCaseInsensitiveCStringComparator()))
    return NS_OK;

  nsCOMPtr<nsIFile> tmpFile =
    do_QueryInterface(mTmpFile);
  if (!tmpFile)
    return NS_OK;
  
  return MsgDetectCharsetFromFile(tmpFile, m_charset);
}
    
static nsresult
FetcherURLDoneCallback(nsresult aStatus,
                       const nsACString &aContentType,
                       const nsACString &aCharset,
                       int32_t totalSize,
                       const char16_t* aMsg, void *tagData)
{
  nsMsgAttachmentHandler *ma = (nsMsgAttachmentHandler *) tagData;
  NS_ASSERTION(ma != nullptr, "not-null mime attachment");

  if (ma != nullptr)
  {
    ma->m_size = totalSize;
    if (!aContentType.IsEmpty())
    {
      // can't send appledouble on non-macs
      if (!aContentType.EqualsLiteral("multipart/appledouble"))
        ma->m_type = aContentType;
    }

    if (!aCharset.IsEmpty())
      ma->m_charset = aCharset;

    return ma->UrlExit(aStatus, aMsg);
  }
  else
    return NS_OK;
}

nsresult
nsMsgAttachmentHandler::SnarfMsgAttachment(nsMsgCompFields *compFields)
{
  nsresult rv = NS_ERROR_INVALID_ARG;
  nsCOMPtr <nsIMsgMessageService> messageService;

  if (m_uri.Find("-message:", CaseInsensitiveCompare) != -1)
  {
    nsCOMPtr <nsIFile> tmpFile;
    rv = nsMsgCreateTempFile("nsmail.tmp", getter_AddRefs(tmpFile));
    NS_ENSURE_SUCCESS(rv, rv);
    mTmpFile = do_QueryInterface(tmpFile);
    mDeleteFile = true;
    mCompFields = compFields;
    m_type = MESSAGE_RFC822;
    m_overrideType = MESSAGE_RFC822;
    if (!mTmpFile)
    {
      rv = NS_ERROR_FAILURE;
      goto done;
    }

    rv = MsgNewBufferedFileOutputStream(getter_AddRefs(mOutFile), mTmpFile, -1, 00600);
    if (NS_FAILED(rv) || !mOutFile)
    {
      if (m_mime_delivery_state)
      {
        nsCOMPtr<nsIMsgSendReport> sendReport;
        m_mime_delivery_state->GetSendReport(getter_AddRefs(sendReport));
        if (sendReport)
        {
          nsAutoString error_msg;
          nsMsgBuildMessageWithTmpFile(mTmpFile, error_msg);
          sendReport->SetMessage(nsIMsgSendReport::process_Current, error_msg.get(), false);
        }
      }
      rv =  NS_MSG_UNABLE_TO_OPEN_TMP_FILE;
      goto done;
    }

    nsCOMPtr<nsIURLFetcher> fetcher = do_CreateInstance(NS_URLFETCHER_CONTRACTID, &rv);
    if (NS_FAILED(rv) || !fetcher)
    {
      if (NS_SUCCEEDED(rv))
        rv =  NS_ERROR_UNEXPECTED;
      goto done;
    }

    rv = fetcher->Initialize(mTmpFile, mOutFile, FetcherURLDoneCallback, this);
    rv = GetMessageServiceFromURI(m_uri, getter_AddRefs(messageService));
    if (NS_SUCCEEDED(rv) && messageService)
    {
      nsAutoCString uri(m_uri);
      uri += (uri.FindChar('?') == kNotFound) ? '?' : '&';
      uri.Append("fetchCompleteMessage=true");
      nsCOMPtr<nsIStreamListener> strListener;
      fetcher->QueryInterface(NS_GET_IID(nsIStreamListener), getter_AddRefs(strListener));

      // initialize a new stream converter, that uses the strListener as its input
      // obtain the input stream listener from the new converter,
      // and pass the converter's input stream listener to DisplayMessage

      m_mime_parser = do_CreateInstance(NS_MAILNEWS_MIME_STREAM_CONVERTER_CONTRACTID, &rv);
      if (NS_FAILED(rv))
        goto done;

      // Set us as the output stream for HTML data from libmime...
      nsCOMPtr<nsIMimeStreamConverter> mimeConverter = do_QueryInterface(m_mime_parser);
      if (mimeConverter)
      {
        mimeConverter->SetMimeOutputType(nsMimeOutput::nsMimeMessageDecrypt);
        mimeConverter->SetForwardInline(false);
        mimeConverter->SetIdentity(nullptr);
        mimeConverter->SetOriginalMsgURI(nullptr);
      }

      nsCOMPtr<nsIStreamListener> convertedListener = do_QueryInterface(m_mime_parser, &rv);
      if (NS_FAILED(rv))
        goto done;

      nsCOMPtr<nsIURI> aURL;
      rv = messageService->GetUrlForUri(uri.get(), getter_AddRefs(aURL), nullptr);
      if (NS_FAILED(rv))
        goto done;


      nsCOMPtr<nsIPrincipal> nullPrincipal =
        do_CreateInstance("@mozilla.org/nullprincipal;1", &rv);
      NS_ASSERTION(NS_SUCCEEDED(rv), "CreateInstance of nullprincipal failed.");
      if (NS_FAILED(rv))
        goto done;

      rv = NS_NewInputStreamChannel(getter_AddRefs(m_converter_channel),
                                    aURL,
                                    nullptr,
                                    nullPrincipal,
                                    nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_DATA_IS_NULL,
                                    nsIContentPolicy::TYPE_OTHER);
      if (NS_FAILED(rv))
        goto done;

      rv = m_mime_parser->AsyncConvertData("message/rfc822", "message/rfc822",
                                           strListener, m_converter_channel);
      if (NS_FAILED(rv))
        goto done;

      nsCOMPtr<nsIURI> dummyNull;
      rv = messageService->DisplayMessage(uri.get(), convertedListener, nullptr, nullptr, nullptr,
                                          getter_AddRefs(dummyNull));
    }
  }
done:
  if (NS_FAILED(rv))
  {
      if (mOutFile)
      {
        mOutFile->Close();
        mOutFile = nullptr;
      }

      if (mTmpFile)
      {
        mTmpFile->Remove(false);
        mTmpFile = nullptr;
      }
  }

  return rv;
}

nsresult
nsMsgAttachmentHandler::SnarfAttachment(nsMsgCompFields *compFields)
{
  NS_ASSERTION (! m_done, "Already done");

  if (!mURL)
    return SnarfMsgAttachment(compFields);

  mCompFields = compFields;

  // First, get as file spec and create the stream for the
  // temp file where we will save this data
  nsCOMPtr <nsIFile> tmpFile;
  nsresult rv = nsMsgCreateTempFile("nsmail.tmp", getter_AddRefs(tmpFile));
  NS_ENSURE_SUCCESS(rv, rv);
  mTmpFile = do_QueryInterface(tmpFile);
  mDeleteFile = true;

  rv = MsgNewBufferedFileOutputStream(getter_AddRefs(mOutFile), mTmpFile, -1, 00600);
  if (NS_FAILED(rv) || !mOutFile)
  {
    if (m_mime_delivery_state)
    {
      nsCOMPtr<nsIMsgSendReport> sendReport;
      m_mime_delivery_state->GetSendReport(getter_AddRefs(sendReport));
      if (sendReport)
      {
        nsAutoString error_msg;
        nsMsgBuildMessageWithTmpFile(mTmpFile, error_msg);
        sendReport->SetMessage(nsIMsgSendReport::process_Current, error_msg.get(), false);
      }
    }
    mTmpFile->Remove(false);
    mTmpFile = nullptr;
    return NS_MSG_UNABLE_TO_OPEN_TMP_FILE;
  }

  nsCString sourceURISpec;
  rv = mURL->GetSpec(sourceURISpec);
  NS_ENSURE_SUCCESS(rv, rv);

  //
  // Ok, here we are, we need to fire the URL off and get the data
  // in the temp file
  //
  // Create a fetcher for the URL attachment...
  
  nsCOMPtr<nsIURLFetcher> fetcher = do_CreateInstance(NS_URLFETCHER_CONTRACTID, &rv);
  if (NS_FAILED(rv) || !fetcher)
  {
    if (NS_SUCCEEDED(rv))
      return NS_ERROR_UNEXPECTED;
    else
      return rv;
  }

  return fetcher->FireURLRequest(mURL, mTmpFile, mOutFile, FetcherURLDoneCallback, this);
}

nsresult
nsMsgAttachmentHandler::LoadDataFromFile(nsIFile *file, nsString &sigData, bool charsetConversion)
{
  int32_t       readSize;
  char          *readBuf;

  nsCOMPtr <nsIInputStream> inputFile;
  nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputFile), file);
  if (NS_FAILED(rv))
    return NS_MSG_ERROR_WRITING_FILE;

  int64_t fileSize;
  file->GetFileSize(&fileSize);
  readSize = (uint32_t) fileSize;

  readBuf = (char *)PR_Malloc(readSize + 1);
  if (!readBuf)
    return NS_ERROR_OUT_OF_MEMORY;
  memset(readBuf, 0, readSize + 1);

  uint32_t bytesRead;
  inputFile->Read(readBuf, readSize, &bytesRead);
  inputFile->Close();

  nsDependentCString cstringReadBuf(readBuf, bytesRead);
  if (charsetConversion)
  {
    if (NS_FAILED(ConvertToUnicode(m_charset.get(), cstringReadBuf, sigData)))
      CopyASCIItoUTF16(cstringReadBuf, sigData);
  }
  else
    CopyASCIItoUTF16(cstringReadBuf, sigData);

  PR_FREEIF(readBuf);
  return NS_OK;
}

nsresult
nsMsgAttachmentHandler::Abort()
{
  nsCOMPtr<nsIRequest> saveRequest;
  saveRequest.swap(mRequest);

  if (mTmpFile)
  {
    if (mDeleteFile)
      mTmpFile->Remove(false);
    mTmpFile = nullptr;
  }

  NS_ASSERTION(m_mime_delivery_state != nullptr, "not-null m_mime_delivery_state");

  if (m_done)
    return NS_OK;

  if (saveRequest)
    return saveRequest->Cancel(NS_ERROR_ABORT);
  else
    if (m_mime_delivery_state)
    {
      m_mime_delivery_state->SetStatus(NS_ERROR_ABORT);
      m_mime_delivery_state->NotifyListenerOnStopSending(nullptr, NS_ERROR_ABORT, 0, nullptr);
    }
  return NS_OK;

}

nsresult
nsMsgAttachmentHandler::UrlExit(nsresult status, const char16_t* aMsg)
{
  NS_ASSERTION(m_mime_delivery_state != nullptr, "not-null m_mime_delivery_state");

  // Close the file, but don't delete the disk file (or the file spec.)
  if (mOutFile)
  {
    mOutFile->Close();
    mOutFile = nullptr;
  }
  // this silliness is because Windows nsIFile caches its file size
  // so if an output stream writes to it, it will still return the original
  // cached size.
  if (mTmpFile)
  {
    nsCOMPtr <nsIFile> tmpFile;
    mTmpFile->Clone(getter_AddRefs(tmpFile));
    mTmpFile = do_QueryInterface(tmpFile);
  }
  mRequest = nullptr;

  // First things first, we are now going to see if this is an HTML
  // Doc and if it is, we need to see if we can determine the charset
  // for this part by sniffing the HTML file.
  // This is needed only when the charset is not set already.
  // (e.g. a charset may be specified in HTTP header)
  //
  if (!m_type.IsEmpty() && m_charset.IsEmpty() &&
      m_type.LowerCaseEqualsLiteral(TEXT_HTML))
    m_charset = nsMsgI18NParseMetaCharset(mTmpFile);

  nsresult mimeDeliveryStatus;
  m_mime_delivery_state->GetStatus(&mimeDeliveryStatus);

  if (mimeDeliveryStatus == NS_ERROR_ABORT)
    status = NS_ERROR_ABORT;

  // If the attachment is empty, let's call that a failure.
  if (!m_size)
    status = NS_ERROR_FAILURE;

  if (NS_FAILED(status) && status != NS_ERROR_ABORT && NS_SUCCEEDED(mimeDeliveryStatus))
  {
    // At this point, we should probably ask a question to the user
    // if we should continue without this attachment.
    //
    bool              keepOnGoing = true;
    nsCString    turl;
    nsString     msg;
    nsresult rv;
    nsCOMPtr<nsIStringBundleService> bundleService =
      mozilla::services::GetStringBundleService();
    NS_ENSURE_TRUE(bundleService, NS_ERROR_UNEXPECTED);
    nsCOMPtr<nsIStringBundle> bundle;
    rv = bundleService->CreateBundle("chrome://messenger/locale/messengercompose/composeMsgs.properties", getter_AddRefs(bundle));
    NS_ENSURE_SUCCESS(rv, rv);
    nsMsgDeliverMode mode = nsIMsgSend::nsMsgDeliverNow;
    m_mime_delivery_state->GetDeliveryMode(&mode);
    nsCString params;
    if (!m_realName.IsEmpty())
      params = m_realName;
    else if (NS_SUCCEEDED(mURL->GetSpec(turl)) && !turl.IsEmpty())
    {
      nsAutoCString unescapedUrl;
      MsgUnescapeString(turl, 0, unescapedUrl);
      if (unescapedUrl.IsEmpty())
        params = turl;
      else
        params = unescapedUrl;
    }
    else
      params.AssignLiteral("?");

    NS_ConvertUTF8toUTF16 UTF16params(params);
    const char16_t* formatParams[] = { UTF16params.get() };
    if (mode == nsIMsgSend::nsMsgSaveAsDraft || mode == nsIMsgSend::nsMsgSaveAsTemplate)
      bundle->FormatStringFromName(u"failureOnObjectEmbeddingWhileSaving",
                                   formatParams, 1, getter_Copies(msg));
    else
      bundle->FormatStringFromName(u"failureOnObjectEmbeddingWhileSending",
                                   formatParams, 1, getter_Copies(msg));

    nsCOMPtr<nsIPrompt> aPrompt;
    if (m_mime_delivery_state)
      m_mime_delivery_state->GetDefaultPrompt(getter_AddRefs(aPrompt));
    nsMsgAskBooleanQuestionByString(aPrompt, msg.get(), &keepOnGoing);

    if (keepOnGoing)
    {
      status = NS_OK;
      m_bogus_attachment = true; //That will cause this attachment to be ignored.
    }
    else
    {
      status = NS_ERROR_ABORT;
      m_mime_delivery_state->SetStatus(status);
      nsresult ignoreMe;
      m_mime_delivery_state->Fail(status, nullptr, &ignoreMe);
      m_mime_delivery_state->NotifyListenerOnStopSending(nullptr, status, 0, nullptr);
      SetMimeDeliveryState(nullptr);
      return status;
    }
  }

  m_done = true;

  //
  // Ok, now that we have the file here on disk, we need to see if there was
  // a need to do conversion to plain text...if so, the magic happens here,
  // otherwise, just move on to other attachments...
  //
  if (NS_SUCCEEDED(status) && !m_type.LowerCaseEqualsLiteral(TEXT_PLAIN) &&
      m_desiredType.LowerCaseEqualsLiteral(TEXT_PLAIN))
  {
    //
    // Conversion to plain text desired.
    // Now use the converter service here to do the right
    // thing and convert this data to plain text for us!
    //
    nsAutoString      conData;

    if (NS_SUCCEEDED(LoadDataFromFile(mTmpFile, conData, true)))
    {
      bool flowed, delsp, formatted, disallowBreaks;
      GetSerialiserFlags(m_charset.get(), &flowed, &delsp, &formatted, &disallowBreaks);

      if (NS_SUCCEEDED(ConvertBufToPlainText(conData, flowed, delsp, formatted, disallowBreaks)))
      {
        if (mDeleteFile)
          mTmpFile->Remove(false);

        nsCOMPtr<nsIOutputStream> outputStream;
        nsresult rv = MsgNewBufferedFileOutputStream(getter_AddRefs(outputStream), mTmpFile,
                                                     PR_WRONLY | PR_CREATE_FILE, 00600);

        if (NS_SUCCEEDED(rv))
        {
          nsAutoCString tData;
          if (NS_FAILED(ConvertFromUnicode(m_charset.get(), conData, tData)))
            LossyCopyUTF16toASCII(conData, tData);
          if (!tData.IsEmpty())
          {
            uint32_t bytesWritten;
            (void) outputStream->Write(tData.get(), tData.Length(), &bytesWritten);
          }
          outputStream->Close();
          // this silliness is because Windows nsIFile caches its file size
          // so if an output stream writes to it, it will still return the original
          // cached size.
          if (mTmpFile)
          {
            nsCOMPtr <nsIFile> tmpFile;
            mTmpFile->Clone(getter_AddRefs(tmpFile));
            mTmpFile = do_QueryInterface(tmpFile);
          }

        }
      }
    }

    m_type = m_desiredType;
    m_desiredType.Truncate();
    m_encoding.Truncate();
  }

  uint32_t pendingAttachmentCount = 0;
  m_mime_delivery_state->GetPendingAttachmentCount(&pendingAttachmentCount);
  NS_ASSERTION (pendingAttachmentCount > 0, "no more pending attachment");

  m_mime_delivery_state->SetPendingAttachmentCount(pendingAttachmentCount - 1);

  bool processAttachmentsSynchronously = false;
  m_mime_delivery_state->GetProcessAttachmentsSynchronously(&processAttachmentsSynchronously);
  if (NS_SUCCEEDED(status) && processAttachmentsSynchronously)
  {
    /* Find the next attachment which has not yet been loaded,
     if any, and start it going.
     */
    uint32_t i;
    nsMsgAttachmentHandler *next = 0;
    nsTArray<RefPtr<nsMsgAttachmentHandler>> *attachments;

    m_mime_delivery_state->GetAttachmentHandlers(&attachments);

    for (i = 0; i < attachments->Length(); i++)
    {
      if (!(*attachments)[i]->m_done)
      {
        next = (*attachments)[i];
        //
        // rhp: We need to get a little more understanding to failed URL
        // requests. So, at this point if most of next is NULL, then we
        // should just mark it fetched and move on! We probably ignored
        // this earlier on in the send process.
        //
        if ( (!next->mURL) && (next->m_uri.IsEmpty()) )
        {
          (*attachments)[i]->m_done = true;
          (*attachments)[i]->SetMimeDeliveryState(nullptr);
          m_mime_delivery_state->GetPendingAttachmentCount(&pendingAttachmentCount);
          m_mime_delivery_state->SetPendingAttachmentCount(pendingAttachmentCount - 1);
          next->mPartUserOmissionOverride = true;
          next = nullptr;
          continue;
        }

        break;
      }
    }

    if (next)
    {
      nsresult status = next->SnarfAttachment(mCompFields);
      if (NS_FAILED(status))
      {
        nsresult ignoreMe;
        m_mime_delivery_state->Fail(status, nullptr, &ignoreMe);
        m_mime_delivery_state->NotifyListenerOnStopSending(nullptr, status, 0, nullptr);
        SetMimeDeliveryState(nullptr);
        return NS_ERROR_UNEXPECTED;
      }
    }
  }

  m_mime_delivery_state->GetPendingAttachmentCount(&pendingAttachmentCount);
  if (pendingAttachmentCount == 0)
  {
    // If this is the last attachment, then either complete the
    // delivery (if successful) or report the error by calling
    // the exit routine and terminating the delivery.
    if (NS_FAILED(status))
    {
      nsresult ignoreMe;
      m_mime_delivery_state->Fail(status, aMsg, &ignoreMe);
      m_mime_delivery_state->NotifyListenerOnStopSending(nullptr, status, aMsg, nullptr);
      SetMimeDeliveryState(nullptr);
      return NS_ERROR_UNEXPECTED;
    }
    else
    {
      status = m_mime_delivery_state->GatherMimeAttachments ();
      if (NS_FAILED(status))
      {
        nsresult ignoreMe;
        m_mime_delivery_state->Fail(status, aMsg, &ignoreMe);
        m_mime_delivery_state->NotifyListenerOnStopSending(nullptr, status, aMsg, nullptr);
        SetMimeDeliveryState(nullptr);
        return NS_ERROR_UNEXPECTED;
      }
    }
  }
  else
  {
    // If this is not the last attachment, but it got an error,
    // then report that error and continue
    if (NS_FAILED(status))
    {
      nsresult ignoreMe;
      m_mime_delivery_state->Fail(status, aMsg, &ignoreMe);
    }
  }

  SetMimeDeliveryState(nullptr);
  return NS_OK;
}


nsresult
nsMsgAttachmentHandler::GetMimeDeliveryState(nsIMsgSend** _retval)
{
  NS_ENSURE_ARG(_retval);
  *_retval = m_mime_delivery_state;
  NS_IF_ADDREF(*_retval);
  return NS_OK;
}

nsresult
nsMsgAttachmentHandler::SetMimeDeliveryState(nsIMsgSend* mime_delivery_state)
{
  /*
    Because setting m_mime_delivery_state to null could destroy ourself as
    m_mime_delivery_state it's our parent, we need to protect ourself against
    that!

    This extra comptr is necessary,
    see bug http://bugzilla.mozilla.org/show_bug.cgi?id=78967
  */
  nsCOMPtr<nsIMsgSend> temp = m_mime_delivery_state; /* Should lock our parent until the end of the function */
  m_mime_delivery_state = mime_delivery_state;
  return NS_OK;
}
