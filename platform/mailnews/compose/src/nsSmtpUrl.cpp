/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "msgCore.h"

#include "nsIURI.h"
#include "nsNetCID.h"
#include "nsSmtpUrl.h"
#include "nsStringGlue.h"
#include "nsMsgUtils.h"
#include "nsIMimeConverter.h"
#include "nsMsgMimeCID.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsCRT.h"
#include "nsAutoPtr.h"

/////////////////////////////////////////////////////////////////////////////////////
// mailto url definition
/////////////////////////////////////////////////////////////////////////////////////
nsMailtoUrl::nsMailtoUrl()
{
  mFormat = nsIMsgCompFormat::Default;
  m_baseURL = do_CreateInstance(NS_SIMPLEURI_CONTRACTID);
}

nsMailtoUrl::~nsMailtoUrl()
{
}

NS_IMPL_ISUPPORTS(nsMailtoUrl, nsIMailtoUrl, nsIURI)

static void UnescapeAndConvert(nsIMimeConverter *mimeConverter,
                               const nsACString &escaped, nsACString &out)
{
  NS_ASSERTION(mimeConverter, "Set mimeConverter before calling!");
  // If the string is empty, do absolutely nothing.
  if (escaped.IsEmpty())
    return;

  MsgUnescapeString(escaped, 0, out);
  nsAutoCString decodedString;
  nsresult rv = mimeConverter->DecodeMimeHeaderToUTF8(out, "UTF_8", false,
    true, decodedString);
  if (NS_SUCCEEDED(rv) && !decodedString.IsEmpty())
    out = decodedString;
}

nsresult nsMailtoUrl::ParseMailtoUrl(char * searchPart)
{
  char *rest = searchPart;
  nsCString escapedInReplyToPart;
  nsCString escapedToPart;
  nsCString escapedCcPart;
  nsCString escapedSubjectPart;
  nsCString escapedNewsgroupPart;
  nsCString escapedNewsHostPart;
  nsCString escapedReferencePart;
  nsCString escapedBodyPart;
  nsCString escapedBccPart;
  nsCString escapedFollowUpToPart;
  nsCString escapedFromPart;
  nsCString escapedHtmlPart;
  nsCString escapedOrganizationPart;
  nsCString escapedReplyToPart;
  nsCString escapedPriorityPart;

  // okay, first, free up all of our old search part state.....
  CleanupMailtoState();
  // m_toPart has the escaped address from before the query string, copy it
  // over so we can add on any additional to= addresses and unescape them all.
  escapedToPart = m_toPart;

  if (rest && *rest == '?')
  {
    /* start past the '?' */
    rest++;
  }

  if (rest)
  {
    char *token = NS_strtok("&", &rest);
    while (token && *token)
    {
      char *value = 0;
      char *eq = PL_strchr(token, '=');
      if (eq)
      {
        value = eq+1;
        *eq = 0;
      }

      nsCString decodedName;
      MsgUnescapeString(nsDependentCString(token), 0, decodedName);

      if (decodedName.IsEmpty())
        break;

      switch (NS_ToUpper(decodedName.First()))
      {
        /* DO NOT support attachment= in mailto urls. This poses a security fire hole!!! 
                          case 'A':
                          if (!PL_strcasecmp (token, "attachment"))
                          m_attachmentPart = value;
                          break;
                     */
      case 'B':
        if (decodedName.LowerCaseEqualsLiteral("bcc"))
        {
          if (!escapedBccPart.IsEmpty())
          {
            escapedBccPart += ", ";
            escapedBccPart += value;
          }
          else
            escapedBccPart = value; 
        }
        else if (decodedName.LowerCaseEqualsLiteral("body"))
        {
          if (!escapedBodyPart.IsEmpty())
          {
            escapedBodyPart +="\n";
            escapedBodyPart += value;
          }
          else
            escapedBodyPart = value;
        }
        break;
      case 'C': 
        if (decodedName.LowerCaseEqualsLiteral("cc"))
        {
          if (!escapedCcPart.IsEmpty())
          {
            escapedCcPart += ", ";
            escapedCcPart += value;
          }
          else
            escapedCcPart = value;
        }
        break;
      case 'F': 
        if (decodedName.LowerCaseEqualsLiteral("followup-to"))
          escapedFollowUpToPart = value;
        else if (decodedName.LowerCaseEqualsLiteral("from"))
          escapedFromPart = value;
        break;
      case 'H':
        if (decodedName.LowerCaseEqualsLiteral("html-part") ||
            decodedName.LowerCaseEqualsLiteral("html-body"))
        {
          // escapedHtmlPart holds the body for both html-part and html-body.
          escapedHtmlPart = value;
          mFormat = nsIMsgCompFormat::HTML;
        }
        break;
      case 'I':
        if (decodedName.LowerCaseEqualsLiteral("in-reply-to"))
          escapedInReplyToPart = value;
        break;

      case 'N':
        if (decodedName.LowerCaseEqualsLiteral("newsgroups"))
          escapedNewsgroupPart = value;
        else if (decodedName.LowerCaseEqualsLiteral("newshost"))
          escapedNewsHostPart = value;
        break;
      case 'O':
        if (decodedName.LowerCaseEqualsLiteral("organization"))
          escapedOrganizationPart = value;
        break;
      case 'R':
        if (decodedName.LowerCaseEqualsLiteral("references"))
          escapedReferencePart = value;
        else if (decodedName.LowerCaseEqualsLiteral("reply-to"))
          escapedReplyToPart = value;
        break;
      case 'S':
        if(decodedName.LowerCaseEqualsLiteral("subject"))
          escapedSubjectPart = value;
        break;
      case 'P':
        if (decodedName.LowerCaseEqualsLiteral("priority"))
          escapedPriorityPart = PL_strdup(value);
        break;
      case 'T':
        if (decodedName.LowerCaseEqualsLiteral("to"))
        {
          if (!escapedToPart.IsEmpty())
          {
            escapedToPart += ", ";
            escapedToPart += value;
          }
          else
            escapedToPart = value;
        }
        break;
      default:
        break;
      } // end of switch statement...

      if (eq)
        *eq = '='; /* put it back */
      token = NS_strtok("&", &rest);
    } // while we still have part of the url to parse...
  } // if rest && *rest

  // Get a global converter
  nsCOMPtr<nsIMimeConverter> mimeConverter =
    do_GetService(NS_MIME_CONVERTER_CONTRACTID);

  // Now unescape everything, and mime-decode the things that can be encoded.
  UnescapeAndConvert(mimeConverter, escapedToPart, m_toPart);
  UnescapeAndConvert(mimeConverter, escapedCcPart, m_ccPart);
  UnescapeAndConvert(mimeConverter, escapedBccPart, m_bccPart);
  UnescapeAndConvert(mimeConverter, escapedSubjectPart, m_subjectPart);
  UnescapeAndConvert(mimeConverter, escapedNewsgroupPart, m_newsgroupPart);
  UnescapeAndConvert(mimeConverter, escapedReferencePart, m_referencePart);
  if (!escapedBodyPart.IsEmpty())
    MsgUnescapeString(escapedBodyPart, 0, m_bodyPart);
  if (!escapedHtmlPart.IsEmpty())
    MsgUnescapeString(escapedHtmlPart, 0, m_htmlPart);
  UnescapeAndConvert(mimeConverter, escapedNewsHostPart, m_newsHostPart);
  UnescapeAndConvert(mimeConverter, escapedFollowUpToPart, m_followUpToPart);
  UnescapeAndConvert(mimeConverter, escapedFromPart, m_fromPart);
  UnescapeAndConvert(mimeConverter, escapedOrganizationPart, m_organizationPart);
  UnescapeAndConvert(mimeConverter, escapedReplyToPart, m_replyToPart);
  UnescapeAndConvert(mimeConverter, escapedPriorityPart, m_priorityPart);

  nsCString inReplyToPart; // Not a member like the others...
  UnescapeAndConvert(mimeConverter, escapedInReplyToPart, inReplyToPart);

  if (!inReplyToPart.IsEmpty())
  {
    // Ensure that References and In-Reply-To are consistent... The last
    // reference will be used as In-Reply-To header.
    if (m_referencePart.IsEmpty())
    {
      // If References is not set, set it to be the In-Reply-To.
      m_referencePart = inReplyToPart;
    }
    else
    {
      // References is set. Add the In-Reply-To as last header unless it's
      // set as last reference already.
      int32_t lastRefStart = m_referencePart.RFindChar('<');
      nsAutoCString lastReference;
      if (lastRefStart != -1)
        lastReference = StringTail(m_referencePart, lastRefStart);
      else
        lastReference = m_referencePart;

      if (lastReference != inReplyToPart)
      {
        m_referencePart += " ";
        m_referencePart += inReplyToPart;
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP nsMailtoUrl::SetSpec(const nsACString &aSpec)
{
  nsresult rv = m_baseURL->SetSpec(aSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  return ParseUrl();
}

nsresult nsMailtoUrl::CleanupMailtoState()
{
    m_ccPart = "";
    m_subjectPart = "";
    m_newsgroupPart = "";
    m_newsHostPart = ""; 
    m_referencePart = "";
    m_bodyPart = "";
    m_bccPart = "";
    m_followUpToPart = "";
    m_fromPart = "";
    m_htmlPart = "";
    m_organizationPart = "";
    m_replyToPart = "";
    m_priorityPart = "";
    return NS_OK;
}

nsresult nsMailtoUrl::ParseUrl()
{
  // we can get the path from the simple url.....
  nsCString escapedPath;
  m_baseURL->GetPath(escapedPath);

  int32_t startOfSearchPart = escapedPath.FindChar('?');
  if (startOfSearchPart >= 0)
  {
    // now parse out the search field...
    nsAutoCString searchPart(Substring(escapedPath, startOfSearchPart));

    if (!searchPart.IsEmpty())
    {
      // now we need to strip off the search part from the
      // to part....
      escapedPath.SetLength(startOfSearchPart);
      MsgUnescapeString(escapedPath, 0, m_toPart);
      ParseMailtoUrl(searchPart.BeginWriting());
    }
  }
  else if (!escapedPath.IsEmpty())
  {
    MsgUnescapeString(escapedPath, 0, m_toPart);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetMessageContents(nsACString &aToPart, nsACString &aCcPart,
                                nsACString &aBccPart, nsACString &aSubjectPart,
                                nsACString &aBodyPart, nsACString &aHtmlPart,
                                nsACString &aReferencePart,
                                nsACString &aNewsgroupPart,
                                MSG_ComposeFormat *aFormat)
{
  NS_ENSURE_ARG_POINTER(aFormat);

  aToPart = m_toPart;
  aCcPart = m_ccPart;
  aBccPart = m_bccPart;
  aSubjectPart = m_subjectPart;
  aBodyPart = m_bodyPart;
  aHtmlPart = m_htmlPart;
  aReferencePart = m_referencePart;
  aNewsgroupPart = m_newsgroupPart;
  *aFormat = mFormat;
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetFromPart(nsACString &aResult)
{
  aResult = m_fromPart;
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetFollowUpToPart(nsACString &aResult)
{
  aResult = m_followUpToPart;
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetOrganizationPart(nsACString &aResult)
{
  aResult = m_organizationPart;
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetReplyToPart(nsACString &aResult)
{
  aResult = m_replyToPart;
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetPriorityPart(nsACString &aResult)
{
  aResult = m_priorityPart;
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::GetNewsHostPart(nsACString &aResult)
{
  aResult = m_newsHostPart;
  return NS_OK;
}

//////////////////////////////////////////////////////////////////////////////
// Begin nsIURI support
//////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP nsMailtoUrl::GetSpec(nsACString &aSpec)
{
	return m_baseURL->GetSpec(aSpec);
}

NS_IMETHODIMP nsMailtoUrl::GetPrePath(nsACString &aPrePath)
{
	return m_baseURL->GetPrePath(aPrePath);
}

NS_IMETHODIMP nsMailtoUrl::GetScheme(nsACString &aScheme)
{
	return m_baseURL->GetScheme(aScheme);
}

NS_IMETHODIMP nsMailtoUrl::SetScheme(const nsACString &aScheme)
{
	m_baseURL->SetScheme(aScheme);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetUserPass(nsACString &aUserPass)
{
	return m_baseURL->GetUserPass(aUserPass);
}

NS_IMETHODIMP nsMailtoUrl::SetUserPass(const nsACString &aUserPass)
{
	m_baseURL->SetUserPass(aUserPass);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetUsername(nsACString &aUsername)
{
	return m_baseURL->GetUsername(aUsername);
}

NS_IMETHODIMP nsMailtoUrl::SetUsername(const nsACString &aUsername)
{
	m_baseURL->SetUsername(aUsername);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetPassword(nsACString &aPassword)
{
	return m_baseURL->GetPassword(aPassword);
}

NS_IMETHODIMP nsMailtoUrl::SetPassword(const nsACString &aPassword)
{
	m_baseURL->SetPassword(aPassword);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetHostPort(nsACString &aHostPort)
{
	return m_baseURL->GetHost(aHostPort);
}

NS_IMETHODIMP nsMailtoUrl::SetHostPort(const nsACString &aHostPort)
{
	m_baseURL->SetHost(aHostPort);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::SetHostAndPort(const nsACString &aHostPort)
{
	m_baseURL->SetHostAndPort(aHostPort);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetHost(nsACString &aHost)
{
	return m_baseURL->GetHost(aHost);
}

NS_IMETHODIMP nsMailtoUrl::SetHost(const nsACString &aHost)
{
	m_baseURL->SetHost(aHost);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetPort(int32_t *aPort)
{
	return m_baseURL->GetPort(aPort);
}

NS_IMETHODIMP nsMailtoUrl::SetPort(int32_t aPort)
{
	m_baseURL->SetPort(aPort);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetPath(nsACString &aPath)
{
	return m_baseURL->GetPath(aPath);
}

NS_IMETHODIMP nsMailtoUrl::SetPath(const nsACString &aPath)
{
	m_baseURL->SetPath(aPath);
	return ParseUrl();
}

NS_IMETHODIMP nsMailtoUrl::GetAsciiHost(nsACString &aHostA)
{
	return m_baseURL->GetAsciiHost(aHostA);
}

NS_IMETHODIMP nsMailtoUrl::GetAsciiHostPort(nsACString &aHostPortA)
{
	return m_baseURL->GetAsciiHostPort(aHostPortA);
}

NS_IMETHODIMP nsMailtoUrl::GetAsciiSpec(nsACString &aSpecA)
{
	return m_baseURL->GetAsciiSpec(aSpecA);
}

NS_IMETHODIMP nsMailtoUrl::GetOriginCharset(nsACString &aOriginCharset)
{
    return m_baseURL->GetOriginCharset(aOriginCharset);
}

NS_IMETHODIMP nsMailtoUrl::SchemeIs(const char *aScheme, bool *_retval)
{
	return m_baseURL->SchemeIs(aScheme, _retval);
}

NS_IMETHODIMP nsMailtoUrl::Equals(nsIURI *other, bool *_retval)
{
  // The passed-in URI might be an nsMailtoUrl. Pass our inner URL to its
  // Equals method. The other nsMailtoUrl will then pass its inner URL to
  // to the Equals method of our inner URL. Other URIs will return false.
  if (other)
    return other->Equals(m_baseURL, _retval);

  return m_baseURL->Equals(other, _retval);
}

nsresult
nsMailtoUrl::CloneInternal(RefHandlingEnum aRefHandlingMode,
                           const nsACString& newRef, nsIURI** _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);

  RefPtr<nsMailtoUrl> clone = new nsMailtoUrl();

  NS_ENSURE_TRUE(clone, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv;
  if (aRefHandlingMode == eHonorRef) {
    rv = m_baseURL->Clone(getter_AddRefs(clone->m_baseURL));
  } else if (aRefHandlingMode == eReplaceRef) {
    rv = m_baseURL->CloneWithNewRef(newRef, getter_AddRefs(clone->m_baseURL));
  } else {
    rv = m_baseURL->CloneIgnoringRef(getter_AddRefs(clone->m_baseURL));
  }
  NS_ENSURE_SUCCESS(rv, rv);
  clone->ParseUrl();
  clone.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsMailtoUrl::Clone(nsIURI **_retval)
{
  return CloneInternal(eHonorRef, EmptyCString(), _retval);
}

NS_IMETHODIMP
nsMailtoUrl::CloneIgnoringRef(nsIURI** _retval)
{
  return CloneInternal(eIgnoreRef, EmptyCString(), _retval);
}

NS_IMETHODIMP
nsMailtoUrl::CloneWithNewRef(const nsACString& newRef, nsIURI** _retval)
{
  return CloneInternal(eReplaceRef, newRef, _retval);
}

NS_IMETHODIMP nsMailtoUrl::Resolve(const nsACString &relativePath, nsACString &result)
{
  return m_baseURL->Resolve(relativePath, result);
}

NS_IMETHODIMP nsMailtoUrl::SetRef(const nsACString &aRef)
{
  return m_baseURL->SetRef(aRef);
}

NS_IMETHODIMP
nsMailtoUrl::GetRef(nsACString &result)
{
  return m_baseURL->GetRef(result);
}

NS_IMETHODIMP nsMailtoUrl::EqualsExceptRef(nsIURI *other, bool *result)
{
  // The passed-in URI might be an nsMailtoUrl. Pass our inner URL to its
  // Equals method. The other nsMailtoUrl will then pass its inner URL to
  // to the Equals method of our inner URL. Other URIs will return false.
  if (other)
    return other->EqualsExceptRef(m_baseURL, result);

  return m_baseURL->EqualsExceptRef(other, result);
}

NS_IMETHODIMP
nsMailtoUrl::GetSpecIgnoringRef(nsACString &result)
{
  return m_baseURL->GetSpecIgnoringRef(result);
}

NS_IMETHODIMP
nsMailtoUrl::GetHasRef(bool *result)
{
  return m_baseURL->GetHasRef(result);
}

NS_IMETHODIMP
nsMailtoUrl::GetFilePath(nsACString &aFilePath)
{
  return m_baseURL->GetFilePath(aFilePath);
}

NS_IMETHODIMP
nsMailtoUrl::SetFilePath(const nsACString &aFilePath)
{
  return m_baseURL->SetFilePath(aFilePath);
}

NS_IMETHODIMP
nsMailtoUrl::GetQuery(nsACString &aQuery)
{
  return m_baseURL->GetQuery(aQuery);
}

NS_IMETHODIMP
nsMailtoUrl::SetQuery(const nsACString &aQuery)
{
  return m_baseURL->SetQuery(aQuery);
}

/////////////////////////////////////////////////////////////////////////////////////
// smtp url definition
/////////////////////////////////////////////////////////////////////////////////////

nsSmtpUrl::nsSmtpUrl() : nsMsgMailNewsUrl()
{
  // nsISmtpUrl specific state...

  m_isPostMessage = true;
  m_requestDSN = false;
  m_verifyLogon = false;
}
 
nsSmtpUrl::~nsSmtpUrl()
{
}
  
NS_IMPL_ISUPPORTS_INHERITED(nsSmtpUrl, nsMsgMailNewsUrl, nsISmtpUrl)  

////////////////////////////////////////////////////////////////////////////////////
// Begin nsISmtpUrl specific support

////////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP
nsSmtpUrl::SetRecipients(const char * aRecipientsList)
{
  NS_ENSURE_ARG(aRecipientsList);
  MsgUnescapeString(nsDependentCString(aRecipientsList), 0, m_toPart);
  return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetRecipients(char ** aRecipientsList)
{
  NS_ENSURE_ARG_POINTER(aRecipientsList);
  if (aRecipientsList)
    *aRecipientsList = ToNewCString(m_toPart);
  return NS_OK;
}

NS_IMPL_GETSET(nsSmtpUrl, PostMessage, bool, m_isPostMessage)

NS_IMPL_GETSET(nsSmtpUrl, VerifyLogon, bool, m_verifyLogon)

// the message can be stored in a file....allow accessors for getting and setting
// the file name to post...
NS_IMETHODIMP nsSmtpUrl::SetPostMessageFile(nsIFile * aFile)
{
  NS_ENSURE_ARG_POINTER(aFile);
  m_fileName = aFile;
  return NS_OK;
}

NS_IMETHODIMP nsSmtpUrl::GetPostMessageFile(nsIFile ** aFile)
{
  NS_ENSURE_ARG_POINTER(aFile);
  if (m_fileName)
  {
    // Clone the file so nsLocalFile stat caching doesn't make the caller get
    // the wrong file size.
    m_fileName->Clone(aFile);
    return *aFile ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_ERROR_NULL_POINTER;
}

NS_IMPL_GETSET(nsSmtpUrl, RequestDSN, bool, m_requestDSN)

NS_IMETHODIMP 
nsSmtpUrl::SetDsnEnvid(const nsACString &aDsnEnvid)
{
    m_dsnEnvid = aDsnEnvid;
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetDsnEnvid(nsACString &aDsnEnvid)
{
    aDsnEnvid = m_dsnEnvid;
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetSenderIdentity(nsIMsgIdentity **aSenderIdentity)
{
  NS_ENSURE_ARG_POINTER(aSenderIdentity);
  *aSenderIdentity = m_senderIdentity;
  NS_ADDREF(*aSenderIdentity);
  return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::SetSenderIdentity(nsIMsgIdentity *aSenderIdentity)
{
  NS_ENSURE_ARG_POINTER(aSenderIdentity);
  m_senderIdentity = aSenderIdentity;
  return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::SetPrompt(nsIPrompt *aNetPrompt)
{
    NS_ENSURE_ARG_POINTER(aNetPrompt);
    m_netPrompt = aNetPrompt;
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetPrompt(nsIPrompt **aNetPrompt)
{
    NS_ENSURE_ARG_POINTER(aNetPrompt);
    NS_ENSURE_TRUE(m_netPrompt, NS_ERROR_NULL_POINTER);
    *aNetPrompt = m_netPrompt;
    NS_ADDREF(*aNetPrompt);
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::SetAuthPrompt(nsIAuthPrompt *aNetAuthPrompt)
{
    NS_ENSURE_ARG_POINTER(aNetAuthPrompt);
    m_netAuthPrompt = aNetAuthPrompt;
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetAuthPrompt(nsIAuthPrompt **aNetAuthPrompt)
{
    NS_ENSURE_ARG_POINTER(aNetAuthPrompt);
    NS_ENSURE_TRUE(m_netAuthPrompt, NS_ERROR_NULL_POINTER);
    *aNetAuthPrompt = m_netAuthPrompt;
    NS_ADDREF(*aNetAuthPrompt);
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::SetNotificationCallbacks(nsIInterfaceRequestor* aCallbacks)
{
    NS_ENSURE_ARG_POINTER(aCallbacks);
    m_callbacks = aCallbacks;
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetNotificationCallbacks(nsIInterfaceRequestor** aCallbacks)
{
    NS_ENSURE_ARG_POINTER(aCallbacks);
    NS_ENSURE_TRUE(m_callbacks, NS_ERROR_NULL_POINTER);
    *aCallbacks = m_callbacks;
    NS_ADDREF(*aCallbacks);
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::SetSmtpServer(nsISmtpServer * aSmtpServer)
{
    NS_ENSURE_ARG_POINTER(aSmtpServer);
    m_smtpServer = aSmtpServer;
    return NS_OK;
}

NS_IMETHODIMP
nsSmtpUrl::GetSmtpServer(nsISmtpServer ** aSmtpServer)
{
    NS_ENSURE_ARG_POINTER(aSmtpServer);
    NS_ENSURE_TRUE(m_smtpServer, NS_ERROR_NULL_POINTER);
    *aSmtpServer = m_smtpServer;
    NS_ADDREF(*aSmtpServer);
    return NS_OK;
}
