/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ipc/IOThreadChild.h"

#include "ContentProcess.h"

using mozilla::ipc::IOThreadChild;

namespace mozilla {
namespace dom {

void
ContentProcess::SetAppDir(const nsACString& aPath)
{
  mXREEmbed.SetAppDir(aPath);
}

bool
ContentProcess::Init()
{
    mContent.Init(IOThreadChild::message_loop(),
                  ParentPid(),
                  IOThreadChild::channel());
    mXREEmbed.Start();
    mContent.InitXPCOM();
    mContent.InitGraphicsDeviceData();

    return true;
}

// Note: CleanUp() never gets called in non-debug builds because we exit early
// in ContentChild::ActorDestroy().
void
ContentProcess::CleanUp()
{
    mXREEmbed.Stop();
}

} // namespace dom
} // namespace mozilla
