/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-  */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MDB_
#include "mdb.h"
#endif

#ifndef _MORK_
#include "mork.h"
#endif

#ifndef _MORKCONFIG_
#include "morkConfig.h"
#endif

//3456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789

void mork_assertion_signal(const char* inMessage)
{
#if defined(MORK_WIN) || defined(MORK_MAC)
  // asm { int 3 }
  NS_ERROR(inMessage);
#endif /*MORK_WIN*/
}

#ifdef MORK_PROVIDE_STDLIB

MORK_LIB_IMPL(mork_i4)
mork_memcmp(const void* inOne, const void* inTwo, mork_size inSize)
{
  const mork_u1* t = (const mork_u1*) inTwo;
  const mork_u1* s = (const mork_u1*) inOne;
  const mork_u1* end = s + inSize;
  mork_i4 delta;
  
  while ( s < end )
  {
    delta = ((mork_i4) *s) - ((mork_i4) *t);
    if ( delta )
      return delta;
    else
    {
      ++t;
      ++s;
    }
  }
  return 0;
}

MORK_LIB_IMPL(void)
mork_memcpy(void* outDst, const void* inSrc, mork_size inSize)
{
  mork_u1* d = (mork_u1*) outDst;
  mork_u1* end = d + inSize;
  const mork_u1* s = ((const mork_u1*) inSrc);
  
  while ( inSize >= 8 )
  {
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    
    inSize -= 8;
  }
  
  while ( d < end )
    *d++ = *s++;
}

MORK_LIB_IMPL(void)
mork_memmove(void* outDst, const void* inSrc, mork_size inSize)
{
  mork_u1* d = (mork_u1*) outDst;
  const mork_u1* s = (const mork_u1*) inSrc;
  if ( d != s && inSize ) // copy is necessary?
  {
    const mork_u1* srcEnd = s + inSize; // one past last source byte
    
    if ( d > s && d < srcEnd ) // overlap? need to copy backwards?
    {
      s = srcEnd; // start one past last source byte
      d += inSize; // start one past last dest byte
      mork_u1* dstBegin = d; // last byte to write is first in dest range
      while ( d - dstBegin >= 8 )
      {
        *--d = *--s;
        *--d = *--s;
        *--d = *--s;
        *--d = *--s;
        
        *--d = *--s;
        *--d = *--s;
        *--d = *--s;
        *--d = *--s;
      }
      while ( d > dstBegin )
        *--d = *--s;
    }
    else // can copy forwards without any overlap
    {
      mork_u1* dstEnd = d + inSize;
      while ( dstEnd - d >= 8 )
      {
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
      }
      while ( d < dstEnd )
        *d++ = *s++;
    }
  }
}

MORK_LIB_IMPL(void)
mork_memset(void* outDst, int inByte, mork_size inSize)
{
  mork_u1* d = (mork_u1*) outDst;
  mork_u1* end = d + inSize;
  while ( d < end )
    *d++ = (mork_u1) inByte;
}

MORK_LIB_IMPL(void)
mork_strcpy(void* outDst, const void* inSrc)
{
  // back up one first to support preincrement
  mork_u1* d = ((mork_u1*) outDst) - 1;
  const mork_u1* s = ((const mork_u1*) inSrc) - 1;
  while ( ( *++d = *++s ) != 0 )
    /* empty */;
}

MORK_LIB_IMPL(mork_i4)
mork_strcmp(const void* inOne, const void* inTwo)
{
  const mork_u1* t = (const mork_u1*) inTwo;
  const mork_u1* s = ((const mork_u1*) inOne);
  mork_i4 a;
  mork_i4 b;
  mork_i4 delta;
  
  do
  {
    a = (mork_i4) *s++;
    b = (mork_i4) *t++;
    delta = a - b;
  }
  while ( !delta && a && b );
  
  return delta;
}

MORK_LIB_IMPL(mork_i4)
mork_strncmp(const void* inOne, const void* inTwo, mork_size inSize)
{
  const mork_u1* t = (const mork_u1*) inTwo;
  const mork_u1* s = (const mork_u1*) inOne;
  const mork_u1* end = s + inSize;
  mork_i4 delta;
  mork_i4 a;
  mork_i4 b;
  
  while ( s < end )
  {
    a = (mork_i4) *s++;
    b = (mork_i4) *t++;
    delta = a - b;
    if ( delta || !a || !b )
      return delta;
  }
  return 0;
}

MORK_LIB_IMPL(mork_size)
mork_strlen(const void* inString)
{
  // back up one first to support preincrement
  const mork_u1* s = ((const mork_u1*) inString) - 1;
  while ( *++s ) // preincrement is cheapest
    /* empty */;
  
  return s - ((const mork_u1*) inString); // distance from original address
}

#endif /*MORK_PROVIDE_STDLIB*/

//3456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789
