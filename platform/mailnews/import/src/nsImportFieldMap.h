/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsImportFieldMap_h___
#define nsImportFieldMap_h___

#include "nscore.h"
#include "nsIImportFieldMap.h"
#include "nsIAddrDatabase.h"
#include "nsTArray.h"
#include "nsString.h"


////////////////////////////////////////////////////////////////////////

class nsIStringBundle;

class nsImportFieldMap : public nsIImportFieldMap
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NS_DECL_NSIIMPORTFIELDMAP

  explicit nsImportFieldMap(nsIStringBundle *aBundle);

   static NS_METHOD Create(nsIStringBundle *aBundle, nsISupports *aOuter, REFNSIID aIID, void **aResult);

private:
  virtual ~nsImportFieldMap();
  nsresult  Allocate(int32_t newSize);

private:
  int32_t    m_numFields;
  int32_t  *  m_pFields;
  bool *  m_pActive;
  int32_t    m_allocated;
  nsTArray<nsString*>  m_descriptions;
  int32_t    m_mozFieldCount;
  bool        m_skipFirstRecord;
};


#endif
