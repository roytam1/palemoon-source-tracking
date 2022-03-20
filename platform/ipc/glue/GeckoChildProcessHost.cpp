/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoChildProcessHost.h"

#include "base/command_line.h"
#include "base/string_util.h"
#include "base/task.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/process_watcher.h"

#include "MainThreadUtils.h"
#include "mozilla/Sprintf.h"
#include "prenv.h"
#include "nsXPCOMPrivate.h"

#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsPrintfCString.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ipc/BrowserProcessSubThread.h"
#include "mozilla/Omnijar.h"
#include "ProtocolUtils.h"
#include <sys/stat.h>

#ifdef XP_WIN
#include "nsIWinTaskbar.h"
#define NS_TASKBAR_CONTRACTID "@mozilla.org/windows-taskbar;1"
#endif

#include "nsTArray.h"
#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsNativeCharsetUtils.h"
#include "nscore.h" // for NS_FREE_PERMANENT_DATA

using mozilla::MonitorAutoLock;
using mozilla::ipc::GeckoChildProcessHost;

static const bool kLowRightsSubprocesses =
  // We only attempted to drop privileges on gonk, because it
  // had no plugins or extensions to worry about breaking.
  false
  ;

static bool
ShouldHaveDirectoryService()
{
  return GeckoProcessType_Default == XRE_GetProcessType();
}

/*static*/
base::ChildPrivileges
GeckoChildProcessHost::DefaultChildPrivileges()
{
  return (kLowRightsSubprocesses ?
          base::PRIVILEGES_UNPRIVILEGED : base::PRIVILEGES_INHERIT);
}

GeckoChildProcessHost::GeckoChildProcessHost(GeckoProcessType aProcessType,
                                             ChildPrivileges aPrivileges)
  : mProcessType(aProcessType),
    mPrivileges(aPrivileges),
    mMonitor("mozilla.ipc.GeckChildProcessHost.mMonitor"),
    mProcessState(CREATING_CHANNEL),
    mChildProcessHandle(0)
{
    MOZ_COUNT_CTOR(GeckoChildProcessHost);
}

GeckoChildProcessHost::~GeckoChildProcessHost()

{
  AssertIOThread();

  MOZ_COUNT_DTOR(GeckoChildProcessHost);

  if (mChildProcessHandle != 0) {
    ProcessWatcher::EnsureProcessTerminated(mChildProcessHandle
#ifdef NS_FREE_PERMANENT_DATA
    // If we're doing leak logging, shutdown can be slow.
                                            , false // don't "force"
#endif
    );
  }
}

//static
auto
GeckoChildProcessHost::GetPathToBinary(FilePath& exePath, GeckoProcessType processType) -> BinaryPathType
{
  if (sRunSelfAsContentProc &&
      (processType == GeckoProcessType_Content || processType == GeckoProcessType_GPU)) {
#if defined(OS_WIN)
    wchar_t exePathBuf[MAXPATHLEN];
    if (!::GetModuleFileNameW(nullptr, exePathBuf, MAXPATHLEN)) {
      MOZ_CRASH("GetModuleFileNameW failed (FIXME)");
    }
    exePath = FilePath::FromWStringHack(exePathBuf);
#elif defined(OS_POSIX)
    exePath = FilePath(CommandLine::ForCurrentProcess()->argv()[0]);
#else
#  error Sorry; target OS not supported yet.
#endif
    return BinaryPathType::Self;
  }

  if (ShouldHaveDirectoryService()) {
    MOZ_ASSERT(gGREBinPath);
#ifdef OS_WIN
    exePath = FilePath(char16ptr_t(gGREBinPath));
#else
    nsCString path;
    NS_CopyUnicodeToNative(nsDependentString(gGREBinPath), path);
    exePath = FilePath(path.get());
#endif
  }

  if (exePath.empty()) {
#ifdef OS_WIN
    exePath = FilePath::FromWStringHack(CommandLine::ForCurrentProcess()->program());
#else
    exePath = FilePath(CommandLine::ForCurrentProcess()->argv()[0]);
#endif
    exePath = exePath.DirName();
  }

  exePath = exePath.AppendASCII(MOZ_CHILD_PROCESS_NAME);

  return BinaryPathType::PluginContainer;
}

nsresult GeckoChildProcessHost::GetArchitecturesForBinary(const char *path, uint32_t *result)
{
  *result = 0;

  return NS_ERROR_NOT_IMPLEMENTED;
}

uint32_t GeckoChildProcessHost::GetSupportedArchitecturesForProcessType(GeckoProcessType type)
{
  return base::GetCurrentProcessArchitecture();
}

// We start the unique IDs at 1 so that 0 can be used to mean that
// a component has no unique ID assigned to it.
uint32_t GeckoChildProcessHost::sNextUniqueID = 1;

/* static */
uint32_t
GeckoChildProcessHost::GetUniqueID()
{
  return sNextUniqueID++;
}

void
GeckoChildProcessHost::PrepareLaunch()
{
#ifdef XP_WIN
  if (mProcessType == GeckoProcessType_Plugin) {
    InitWindowsGroupID();
  }
#endif
}

#ifdef XP_WIN
void GeckoChildProcessHost::InitWindowsGroupID()
{
  // On Win7+, pass the application user model to the child, so it can
  // register with it. This insures windows created by the container
  // properly group with the parent app on the Win7 taskbar.
  nsCOMPtr<nsIWinTaskbar> taskbarInfo =
    do_GetService(NS_TASKBAR_CONTRACTID);
  if (taskbarInfo) {
    bool isSupported = false;
    taskbarInfo->GetAvailable(&isSupported);
    nsAutoString appId;
    if (isSupported && NS_SUCCEEDED(taskbarInfo->GetDefaultGroupId(appId))) {
      mGroupId.Append(appId);
    } else {
      mGroupId.Assign('-');
    }
  }
}
#endif

bool
GeckoChildProcessHost::SyncLaunch(std::vector<std::string> aExtraOpts, int aTimeoutMs, base::ProcessArchitecture arch)
{
  PrepareLaunch();

  MessageLoop* ioLoop = XRE_GetIOMessageLoop();
  NS_ASSERTION(MessageLoop::current() != ioLoop, "sync launch from the IO thread NYI");

  ioLoop->PostTask(NewNonOwningRunnableMethod
                   <std::vector<std::string>, base::ProcessArchitecture>
                   (this, &GeckoChildProcessHost::RunPerformAsyncLaunch,
                    aExtraOpts, arch));

  return WaitUntilConnected(aTimeoutMs);
}

bool
GeckoChildProcessHost::AsyncLaunch(std::vector<std::string> aExtraOpts,
                                   base::ProcessArchitecture arch)
{
  PrepareLaunch();

  MessageLoop* ioLoop = XRE_GetIOMessageLoop();

  ioLoop->PostTask(NewNonOwningRunnableMethod
                   <std::vector<std::string>, base::ProcessArchitecture>
                   (this, &GeckoChildProcessHost::RunPerformAsyncLaunch,
                    aExtraOpts, arch));

  // This may look like the sync launch wait, but we only delay as
  // long as it takes to create the channel.
  MonitorAutoLock lock(mMonitor);
  while (mProcessState < CHANNEL_INITIALIZED) {
    lock.Wait();
  }

  return true;
}

bool
GeckoChildProcessHost::WaitUntilConnected(int32_t aTimeoutMs)
{
  PROFILER_LABEL_FUNC(js::ProfileEntry::Category::OTHER);

  // NB: this uses a different mechanism than the chromium parent
  // class.
  PRIntervalTime timeoutTicks = (aTimeoutMs > 0) ?
    PR_MillisecondsToInterval(aTimeoutMs) : PR_INTERVAL_NO_TIMEOUT;

  MonitorAutoLock lock(mMonitor);
  PRIntervalTime waitStart = PR_IntervalNow();
  PRIntervalTime current;

  // We'll receive several notifications, we need to exit when we
  // have either successfully launched or have timed out.
  while (mProcessState != PROCESS_CONNECTED) {
    // If there was an error then return it, don't wait out the timeout.
    if (mProcessState == PROCESS_ERROR) {
      break;
    }

    lock.Wait(timeoutTicks);

    if (timeoutTicks != PR_INTERVAL_NO_TIMEOUT) {
      current = PR_IntervalNow();
      PRIntervalTime elapsed = current - waitStart;
      if (elapsed > timeoutTicks) {
        break;
      }
      timeoutTicks = timeoutTicks - elapsed;
      waitStart = current;
    }
  }

  return mProcessState == PROCESS_CONNECTED;
}

bool
GeckoChildProcessHost::LaunchAndWaitForProcessHandle(StringVector aExtraOpts)
{
  PrepareLaunch();

  MessageLoop* ioLoop = XRE_GetIOMessageLoop();
  ioLoop->PostTask(NewNonOwningRunnableMethod
                   <std::vector<std::string>, base::ProcessArchitecture>
                   (this, &GeckoChildProcessHost::RunPerformAsyncLaunch,
                    aExtraOpts, base::GetCurrentProcessArchitecture()));

  MonitorAutoLock lock(mMonitor);
  while (mProcessState < PROCESS_CREATED) {
    lock.Wait();
  }
  MOZ_ASSERT(mProcessState == PROCESS_ERROR || mChildProcessHandle);

  return mProcessState < PROCESS_ERROR;
}

void
GeckoChildProcessHost::InitializeChannel()
{
  CreateChannel();

  MonitorAutoLock lock(mMonitor);
  mProcessState = CHANNEL_INITIALIZED;
  lock.Notify();
}

void
GeckoChildProcessHost::Join()
{
  AssertIOThread();

  if (!mChildProcessHandle) {
    return;
  }

  // If this fails, there's nothing we can do.
  base::KillProcess(mChildProcessHandle, 0, /*wait*/true);
  SetAlreadyDead();
}

void
GeckoChildProcessHost::SetAlreadyDead()
{
  if (mChildProcessHandle &&
      mChildProcessHandle != kInvalidProcessHandle) {
    base::CloseProcessHandle(mChildProcessHandle);
  }

  mChildProcessHandle = 0;
}

int32_t GeckoChildProcessHost::mChildCounter = 0;

void
GeckoChildProcessHost::SetChildLogName(const char* varName, const char* origLogName,
                                       nsACString &buffer)
{
  // We currently have no portable way to launch child with environment
  // different than parent.  So temporarily change NSPR_LOG_FILE so child
  // inherits value we want it to have. (NSPR only looks at NSPR_LOG_FILE at
  // startup, so it's 'safe' to play with the parent's environment this way.)
  buffer.Assign(varName);
  buffer.Append(origLogName);

  // Append child-specific postfix to name
  buffer.AppendLiteral(".child-");
  buffer.AppendInt(mChildCounter);

  // Passing temporary to PR_SetEnv is ok here if we keep the temporary
  // for the time we launch the sub-process.  It's copied to the new
  // environment.
  PR_SetEnv(buffer.BeginReading());
}

bool
GeckoChildProcessHost::PerformAsyncLaunch(std::vector<std::string> aExtraOpts, base::ProcessArchitecture arch)
{
  // If NSPR log files are not requested, we're done.
  const char* origNSPRLogName = PR_GetEnv("NSPR_LOG_FILE");
  const char* origMozLogName = PR_GetEnv("MOZ_LOG_FILE");
  if (!origNSPRLogName && !origMozLogName) {
    return PerformAsyncLaunchInternal(aExtraOpts, arch);
  }

  // - Note: this code is not called re-entrantly, nor are restoreOrig*LogName
  //   or mChildCounter touched by any other thread, so this is safe.
  ++mChildCounter;

  // Must keep these on the same stack where from we call PerformAsyncLaunchInternal
  // so that PR_DuplicateEnvironment() still sees a valid memory.
  nsAutoCString nsprLogName;
  nsAutoCString mozLogName;

  if (origNSPRLogName) {
    if (mRestoreOrigNSPRLogName.IsEmpty()) {
      mRestoreOrigNSPRLogName.AssignLiteral("NSPR_LOG_FILE=");
      mRestoreOrigNSPRLogName.Append(origNSPRLogName);
    }
    SetChildLogName("NSPR_LOG_FILE=", origNSPRLogName, nsprLogName);
  }
  if (origMozLogName) {
    if (mRestoreOrigMozLogName.IsEmpty()) {
      mRestoreOrigMozLogName.AssignLiteral("MOZ_LOG_FILE=");
      mRestoreOrigMozLogName.Append(origMozLogName);
    }
    SetChildLogName("MOZ_LOG_FILE=", origMozLogName, mozLogName);
  }

  bool retval = PerformAsyncLaunchInternal(aExtraOpts, arch);

  // Revert to original value
  if (origNSPRLogName) {
    PR_SetEnv(mRestoreOrigNSPRLogName.get());
  }
  if (origMozLogName) {
    PR_SetEnv(mRestoreOrigMozLogName.get());
  }

  return retval;
}

bool
GeckoChildProcessHost::RunPerformAsyncLaunch(std::vector<std::string> aExtraOpts,
                                             base::ProcessArchitecture aArch)
{
  InitializeChannel();

  bool ok = PerformAsyncLaunch(aExtraOpts, aArch);
  if (!ok) {
    // WaitUntilConnected might be waiting for us to signal.
    // If something failed let's set the error state and notify.
    MonitorAutoLock lock(mMonitor);
    mProcessState = PROCESS_ERROR;
    lock.Notify();
    CHROMIUM_LOG(ERROR) << "Failed to launch " <<
      XRE_ChildProcessTypeToString(mProcessType) << " subprocess";
  }
  return ok;
}

void
#if defined(XP_WIN)
AddAppDirToCommandLine(CommandLine& aCmdLine)
#else
AddAppDirToCommandLine(std::vector<std::string>& aCmdLine)
#endif
{
  // Content processes need access to application resources, so pass
  // the full application directory path to the child process.
  if (ShouldHaveDirectoryService()) {
    nsCOMPtr<nsIProperties> directoryService(do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID));
    NS_ASSERTION(directoryService, "Expected XPCOM to be available");
    if (directoryService) {
      nsCOMPtr<nsIFile> appDir;
      // NS_XPCOM_CURRENT_PROCESS_DIR really means the app dir, not the
      // current process dir.
      nsresult rv = directoryService->Get(NS_XPCOM_CURRENT_PROCESS_DIR,
                                          NS_GET_IID(nsIFile),
                                          getter_AddRefs(appDir));
      if (NS_SUCCEEDED(rv)) {
#if defined(XP_WIN)
        nsString path;
        MOZ_ALWAYS_SUCCEEDS(appDir->GetPath(path));
        aCmdLine.AppendLooseValue(UTF8ToWide("-appdir"));
        std::wstring wpath(path.get());
        aCmdLine.AppendLooseValue(wpath);
#else
        nsAutoCString path;
        MOZ_ALWAYS_SUCCEEDS(appDir->GetNativePath(path));
        aCmdLine.push_back("-appdir");
        aCmdLine.push_back(path.get());
#endif
      }
    }
  }
}

bool
GeckoChildProcessHost::PerformAsyncLaunchInternal(std::vector<std::string>& aExtraOpts, base::ProcessArchitecture arch)
{
  // We rely on the fact that InitializeChannel() has already been processed
  // on the IO thread before this point is reached.
  if (!GetChannel()) {
    return false;
  }

  base::ProcessHandle process = 0;

  // send the child the PID so that it can open a ProcessHandle back to us.
  // probably don't want to do this in the long run
  char pidstring[32];
  SprintfLiteral(pidstring,"%d", base::Process::Current().pid());

  const char* const childProcessType =
      XRE_ChildProcessTypeToString(mProcessType);

//--------------------------------------------------
#if defined(OS_POSIX)
  // For POSIX, we have to be extremely anal about *not* using
  // std::wstring in code compiled with Mozilla's -fshort-wchar
  // configuration, because chromium is compiled with -fno-short-wchar
  // and passing wstrings from one config to the other is unsafe.  So
  // we split the logic here.

#if defined(OS_LINUX) || defined(OS_MACOSX) || defined(OS_BSD) || defined(OS_SOLARIS)
  base::environment_map newEnvVars;
  ChildPrivileges privs = mPrivileges;
  if (privs == base::PRIVILEGES_DEFAULT) {
    privs = DefaultChildPrivileges();
  }

#if defined(MOZ_WIDGET_GTK)
  if (mProcessType == GeckoProcessType_Content) {
    // disable IM module to avoid sandbox violation
    newEnvVars["GTK_IM_MODULE"] = "gtk-im-context-simple";
  }
#endif

  // XPCOM may not be initialized in some subprocesses.  We don't want
  // to initialize XPCOM just for the directory service, especially
  // since LD_LIBRARY_PATH is already set correctly in subprocesses
  // (meaning that we don't need to set that up in the environment).
  if (ShouldHaveDirectoryService()) {
    MOZ_ASSERT(gGREBinPath);
    nsCString path;
    NS_CopyUnicodeToNative(nsDependentString(gGREBinPath), path);
# if defined(OS_LINUX) || defined(OS_BSD)
    const char *ld_library_path = PR_GetEnv("LD_LIBRARY_PATH");
    nsCString new_ld_lib_path(path.get());

#  if (MOZ_WIDGET_GTK == 3)
    if (mProcessType == GeckoProcessType_Plugin) {
      new_ld_lib_path.Append("/gtk2:");
      new_ld_lib_path.Append(path.get());
    }
#endif
    if (ld_library_path && *ld_library_path) {
      new_ld_lib_path.Append(':');
      new_ld_lib_path.Append(ld_library_path);
    }
    newEnvVars["LD_LIBRARY_PATH"] = new_ld_lib_path.get();

# elif OS_MACOSX
    newEnvVars["DYLD_LIBRARY_PATH"] = path.get();
    // XXX DYLD_INSERT_LIBRARIES should only be set when launching a plugin
    //     process, and has no effect on other subprocesses (the hooks in
    //     libplugin_child_interpose.dylib become noops).  But currently it
    //     gets set when launching any kind of subprocess.
    //
    // Trigger "dyld interposing" for the dylib that contains
    // plugin_child_interpose.mm.  This allows us to hook OS calls in the
    // plugin process (ones that don't work correctly in a background
    // process).  Don't break any other "dyld interposing" that has already
    // been set up by whatever may have launched the browser.
    const char* prevInterpose = PR_GetEnv("DYLD_INSERT_LIBRARIES");
    nsCString interpose;
    if (prevInterpose && strlen(prevInterpose) > 0) {
      interpose.Assign(prevInterpose);
      interpose.Append(':');
    }
    interpose.Append(path.get());
    interpose.AppendLiteral("/libplugin_child_interpose.dylib");
    newEnvVars["DYLD_INSERT_LIBRARIES"] = interpose.get();
# endif  // OS_LINUX
  }
#endif  // OS_LINUX || OS_MACOSX

  FilePath exePath;
  BinaryPathType pathType = GetPathToBinary(exePath, mProcessType);

  // remap the IPC socket fd to a well-known int, as the OS does for
  // STDOUT_FILENO, for example
  int srcChannelFd, dstChannelFd;
  channel().GetClientFileDescriptorMapping(&srcChannelFd, &dstChannelFd);
  mFileMap.push_back(std::pair<int,int>(srcChannelFd, dstChannelFd));

  // no need for kProcessChannelID, the child process inherits the
  // other end of the socketpair() from us

  std::vector<std::string> childArgv;

  childArgv.push_back(exePath.value());

  if (pathType == BinaryPathType::Self) {
    childArgv.push_back("-contentproc");
  }

  childArgv.insert(childArgv.end(), aExtraOpts.begin(), aExtraOpts.end());

  if (Omnijar::IsInitialized()) {
    // Make sure that child processes can find the omnijar
    // See XRE_InitCommandLine in nsAppRunner.cpp
    newEnvVars["UXP_CUSTOM_OMNI"] = 1;
    nsCOMPtr<nsIFile> file = Omnijar::GetPath(Omnijar::GRE);
#ifdef XP_WIN
    nsString path;
    nsAutoCString childPath;
    if (file && NS_SUCCEEDED(file->GetPath(path))) {
      CopyUTF16toUTF8(path, childPath);
      childArgv.push_back("-greomni");
      childArgv.push_back(childPath.get());
    }
    file = Omnijar::GetPath(Omnijar::APP);
    if (file && NS_SUCCEEDED(file->GetPath(path))) {
      CopyUTF16toUTF8(path, childPath);
      childArgv.push_back("-appomni");
      childArgv.push_back(childPath.get());
    }
#else
    nsAutoCString path;
    if (file && NS_SUCCEEDED(file->GetNativePath(path))) {
      childArgv.push_back("-greomni");
      childArgv.push_back(path.get());
    }
    file = Omnijar::GetPath(Omnijar::APP);
    if (file && NS_SUCCEEDED(file->GetNativePath(path))) {
      childArgv.push_back("-appomni");
      childArgv.push_back(path.get());
    }
#endif
  }

  // Add the application directory path (-appdir path)
  AddAppDirToCommandLine(childArgv);

  childArgv.push_back(pidstring);

  childArgv.push_back(childProcessType);

  base::LaunchApp(childArgv, mFileMap,
#if defined(OS_LINUX) || defined(OS_MACOSX) || defined(OS_BSD) || defined(OS_SOLARIS)
                  newEnvVars, privs,
#endif
                  false, &process, arch);

  // We're in the parent and the child was launched. Close the child FD in the
  // parent as soon as possible, which will allow the parent to detect when the
  // child closes its FD (either due to normal exit or due to crash).
  GetChannel()->CloseClientFileDescriptor();

//--------------------------------------------------
#elif defined(OS_WIN)

  FilePath exePath;
  BinaryPathType pathType = GetPathToBinary(exePath, mProcessType);

  CommandLine cmdLine(exePath.ToWStringHack());

  if (pathType == BinaryPathType::Self) {
    cmdLine.AppendLooseValue(UTF8ToWide("-contentproc"));
  }

  cmdLine.AppendSwitchWithValue(switches::kProcessChannelID, channel_id());

  for (std::vector<std::string>::iterator it = aExtraOpts.begin();
       it != aExtraOpts.end();
       ++it) {
      cmdLine.AppendLooseValue(UTF8ToWide(*it));
  }

  if (Omnijar::IsInitialized()) {
    // Make sure the child process can find the omnijar
    // See XRE_InitCommandLine in nsAppRunner.cpp
    nsAutoString path;
    nsCOMPtr<nsIFile> file = Omnijar::GetPath(Omnijar::GRE);
    if (file && NS_SUCCEEDED(file->GetPath(path))) {
      cmdLine.AppendLooseValue(UTF8ToWide("-greomni"));
      cmdLine.AppendLooseValue(path.get());
    }
    file = Omnijar::GetPath(Omnijar::APP);
    if (file && NS_SUCCEEDED(file->GetPath(path))) {
      cmdLine.AppendLooseValue(UTF8ToWide("-appomni"));
      cmdLine.AppendLooseValue(path.get());
    }
  }

  // Add the application directory path (-appdir path)
  AddAppDirToCommandLine(cmdLine);

  // XXX Command line params past this point are expected to be at
  // the end of the command line string, and in a specific order.
  // See XRE_InitChildProcess in nsEmbedFunction.

  // Win app model id
  cmdLine.AppendLooseValue(mGroupId.get());

  // Process id
  cmdLine.AppendLooseValue(UTF8ToWide(pidstring));

  // Process type
  cmdLine.AppendLooseValue(UTF8ToWide(childProcessType));

  {
    base::LaunchApp(cmdLine, false, false, &process);
  }

#else
#  error Sorry
#endif

  if (!process) {
    return false;
  }

  if (!OpenPrivilegedHandle(base::GetProcId(process))
#ifdef XP_WIN
      // If we failed in opening the process handle, try harder by duplicating
      // one.
      && !::DuplicateHandle(::GetCurrentProcess(), process,
                            ::GetCurrentProcess(), &mChildProcessHandle,
                            PROCESS_DUP_HANDLE | PROCESS_TERMINATE |
                            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                            SYNCHRONIZE,
                            FALSE, 0)
#endif
     ) {
    NS_RUNTIMEABORT("cannot open handle to child process");
  }
  MonitorAutoLock lock(mMonitor);
  mProcessState = PROCESS_CREATED;
  lock.Notify();

  return true;
}

bool
GeckoChildProcessHost::OpenPrivilegedHandle(base::ProcessId aPid)
{
  if (mChildProcessHandle) {
    MOZ_ASSERT(aPid == base::GetProcId(mChildProcessHandle));
    return true;
  }

  return base::OpenPrivilegedProcessHandle(aPid, &mChildProcessHandle);
}

void
GeckoChildProcessHost::OnChannelConnected(int32_t peer_pid)
{
  if (!OpenPrivilegedHandle(peer_pid)) {
    NS_RUNTIMEABORT("can't open handle to child process");
  }
  MonitorAutoLock lock(mMonitor);
  mProcessState = PROCESS_CONNECTED;
  lock.Notify();
}

void
GeckoChildProcessHost::OnMessageReceived(IPC::Message&& aMsg)
{
  // We never process messages ourself, just save them up for the next
  // listener.
  mQueue.push(Move(aMsg));
}

void
GeckoChildProcessHost::OnChannelError()
{
  // Update the process state to an error state if we have a channel
  // error before we're connected. This fixes certain failures,
  // but does not address the full range of possible issues described
  // in the FIXME comment below.
  MonitorAutoLock lock(mMonitor);
  if (mProcessState < PROCESS_CONNECTED) {
    mProcessState = PROCESS_ERROR;
    lock.Notify();
  }
  // FIXME/bug 773925: save up this error for the next listener.
}

void
GeckoChildProcessHost::GetQueuedMessages(std::queue<IPC::Message>& queue)
{
  // If this is called off the IO thread, bad things will happen.
  DCHECK(MessageLoopForIO::current());
  swap(queue, mQueue);
  // We expect the next listener to take over processing of our queue.
}

bool GeckoChildProcessHost::sRunSelfAsContentProc(false);
