/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsnativestack.h"

#ifdef XP_WIN
# include "jswin.h"

#elif defined(XP_UNIX)
# include <pthread.h>

# if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#  include <pthread_np.h>
# endif

# if defined(XP_LINUX) && defined(__GLIBC__)
#  include <dlfcn.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define gettid() static_cast<pid_t>(syscall(SYS_gettid))
# endif

#else
# error "Unsupported platform"

#endif

#if defined(XP_WIN)

void*
js::GetNativeStackBaseImpl()
{
# if defined(_M_IX86) && defined(_MSC_VER)
    /*
     * offset 0x18 from the FS segment register gives a pointer to
     * the thread information block for the current thread
     */
    NT_TIB* pTib;
    __asm {
        MOV EAX, FS:[18h]
        MOV pTib, EAX
    }
    return static_cast<void*>(pTib->StackBase);

# elif defined(_M_X64)
    PNT_TIB64 pTib = reinterpret_cast<PNT_TIB64>(NtCurrentTeb());
    return reinterpret_cast<void*>(pTib->StackBase);

# elif defined(_M_ARM)
    PNT_TIB pTib = reinterpret_cast<PNT_TIB>(NtCurrentTeb());
    return static_cast<void*>(pTib->StackBase);

# elif defined(_WIN32) && defined(__GNUC__)
    NT_TIB* pTib;
    asm ("movl %%fs:0x18, %0\n" : "=r" (pTib));
    return static_cast<void*>(pTib->StackBase);

# endif
}

#elif defined(XP_SOLARIS)

#include <ucontext.h>

JS_STATIC_ASSERT(JS_STACK_GROWTH_DIRECTION < 0);

void* 
js::GetNativeStackBaseImpl()
{
	 stack_t st;
	 stack_getbounds(&st);
	 return static_cast<char*>(st.ss_sp) + st.ss_size;
}

#elif defined(XP_LINUX) && defined(__GLIBC__)
void*
js::GetNativeStackBaseImpl()
{
    // On the main thread, get stack base from glibc's __libc_stack_end rather than pthread APIs
    // to avoid filesystem calls /proc/self/maps.  Non-main threads spawned with pthreads can read
    // this information directly from their pthread struct, but when using the pthreads API, the
    // main thread must go parse /proc/self/maps to figure the mapped stack address space ranges.
    // We want to avoid reading from /proc/ so that the application can run in restricted
    // environments where /proc may not be mounted (e.g. chroot).
    if (gettid() == getpid()) {
        void** pLibcStackEnd = (void**)dlsym(RTLD_DEFAULT, "__libc_stack_end");

        // If __libc_stack_end is not found, architecture specific frame pointer hopping will need
        // to be implemented.
        MOZ_RELEASE_ASSERT(pLibcStackEnd, "__libc_stack_end unavailable, unable to setup stack range for JS.");
        void* stackBase = *pLibcStackEnd;
        MOZ_RELEASE_ASSERT(stackBase, "Invalid stack base, unable to setup stack range for JS.");

        // We don't need to fix stackBase, as it already roughly points to beginning of the stack.
        return stackBase;
    }

    // Non-main threads have the required info stored in memory, so no filesystem calls are made.
    pthread_t thread = pthread_self();
    pthread_attr_t sattr;
    pthread_attr_init(&sattr);
    pthread_getattr_np(thread, &sattr);

    // stackBase will be the *lowest* address on all architectures.
    void* stackBase = nullptr;
    size_t stackSize = 0;
    int rc = pthread_attr_getstack(&sattr, &stackBase, &stackSize);
    if (rc) {
        MOZ_CRASH("Call to pthread_attr_getstack failed, unable to setup stack range for JS.");
    }
    MOZ_RELEASE_ASSERT(stackBase, "Invalid stack base, unable to setup stack range for JS.");
    pthread_attr_destroy(&sattr);

# if JS_STACK_GROWTH_DIRECTION > 0
    return stackBase;
# else
    return static_cast<char*>(stackBase) + stackSize;
# endif
}

#else /* XP_UNIX */

void*
js::GetNativeStackBaseImpl()
{
    pthread_t thread = pthread_self();
    pthread_attr_t sattr;
    pthread_attr_init(&sattr);
#  if defined(__OpenBSD__)
    stack_t ss;
#  elif defined(PTHREAD_NP_H) || defined(_PTHREAD_NP_H_) || defined(NETBSD)
    /* e.g. on FreeBSD 4.8 or newer, neundorf@kde.org */
    pthread_attr_get_np(thread, &sattr);
#  else
    /*
     * FIXME: this function is non-portable;
     * other POSIX systems may have different np alternatives
     */
    pthread_getattr_np(thread, &sattr);
#  endif

    void* stackBase = 0;
    size_t stackSize = 0;
    int rc;
# if defined(__OpenBSD__)
    rc = pthread_stackseg_np(pthread_self(), &ss);
    stackBase = (void*)((size_t) ss.ss_sp - ss.ss_size);
    stackSize = ss.ss_size;
# else
    // Use the default pthread_attr_getstack() call. Note that this function
    // differs between libc implementations and could imply /proc access etc.
    // which may not work in restricted environments.
    rc = pthread_attr_getstack(&sattr, &stackBase, &stackSize);
# endif
    if (rc) {
        MOZ_CRASH("Call to pthread_attr_getstack failed, unable to setup stack range for JS.");
    }
    MOZ_RELEASE_ASSERT(stackBase, "Invalid stack base, unable to setup stack range for JS.");
    pthread_attr_destroy(&sattr);

#  if JS_STACK_GROWTH_DIRECTION > 0
    return stackBase;
#  else
    return static_cast<char*>(stackBase) + stackSize;
#  endif
}

#endif /* !XP_WIN */
