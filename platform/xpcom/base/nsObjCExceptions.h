/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Undo the damage that exception_defines.h does.
#undef try
#undef catch

#ifndef nsObjCExceptions_h_
#define nsObjCExceptions_h_

#import <Foundation/Foundation.h>

#include <unistd.h>
#include <signal.h>
#include "nsError.h"

// Undo the damage that exception_defines.h does.
#undef try
#undef catch

/* NOTE: Macros that claim to abort no longer abort, see bug 486574.
 * If you actually want to log and abort, call "nsObjCExceptionLogAbort"
 * from an exception handler. At some point we will fix this by replacing
 * all macros in the tree with appropriately-named macros.
 */

// See Mozilla bug 163260.
// This file can only be included in an Objective-C context.

__attribute__((unused))
static void
nsObjCExceptionLog(NSException* aException)
{
  NSLog(@"Mozilla has caught an Obj-C exception [%@: %@]",
        [aException name], [aException reason]);
}

__attribute__((unused))
static void
nsObjCExceptionAbort()
{
  // We need to raise a mach-o signal here, the Mozilla crash reporter on
  // Mac OS X does not respond to POSIX signals. Raising mach-o signals directly
  // is tricky so we do it by just derefing a null pointer.
  int* foo = nullptr;
  *foo = 1;
}

__attribute__((unused))
static void
nsObjCExceptionLogAbort(NSException* aException)
{
  nsObjCExceptionLog(aException);
  nsObjCExceptionAbort();
}

#define NS_OBJC_TRY(_e, _fail)                     \
@try { _e; }                                       \
@catch(NSException *_exn) {                        \
  nsObjCExceptionLog(_exn);                        \
  _fail;                                           \
}

#define NS_OBJC_TRY_EXPR(_e, _fail)                \
({                                                 \
   typeof(_e) _tmp;                                \
   @try { _tmp = (_e); }                           \
   @catch(NSException *_exn) {                     \
     nsObjCExceptionLog(_exn);                     \
     _fail;                                        \
   }                                               \
   _tmp;                                           \
})

#define NS_OBJC_TRY_EXPR_NULL(_e)                  \
NS_OBJC_TRY_EXPR(_e, 0)

#define NS_OBJC_TRY_IGNORE(_e)                     \
NS_OBJC_TRY(_e, )

// To reduce code size the abort versions do not reuse above macros. This allows
// catch blocks to only contain one call.

#define NS_OBJC_TRY_ABORT(_e)                      \
@try { _e; }                                       \
@catch(NSException *_exn) {                        \
  nsObjCExceptionLog(_exn);                        \
}

#define NS_OBJC_TRY_EXPR_ABORT(_e)                 \
({                                                 \
   typeof(_e) _tmp;                                \
   @try { _tmp = (_e); }                           \
   @catch(NSException *_exn) {                     \
     nsObjCExceptionLog(_exn);                     \
   }                                               \
   _tmp;                                           \
})

// For wrapping blocks of Obj-C calls. Does not actually terminate.
#define NS_OBJC_BEGIN_TRY_ABORT_BLOCK @try {
#define NS_OBJC_END_TRY_ABORT_BLOCK   } @catch(NSException *_exn) {             \
                                        nsObjCExceptionLog(_exn);               \
                                      }

// Same as above ABORT_BLOCK but returns a value after the try/catch block to
// suppress compiler warnings. This allows us to avoid having to refactor code
// to get scoping right when wrapping an entire method.

#define NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL @try {
#define NS_OBJC_END_TRY_ABORT_BLOCK_NIL   } @catch(NSException *_exn) {         \
                                            nsObjCExceptionLog(_exn);           \
                                          }                                     \
                                          return nil;

#define NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSNULL @try {
#define NS_OBJC_END_TRY_ABORT_BLOCK_NSNULL   } @catch(NSException *_exn) {      \
                                               nsObjCExceptionLog(_exn);        \
                                             }                                  \
                                             return nullptr;

#define NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT @try {
#define NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT   } @catch(NSException *_exn) {    \
                                                 nsObjCExceptionLog(_exn);      \
                                               }                                \
                                               return NS_ERROR_FAILURE;

#define NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN    @try {
#define NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(_rv) } @catch(NSException *_exn) {   \
                                                  nsObjCExceptionLog(_exn);\
                                                }                               \
                                                return _rv;

#endif // nsObjCExceptions_h_
