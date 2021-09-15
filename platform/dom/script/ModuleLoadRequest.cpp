/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ModuleLoadRequest.h"
#include "ModuleScript.h"
#include "ScriptLoader.h"

namespace mozilla {
namespace dom {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(ModuleLoadRequest)
NS_INTERFACE_MAP_END_INHERITING(ScriptLoadRequest)

NS_IMPL_CYCLE_COLLECTION_INHERITED(ModuleLoadRequest, ScriptLoadRequest,
                                   mBaseURL,
                                   mLoader,
                                   mModuleScript,
                                   mImports)

NS_IMPL_ADDREF_INHERITED(ModuleLoadRequest, ScriptLoadRequest)
NS_IMPL_RELEASE_INHERITED(ModuleLoadRequest, ScriptLoadRequest)

ModuleLoadRequest::ModuleLoadRequest(nsIURI* aURI,
                                     nsIScriptElement* aElement,
                                     uint32_t aVersion,
                                     CORSMode aCORSMode,
                                     const SRIMetadata &aIntegrity,
                                     nsIURI* aReferrer,
                                     mozilla::net::ReferrerPolicy aReferrerPolicy,
                                     ScriptLoader* aLoader)
  : ScriptLoadRequest(ScriptKind::Module,
                      aURI,
                      aElement,
                      aVersion,
                      aCORSMode,
                      aIntegrity,
                      aReferrer,
                      aReferrerPolicy),
    mIsTopLevel(true),
    mLoader(aLoader),
    mVisitedSet(new VisitedURLSet())
{
  mVisitedSet->PutEntry(aURI);
}

ModuleLoadRequest::ModuleLoadRequest(nsIURI* aURI,
                                     ModuleLoadRequest* aParent)
  : ScriptLoadRequest(ScriptKind::Module,
                      aURI,
                      aParent->mElement,
                      aParent->mJSVersion,
                      aParent->mCORSMode,
                      SRIMetadata(),
                      aParent->mURI,
                      aParent->mReferrerPolicy),
    mIsTopLevel(false),
    mLoader(aParent->mLoader),
    mVisitedSet(aParent->mVisitedSet)
{
  MOZ_ASSERT(mVisitedSet->Contains(aURI));

  mIsInline = false;
  mScriptMode = aParent->mScriptMode;
}

void ModuleLoadRequest::Cancel()
{
  ScriptLoadRequest::Cancel();
  mModuleScript = nullptr;
  mProgress = ScriptLoadRequest::Progress::Ready;
  CancelImports();
  mReady.RejectIfExists(NS_ERROR_DOM_ABORT_ERR, __func__);
}

void
ModuleLoadRequest::CancelImports()
{
  for (size_t i = 0; i < mImports.Length(); i++) {
    mImports[i]->Cancel();
  }
}

void
ModuleLoadRequest::SetReady()
{
  // Mark a module as ready to execute. This means that this module and all it
  // dependencies have had their source loaded, parsed as a module and the
  // modules instantiated.
  //
  // The mReady promise is used to ensure that when all dependencies of a module
  // have become ready, DependenciesLoaded is called on that module
  // request. This is set up in StartFetchingModuleDependencies.

#ifdef DEBUG
  for (size_t i = 0; i < mImports.Length(); i++) {
    MOZ_ASSERT(mImports[i]->IsReadyToRun());
  }
#endif

  ScriptLoadRequest::SetReady();
  mReady.ResolveIfExists(true, __func__);
}

void
ModuleLoadRequest::ModuleLoaded()
{
  // A module that was found to be marked as fetching in the module map has now
  // been loaded.

  mModuleScript = mLoader->GetFetchedModule(mURI);
  if (!mModuleScript || mModuleScript->HasParseError()) {
    ModuleErrored();
    return;
  }

  mLoader->StartFetchingModuleDependencies(this);
}

void
ModuleLoadRequest::ModuleErrored()
{
  mLoader->CheckModuleDependenciesLoaded(this);
  MOZ_ASSERT(!mModuleScript || mModuleScript->HasParseError());

  CancelImports();
  SetReady();
  LoadFinished();
}

void
ModuleLoadRequest::DependenciesLoaded()
{
  // The module and all of its dependencies have been successfully fetched and
  // compiled.

  MOZ_ASSERT(mModuleScript);

  mLoader->CheckModuleDependenciesLoaded(this);
  SetReady();
  LoadFinished();
}

void
ModuleLoadRequest::LoadFailed()
{
  // We failed to load the source text or an error occurred unrelated to the
  // content of the module (e.g. OOM).

  MOZ_ASSERT(!mModuleScript);

  Cancel();
  LoadFinished();
}

void
ModuleLoadRequest::LoadFinished()
{
  mLoader->ProcessLoadedModuleTree(this);
  mLoader = nullptr;
}

} // dom namespace
} // mozilla namespace
