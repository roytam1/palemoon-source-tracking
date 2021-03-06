/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebuggerOnGCRunnable.h"

#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/Move.h"
#include "js/Debug.h"

using namespace mozilla::dom;

namespace mozilla {

/* static */ nsresult
DebuggerOnGCRunnable::Enqueue(JSContext* aCx, const JS::GCDescription& aDesc)
{
  auto gcEvent = aDesc.toGCEvent(aCx);
  if (!gcEvent) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  RefPtr<DebuggerOnGCRunnable> runOnGC =
    new DebuggerOnGCRunnable(Move(gcEvent));
  return NS_DispatchToCurrentThread(runOnGC);
}

NS_IMETHODIMP
DebuggerOnGCRunnable::Run()
{
  AutoJSAPI jsapi;
  jsapi.Init();
  if (!JS::dbg::FireOnGarbageCollectionHook(jsapi.cx(), Move(mGCData))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

nsresult
DebuggerOnGCRunnable::Cancel()
{
  mGCData = nullptr;
  return NS_OK;
}

} // namespace mozilla
