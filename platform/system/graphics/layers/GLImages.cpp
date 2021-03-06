/* -*- Mode: C++; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLImages.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "ScopedGLHelpers.h"
#include "GLImages.h"
#include "GLBlitHelper.h"
#include "GLReadTexImageHelper.h"
#include "GLLibraryEGL.h"
#include "mozilla/gfx/Logging.h"

using namespace mozilla;
using namespace mozilla::gl;

namespace mozilla {
namespace layers {

static RefPtr<GLContext> sSnapshotContext;

EGLImageImage::EGLImageImage(EGLImage aImage, EGLSync aSync,
                             const gfx::IntSize& aSize, const gl::OriginPos& aOrigin,
                             bool aOwns)
 : GLImage(ImageFormat::EGLIMAGE),
   mImage(aImage),
   mSync(aSync),
   mSize(aSize),
   mPos(aOrigin),
   mOwns(aOwns)
{
}

EGLImageImage::~EGLImageImage()
{
  if (!mOwns) {
    return;
  }

  if (mImage) {
    sEGLLibrary.fDestroyImage(EGL_DISPLAY(), mImage);
    mImage = nullptr;
  }

  if (mSync) {
    sEGLLibrary.fDestroySync(EGL_DISPLAY(), mSync);
    mSync = nullptr;
  }
}

already_AddRefed<gfx::SourceSurface>
GLImage::GetAsSourceSurface()
{
  MOZ_ASSERT(NS_IsMainThread(), "Should be on the main thread");

  if (!sSnapshotContext) {
    nsCString discardFailureId;
    sSnapshotContext = GLContextProvider::CreateHeadless(CreateContextFlags::NONE,
                                                         &discardFailureId);
    if (!sSnapshotContext) {
      NS_WARNING("Failed to create snapshot GLContext");
      return nullptr;
    }
  }

  sSnapshotContext->MakeCurrent();
  ScopedTexture scopedTex(sSnapshotContext);
  ScopedBindTexture boundTex(sSnapshotContext, scopedTex.Texture());

  gfx::IntSize size = GetSize();
  sSnapshotContext->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA,
                                size.width, size.height, 0,
                                LOCAL_GL_RGBA,
                                LOCAL_GL_UNSIGNED_BYTE,
                                nullptr);

  ScopedFramebufferForTexture autoFBForTex(sSnapshotContext, scopedTex.Texture());
  if (!autoFBForTex.IsComplete()) {
      gfxCriticalError() << "GetAsSourceSurface: ScopedFramebufferForTexture failed.";
      return nullptr;
  }

  const gl::OriginPos destOrigin = gl::OriginPos::TopLeft;

  if (!sSnapshotContext->BlitHelper()->BlitImageToFramebuffer(this, size,
                                                              autoFBForTex.FB(),
                                                              destOrigin))
  {
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> source =
        gfx::Factory::CreateDataSourceSurface(size, gfx::SurfaceFormat::B8G8R8A8);
  if (NS_WARN_IF(!source)) {
    return nullptr;
  }

  ScopedBindFramebuffer bind(sSnapshotContext, autoFBForTex.FB());
  ReadPixelsIntoDataSurface(sSnapshotContext, source);
  return source.forget();
}

} // namespace layers
} // namespace mozilla
