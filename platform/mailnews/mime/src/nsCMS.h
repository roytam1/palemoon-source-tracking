/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_CMS_H__
#define __NS_CMS_H__

#include "nsISupports.h"
#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsIInterfaceRequestor.h"
#include "nsICMSMessage.h"
#include "nsICMSMessage2.h"
#include "nsIX509Cert.h"
#include "nsICMSEncoder.h"
#include "nsICMSDecoder.h"
#include "sechash.h"
#include "cms.h"
#include "nsNSSShutDown.h"

#define NS_CMSMESSAGE_CID \
  { 0xa4557478, 0xae16, 0x11d5, { 0xba,0x4b,0x00,0x10,0x83,0x03,0xb1,0x17 } }

class nsCMSMessage : public nsICMSMessage,
                     public nsICMSMessage2,
                     public nsNSSShutDownObject
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICMSMESSAGE
  NS_DECL_NSICMSMESSAGE2

  nsCMSMessage();
  explicit nsCMSMessage(NSSCMSMessage* aCMSMsg);
  nsresult Init();

  void referenceContext(nsIInterfaceRequestor* aContext) {m_ctx = aContext;}
  NSSCMSMessage* getCMS() {return m_cmsMsg;}
private:
  virtual ~nsCMSMessage();
  nsCOMPtr<nsIInterfaceRequestor> m_ctx;
  NSSCMSMessage * m_cmsMsg;
  NSSCMSSignerInfo* GetTopLevelSignerInfo();
  nsresult CommonVerifySignature(unsigned char* aDigestData, uint32_t aDigestDataLen,
                                 int16_t aDigestType);

  nsresult CommonAsyncVerifySignature(nsISMimeVerificationListener *aListener,
                                      unsigned char* aDigestData, uint32_t aDigestDataLen,
                                      int16_t aDigestType);
  bool GetIntHashToOidHash(const int16_t aCryptoHashInt, SECOidTag &aOidTag);
  bool IsAllowedHash(const int16_t aCryptoHashInt);

  virtual void virtualDestroyNSSReference() override;
  void destructorSafeDestroyNSSReference();

};

// ===============================================
// nsCMSDecoder - implementation of nsICMSDecoder
// ===============================================

#define NS_CMSDECODER_CID \
  { 0x9dcef3a4, 0xa3bc, 0x11d5, { 0xba, 0x47, 0x00, 0x10, 0x83, 0x03, 0xb1, 0x17 } }

class nsCMSDecoder : public nsICMSDecoder,
                     public nsNSSShutDownObject
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICMSDECODER

  nsCMSDecoder();
  nsresult Init();

private:
  virtual ~nsCMSDecoder();
  nsCOMPtr<nsIInterfaceRequestor> m_ctx;
  NSSCMSDecoderContext *m_dcx;
  virtual void virtualDestroyNSSReference() override;
  void destructorSafeDestroyNSSReference();
};

// ===============================================
// nsCMSEncoder - implementation of nsICMSEncoder
// ===============================================

#define NS_CMSENCODER_CID \
  { 0xa15789aa, 0x8903, 0x462b, { 0x81, 0xe9, 0x4a, 0xa2, 0xcf, 0xf4, 0xd5, 0xcb } }
class nsCMSEncoder : public nsICMSEncoder,
                     public nsNSSShutDownObject
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICMSENCODER

  nsCMSEncoder();
  nsresult Init();

private:
  virtual ~nsCMSEncoder();
  nsCOMPtr<nsIInterfaceRequestor> m_ctx;
  NSSCMSEncoderContext *m_ecx;
  virtual void virtualDestroyNSSReference() override;
  void destructorSafeDestroyNSSReference();
};

#endif
