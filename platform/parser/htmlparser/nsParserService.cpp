/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsError.h"
#include "nsIAtom.h"
#include "nsParserService.h"
#include "nsHTMLEntities.h"
#include "nsElementTable.h"
#include "nsICategoryManager.h"
#include "nsCategoryManagerUtils.h"

nsParserService::nsParserService()
{
}

nsParserService::~nsParserService()
{
}

NS_IMPL_ISUPPORTS(nsParserService, nsIParserService)

int32_t
nsParserService::HTMLAtomTagToId(nsIAtom* aAtom) const
{
  return nsHTMLTags::StringTagToId(nsDependentAtomString(aAtom));
}

int32_t
nsParserService::HTMLCaseSensitiveAtomTagToId(nsIAtom* aAtom) const
{
  return nsHTMLTags::CaseSensitiveAtomTagToId(aAtom);
}

int32_t
nsParserService::HTMLStringTagToId(const nsAString& aTag) const
{
  return nsHTMLTags::StringTagToId(aTag);
}

const char16_t*
nsParserService::HTMLIdToStringTag(int32_t aId) const
{
  return nsHTMLTags::GetStringValue((nsHTMLTag)aId);
}
  
nsIAtom*
nsParserService::HTMLIdToAtomTag(int32_t aId) const
{
  return nsHTMLTags::GetAtom((nsHTMLTag)aId);
}

NS_IMETHODIMP
nsParserService::HTMLConvertEntityToUnicode(const nsAString& aEntity,
                                            int32_t* aUnicode) const
{
  *aUnicode = nsHTMLEntities::EntityToUnicode(aEntity);

  return NS_OK;
}

NS_IMETHODIMP
nsParserService::HTMLConvertUnicodeToEntity(int32_t aUnicode,
                                            nsCString& aEntity) const
{
  const char* str = nsHTMLEntities::UnicodeToEntity(aUnicode);
  if (str) {
    aEntity.Assign(str);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsParserService::IsContainer(int32_t aId, bool& aIsContainer) const
{
  aIsContainer = nsHTMLElement::IsContainer((nsHTMLTag)aId);

  return NS_OK;
}

NS_IMETHODIMP
nsParserService::IsBlock(int32_t aId, bool& aIsBlock) const
{
  aIsBlock = nsHTMLElement::IsBlock((nsHTMLTag)aId);

  return NS_OK;
}
