/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPChild.h"
#include "GMPContentChild.h"
#include "GMPProcessChild.h"
#include "GMPLoader.h"
#include "GMPVideoDecoderChild.h"
#include "GMPVideoEncoderChild.h"
#include "GMPAudioDecoderChild.h"
#include "GMPDecryptorChild.h"
#include "GMPVideoHost.h"
#include "nsDebugImpl.h"
#include "nsIFile.h"
#include "nsXULAppAPI.h"
#include "gmp-video-decode.h"
#include "gmp-video-encode.h"
#include "GMPPlatform.h"
#include "mozilla/ipc/ProcessChild.h"
#include "GMPUtils.h"
#include "prio.h"
#include "base/task.h"
#ifdef MOZ_EME
#include "widevine-adapter/WidevineAdapter.h"
#endif

using namespace mozilla::ipc;

static const int MAX_VOUCHER_LENGTH = 500000;

#ifdef XP_WIN
#include <stdlib.h> // for _exit()
#else
#include <unistd.h> // for _exit()
#endif

namespace mozilla {

#undef LOG
#undef LOGD

extern LogModule* GetGMPLog();
#define LOG(level, x, ...) MOZ_LOG(GetGMPLog(), (level), (x, ##__VA_ARGS__))
#define LOGD(x, ...) LOG(mozilla::LogLevel::Debug, "GMPChild[pid=%d] " x, (int)base::GetCurrentProcId(), ##__VA_ARGS__)

namespace gmp {

GMPChild::GMPChild()
  : mAsyncShutdown(nullptr)
  , mGMPMessageLoop(MessageLoop::current())
  , mGMPLoader(nullptr)
{
  LOGD("GMPChild ctor");
  nsDebugImpl::SetMultiprocessMode("GMP");
}

GMPChild::~GMPChild()
{
  LOGD("GMPChild dtor");
}

static bool
GetFileBase(const nsAString& aPluginPath,
            nsCOMPtr<nsIFile>& aLibDirectory,
            nsCOMPtr<nsIFile>& aFileBase,
            nsAutoString& aBaseName)
{
  nsresult rv = NS_NewLocalFile(aPluginPath,
                                true, getter_AddRefs(aFileBase));
  if (NS_FAILED(rv)) {
    return false;
  }

  if (NS_FAILED(aFileBase->Clone(getter_AddRefs(aLibDirectory)))) {
    return false;
  }

  nsCOMPtr<nsIFile> parent;
  rv = aFileBase->GetParent(getter_AddRefs(parent));
  if (NS_FAILED(rv)) {
    return false;
  }

  nsAutoString parentLeafName;
  rv = parent->GetLeafName(parentLeafName);
  if (NS_FAILED(rv)) {
    return false;
  }

  aBaseName = Substring(parentLeafName,
                        4,
                        parentLeafName.Length() - 1);
  return true;
}

static bool
GetFileBase(const nsAString& aPluginPath,
            nsCOMPtr<nsIFile>& aFileBase,
            nsAutoString& aBaseName)
{
  nsCOMPtr<nsIFile> unusedLibDir;
  return GetFileBase(aPluginPath, unusedLibDir, aFileBase, aBaseName);
}

static bool
GetPluginFile(const nsAString& aPluginPath,
              nsCOMPtr<nsIFile>& aLibDirectory,
              nsCOMPtr<nsIFile>& aLibFile)
{
  nsAutoString baseName;
  GetFileBase(aPluginPath, aLibDirectory, aLibFile, baseName);

#if defined(OS_POSIX)
  nsAutoString binaryName = NS_LITERAL_STRING("lib") + baseName + NS_LITERAL_STRING(".so");
#elif defined(XP_WIN)
  nsAutoString binaryName =                            baseName + NS_LITERAL_STRING(".dll");
#else
#error Unsupported O.S.
#endif
  aLibFile->AppendRelativePath(binaryName);
  return true;
}

static bool
GetPluginFile(const nsAString& aPluginPath,
              nsCOMPtr<nsIFile>& aLibFile)
{
  nsCOMPtr<nsIFile> unusedlibDir;
  return GetPluginFile(aPluginPath, unusedlibDir, aLibFile);
}

bool
GMPChild::Init(const nsAString& aPluginPath,
               const nsAString& aVoucherPath,
               base::ProcessId aParentPid,
               MessageLoop* aIOLoop,
               IPC::Channel* aChannel)
{
  LOGD("%s pluginPath=%s", __FUNCTION__, NS_ConvertUTF16toUTF8(aPluginPath).get());

  if (NS_WARN_IF(!Open(aChannel, aParentPid, aIOLoop))) {
    return false;
  }

  mPluginPath = aPluginPath;
  mSandboxVoucherPath = aVoucherPath;

  return true;
}

bool
GMPChild::RecvSetNodeId(const nsCString& aNodeId)
{
  LOGD("%s nodeId=%s", __FUNCTION__, aNodeId.Data());

  // Store the per origin salt for the node id. Note: we do this in a
  // separate message than RecvStartPlugin() so that the string is not
  // sitting in a string on the IPC code's call stack.
  mNodeId = aNodeId;
  return true;
}

GMPErr
GMPChild::GetAPI(const char* aAPIName,
                 void* aHostAPI,
                 void** aPluginAPI,
                 uint32_t aDecryptorId)
{
  if (!mGMPLoader) {
    return GMPGenericErr;
  }
  return mGMPLoader->GetAPI(aAPIName, aHostAPI, aPluginAPI, aDecryptorId);
}

bool
GMPChild::RecvPreloadLibs(const nsCString& aLibs)
{
#ifdef XP_WIN
  // Pre-load DLLs that need to be used by the EME plugin but that can't be
  // loaded after the sandbox has started
  // Items in this must be lowercase!
  static const char* whitelist[] = {
    "d3d9.dll", // Create an `IDirect3D9` to get adapter information
    "dxva2.dll", // Get monitor information
    "evr.dll", // MFGetStrideForBitmapInfoHeader
    "mfh264dec.dll", // H.264 decoder (on Windows Vista)
    "mfheaacdec.dll", // AAC decoder (on Windows Vista)
    "mfplat.dll", // MFCreateSample, MFCreateAlignedMemoryBuffer, MFCreateMediaType
    "msauddecmft.dll", // AAC decoder (on Windows 8)
    "msmpeg2adec.dll", // AAC decoder (on Windows 7)
    "msmpeg2vdec.dll", // H.264 decoder
  };

  nsTArray<nsCString> libs;
  SplitAt(", ", aLibs, libs);
  for (nsCString lib : libs) {
    ToLowerCase(lib);
    for (const char* whiteListedLib : whitelist) {
      if (lib.EqualsASCII(whiteListedLib)) {
        LoadLibraryA(lib.get());
        break;
      }
    }
  }
#endif
  return true;
}

bool
GMPChild::GetUTF8LibPath(nsACString& aOutLibPath)
{
  nsCOMPtr<nsIFile> libFile;
  if (!GetPluginFile(mPluginPath, libFile)) {
    return false;
  }

  if (!FileExists(libFile)) {
    NS_WARNING("Can't find GMP library file!");
    return false;
  }

  nsAutoString path;
  libFile->GetPath(path);
  aOutLibPath = NS_ConvertUTF16toUTF8(path);

  return true;
}

bool
GMPChild::AnswerStartPlugin(const nsString& aAdapter)
{
  LOGD("%s", __FUNCTION__);

  if (!PreLoadPluginVoucher()) {
    NS_WARNING("Plugin voucher failed to load!");
    return false;
  }
  PreLoadSandboxVoucher();

  nsCString libPath;
  if (!GetUTF8LibPath(libPath)) {
    return false;
  }

  auto platformAPI = new GMPPlatformAPI();
  InitPlatformAPI(*platformAPI, this);

  mGMPLoader = GMPProcessChild::GetGMPLoader();
  if (!mGMPLoader) {
    NS_WARNING("Failed to get GMPLoader");
    delete platformAPI;
    return false;
  }

#ifdef MOZ_EME
  bool isWidevine = aAdapter.EqualsLiteral("widevine");

  GMPAdapter* adapter = (isWidevine) ? new WidevineAdapter() : nullptr;
#else
  GMPAdapter* adapter = nullptr;
#endif
  if (!mGMPLoader->Load(libPath.get(),
                        libPath.Length(),
                        mNodeId.BeginWriting(),
                        mNodeId.Length(),
                        platformAPI,
                        adapter)) {
    NS_WARNING("Failed to load GMP");
    delete platformAPI;
    return false;
  }

  void* sh = nullptr;
  GMPAsyncShutdownHost* host = static_cast<GMPAsyncShutdownHost*>(this);
  GMPErr err = GetAPI(GMP_API_ASYNC_SHUTDOWN, host, &sh);
  if (err == GMPNoErr && sh) {
    mAsyncShutdown = reinterpret_cast<GMPAsyncShutdown*>(sh);
    SendAsyncShutdownRequired();
  }

  return true;
}

MessageLoop*
GMPChild::GMPMessageLoop()
{
  return mGMPMessageLoop;
}

void
GMPChild::ActorDestroy(ActorDestroyReason aWhy)
{
  LOGD("%s reason=%d", __FUNCTION__, aWhy);

  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    MOZ_ASSERT_IF(aWhy == NormalShutdown, !mGMPContentChildren[i - 1]->IsUsed());
    mGMPContentChildren[i - 1]->Close();
  }

  if (mGMPLoader) {
    mGMPLoader->Shutdown();
  }
  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Abnormal shutdown of GMP process!");
    ProcessChild::QuickExit();
  }

  XRE_ShutdownChildProcess();
}

void
GMPChild::ProcessingError(Result aCode, const char* aReason)
{
  switch (aCode) {
    case MsgDropped:
      _exit(0); // Don't trigger a crash report.
    case MsgNotKnown:
      MOZ_CRASH("aborting because of MsgNotKnown");
    case MsgNotAllowed:
      MOZ_CRASH("aborting because of MsgNotAllowed");
    case MsgPayloadError:
      MOZ_CRASH("aborting because of MsgPayloadError");
    case MsgProcessingError:
      MOZ_CRASH("aborting because of MsgProcessingError");
    case MsgRouteError:
      MOZ_CRASH("aborting because of MsgRouteError");
    case MsgValueError:
      MOZ_CRASH("aborting because of MsgValueError");
    default:
      MOZ_CRASH("not reached");
  }
}

PGMPTimerChild*
GMPChild::AllocPGMPTimerChild()
{
  return new GMPTimerChild(this);
}

bool
GMPChild::DeallocPGMPTimerChild(PGMPTimerChild* aActor)
{
  MOZ_ASSERT(mTimerChild == static_cast<GMPTimerChild*>(aActor));
  mTimerChild = nullptr;
  return true;
}

GMPTimerChild*
GMPChild::GetGMPTimers()
{
  if (!mTimerChild) {
    PGMPTimerChild* sc = SendPGMPTimerConstructor();
    if (!sc) {
      return nullptr;
    }
    mTimerChild = static_cast<GMPTimerChild*>(sc);
  }
  return mTimerChild;
}

PGMPStorageChild*
GMPChild::AllocPGMPStorageChild()
{
  return new GMPStorageChild(this);
}

bool
GMPChild::DeallocPGMPStorageChild(PGMPStorageChild* aActor)
{
  mStorage = nullptr;
  return true;
}

GMPStorageChild*
GMPChild::GetGMPStorage()
{
  if (!mStorage) {
    PGMPStorageChild* sc = SendPGMPStorageConstructor();
    if (!sc) {
      return nullptr;
    }
    mStorage = static_cast<GMPStorageChild*>(sc);
  }
  return mStorage;
}

bool
GMPChild::RecvCrashPluginNow()
{
  MOZ_CRASH();
  return true;
}

bool
GMPChild::RecvBeginAsyncShutdown()
{
  LOGD("%s AsyncShutdown=%d", __FUNCTION__, mAsyncShutdown!=nullptr);

  MOZ_ASSERT(mGMPMessageLoop == MessageLoop::current());
  if (mAsyncShutdown) {
    mAsyncShutdown->BeginShutdown();
  } else {
    ShutdownComplete();
  }
  return true;
}

bool
GMPChild::RecvCloseActive()
{
  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    mGMPContentChildren[i - 1]->CloseActive();
  }
  return true;
}

void
GMPChild::ShutdownComplete()
{
  LOGD("%s", __FUNCTION__);
  MOZ_ASSERT(mGMPMessageLoop == MessageLoop::current());
  mAsyncShutdown = nullptr;
  SendAsyncShutdownComplete();
}

static void
GetPluginVoucherFile(const nsAString& aPluginPath,
                     nsCOMPtr<nsIFile>& aOutVoucherFile)
{
  nsAutoString baseName;
  GetFileBase(aPluginPath, aOutVoucherFile, baseName);
  nsAutoString infoFileName = baseName + NS_LITERAL_STRING(".voucher");
  aOutVoucherFile->AppendRelativePath(infoFileName);
}

bool
GMPChild::PreLoadPluginVoucher()
{
  nsCOMPtr<nsIFile> voucherFile;
  GetPluginVoucherFile(mPluginPath, voucherFile);
  if (!FileExists(voucherFile)) {
    // Assume missing file is not fatal; that would break OpenH264.
    return true;
  }
  return ReadIntoArray(voucherFile, mPluginVoucher, MAX_VOUCHER_LENGTH);
}

void
GMPChild::PreLoadSandboxVoucher()
{
  nsCOMPtr<nsIFile> f;
  nsresult rv = NS_NewLocalFile(mSandboxVoucherPath, true, getter_AddRefs(f));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create nsIFile for sandbox voucher");
    return;
  }
  if (!FileExists(f)) {
    // Assume missing file is not fatal; that would break OpenH264.
    return;
  }

  if (!ReadIntoArray(f, mSandboxVoucher, MAX_VOUCHER_LENGTH)) {
    NS_WARNING("Failed to read sandbox voucher");
  }
}

PGMPContentChild*
GMPChild::AllocPGMPContentChild(Transport* aTransport,
                                ProcessId aOtherPid)
{
  GMPContentChild* child =
    mGMPContentChildren.AppendElement(new GMPContentChild(this))->get();
  child->Open(aTransport, aOtherPid, XRE_GetIOMessageLoop(), ipc::ChildSide);

  return child;
}

void
GMPChild::GMPContentChildActorDestroy(GMPContentChild* aGMPContentChild)
{
  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    UniquePtr<GMPContentChild>& toDestroy = mGMPContentChildren[i - 1];
    if (toDestroy.get() == aGMPContentChild) {
      SendPGMPContentChildDestroyed();
      RefPtr<DeleteTask<GMPContentChild>> task =
        new DeleteTask<GMPContentChild>(toDestroy.release());
      MessageLoop::current()->PostTask(task.forget());
      mGMPContentChildren.RemoveElementAt(i - 1);
      break;
    }
  }
}

} // namespace gmp
} // namespace mozilla

#undef LOG
#undef LOGD
