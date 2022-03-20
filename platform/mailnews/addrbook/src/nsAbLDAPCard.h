/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAbLDAPCard_h__
#define nsAbLDAPCard_h__

#include "nsAbCardProperty.h"
#include "nsIAbLDAPCard.h"
#include "nsTArray.h"

class nsIMutableArray;

class nsAbLDAPCard : public nsAbCardProperty,
                     public nsIAbLDAPCard
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIABLDAPCARD

  nsAbLDAPCard();

protected:
  virtual ~nsAbLDAPCard();
  nsTArray<nsCString> m_attributes;
  nsTArray<nsCString> m_objectClass;
};

#endif
