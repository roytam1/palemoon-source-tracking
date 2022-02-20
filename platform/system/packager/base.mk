# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# == | Setup | ========================================================================================================

ifndef GREPKGR_BASE_MK_INCLUDED
GREPKGR_BASE_MK_INCLUDED := 1
else
$(error GRE Packager base.mk may only be included once)
endif # GREPKGR_BASE_MK_INCLUDED

# ---------------------------------------------------------------------------------------------------------------------

# These vars are here so that if paths change the number of places that need to be changed is minimal.
MOZINST_PATH := $(MOZILLA_DIR)/system/installer
GREPKGR_PATH := $(MOZILLA_DIR)/system/packager

# ---------------------------------------------------------------------------------------------------------------------

# This entire section contains common values normally spread all over hell and back but mostly originate from
# the multiple application installer makefiles. Which was redundantly redundant so we collect them here.

DEFINES += \
	-DDLL_PREFIX=$(DLL_PREFIX) \
	-DDLL_SUFFIX=$(DLL_SUFFIX) \
	-DBIN_SUFFIX=$(BIN_SUFFIX) \
	-DMOZ_APP_NAME=$(MOZ_APP_NAME) \
	-DMOZ_APP_DISPLAYNAME=$(MOZ_APP_NAME) \
	-DMOZ_APP_ID=$(MOZ_APP_ID) \
	-DMOZ_APP_VERSION=$(MOZ_APP_VERSION) \
	-DMOZ_APP_MAXVERSION=$(MOZ_APP_MAXVERSION) \
	-DMOZILLA_VERSION=$(MOZILLA_VERSION) \
	-DBINPATH=bin \
	-DPREF_DIR=$(PREF_DIR) \
	-DMOZ_ICU_VERSION=$(MOZ_ICU_VERSION) \
	-DMOZ_ICU_DBG_SUFFIX=$(MOZ_ICU_DBG_SUFFIX) \
	-DICU_DATA_FILE=$(ICU_DATA_FILE) \
	-DMOZ_CHILD_PROCESS_NAME=$(MOZ_CHILD_PROCESS_NAME) \
  $(NULL)

export USE_ELF_HACK ELF_HACK_FLAGS

ifdef NO_PKG_DEFAULT_FILES
NO_PKG_FILES += \
	certutil$(BIN_SUFFIX) \
	modutil$(BIN_SUFFIX) \
	pk12util$(BIN_SUFFIX) \
	shlibsign$(BIN_SUFFIX) \
	xpcshell$(BIN_SUFFIX) \
	$(NULL)
endif

NON_OMNIJAR_FILES += \
	defaults/profile/chrome/userChrome-example.css \
	defaults/profile/chrome/userContent-example.css \
	defaults/profile/mimeTypes.rdf \
	$(NULL)

MOZ_PKG_FATAL_WARNINGS = 1

ifeq ($(MOZ_CHROME_FILE_FORMAT),jar)
DEFINES += -DJAREXT=.jar
else
ifeq ($(MOZ_PACKAGER_FORMAT),omni)
DEFINES += -DMOZ_OMNIJAR=1
endif
DEFINES += -DJAREXT=
endif

ifdef MOZ_DEBUG
DEFINES += -DMOZ_DEBUG=1
endif

ifdef ENABLE_TESTS
DEFINES += -DENABLE_TESTS=1
endif

ifdef MOZ_ANGLE_RENDERER
DEFINES += -DMOZ_ANGLE_RENDERER=$(MOZ_ANGLE_RENDERER)
ifdef MOZ_D3DCOMPILER_VISTA_DLL
DEFINES += -DMOZ_D3DCOMPILER_VISTA_DLL=$(MOZ_D3DCOMPILER_VISTA_DLL)
endif
ifdef MOZ_D3DCOMPILER_XP_DLL
DEFINES += -DMOZ_D3DCOMPILER_XP_DLL=$(MOZ_D3DCOMPILER_XP_DLL)
endif
endif

ifdef MOZ_ENABLE_GNOME_COMPONENT
DEFINES += -DMOZ_ENABLE_GNOME_COMPONENT=1
endif

ifneq (,$(filter gtk%,$(MOZ_WIDGET_TOOLKIT)))
DEFINES += -DMOZ_GTK=1
ifeq ($(MOZ_WIDGET_TOOLKIT),gtk3)
DEFINES += -DMOZ_GTK3=1
endif
endif

ifdef MOZ_FOLD_LIBS
DEFINES += -DMOZ_FOLD_LIBS=1
endif

ifdef _MSC_VER
DEFINES += -D_MSC_VER=$(_MSC_VER)
endif

ifdef MOZ_SYSTEM_NSPR
DEFINES += -DMOZ_SYSTEM_NSPR=1
endif

ifdef MOZ_SYSTEM_NSS
DEFINES += -DMOZ_SYSTEM_NSS=1
endif

ifdef NSS_DISABLE_DBM
DEFINES += -DNSS_DISABLE_DBM=1
endif

ifdef NECKO_WIFI
DEFINES += -DNECKO_WIFI=1
endif

ifdef GKMEDIAS_SHARED_LIBRARY
DEFINES += -DGKMEDIAS_SHARED_LIBRARY
endif

# Set MSVC dlls version to package, if any.
# With VS2015+ it does not make sense to define the ucrt libs without
# the base c++ libs and vice versa. 
ifdef MOZ_NO_DEBUG_RTL
ifdef WIN32_REDIST_DIR
ifdef WIN_UCRT_REDIST_DIR
DEFINES += \
	-DMOZ_PACKAGE_MSVC_DLLS=1 \
	-DMSVC_C_RUNTIME_DLL=$(MSVC_C_RUNTIME_DLL) \
	-DMSVC_CXX_RUNTIME_DLL=$(MSVC_CXX_RUNTIME_DLL) \
	-DMOZ_PACKAGE_WIN_UCRT_DLLS=1 \
	$(NULL)
endif
endif
endif

ifeq ($(OS_ARCH),WINNT)
DEFINES += -DMOZ_SHARED_MOZGLUE=1
endif

ifdef MOZ_SYSTEM_ICU
DEFINES += -DMOZ_SYSTEM_ICU
endif

ifdef MOZ_ICU_DATA_ARCHIVE
DEFINES += -DMOZ_ICU_DATA_ARCHIVE
endif

ifdef MOZ_UPDATER
DEFINES += -DMOZ_UPDATER=1
endif

ifneq (,$(wildcard $(DIST)/bin/application.ini))
BUILDID = $(shell $(PYTHON) $(MOZILLA_DIR)/config/printconfigsetting.py $(DIST)/bin/application.ini App BuildID)
else
BUILDID = $(shell $(PYTHON) $(MOZILLA_DIR)/config/printconfigsetting.py $(DIST)/bin/platform.ini Build BuildID)
endif

# ---------------------------------------------------------------------------------------------------------------------

# Package Application Name
ifndef PKG_APP_NAME
PKG_APP_NAME := $(MOZ_APP_NAME)
endif

# Package Application Version
ifndef PKG_APP_VERSION
PKG_APP_VERSION := $(MOZ_APP_VERSION)
endif

# ---------------------------------------------------------------------------------------------------------------------

# TARGET_OS/TARGET_CPU may be un-intuitive, so we hardcode some special formats
ifndef PKG_BUILD_TARGET
PKG_BUILD_TARGET := $(TARGET_OS)-$(TARGET_CPU)
ifeq ($(OS_ARCH),WINNT)
ifeq ($(TARGET_CPU),x86_64)
PKG_BUILD_TARGET := win64
else
PKG_BUILD_TARGET := win32
endif
endif
ifeq ($(TARGET_OS),linux-gnu)
PKG_BUILD_TARGET := linux-$(TARGET_CPU)
endif
ifeq ($(OS_ARCH),SunOS)
PKG_BUILD_TARGET := sunos-$(TARGET_CPU)
endif
endif # PKG_BUILD_TARGET

# ---------------------------------------------------------------------------------------------------------------------

# Support the older var in this case but only if the newer one isn't set.
# We use this when we want to offer different flavors like gtk2 vs gtk3 or different flavors of sunos.
ifndef PKG_BUILD_FLAVOR
ifdef MOZ_PKG_SPECIAL
PKG_BUILD_TARGET := $(PKG_BUILD_TARGET)-$(MOZ_PKG_SPECIAL)
endif # MOZ_PKG_SPECIAL
else
PKG_BUILD_TARGET := $(PKG_BUILD_TARGET)-$(PKG_BUILD_FLAVOR)
endif # PKG_BUILD_FLAVOR

# ---------------------------------------------------------------------------------------------------------------------

ifndef PGK_BASENAME
PKG_BASENAME := $(PKG_APP_NAME)-$(PKG_APP_VERSION).$(PKG_BUILD_TARGET)
endif

# XXXTobin: It would be nice if we could have a generic stage directory but various tools won't allow it.
PKG_STAGE_DIR = $(PKG_APP_NAME)

# ---------------------------------------------------------------------------------------------------------------------

ifeq ($(OS_ARCH),WINNT)
# Archiver command and filename
PKG_ARCHIVER_CMD = 7z a -t7z -m0=lzma2 -mx=9 -aoa -bb3
PKG_ARCHIVE_FILENAME = $(PKG_BASENAME).7z

# Windows Symbol stage directory and archive filename
PKG_SYMBOLS_STAGE_DIR = $(PKG_STAGE_DIR)-symbols
PKG_SYMBOLS_FILENAME = $(PKG_BASENAME)-symbols.7z

# NSIS-related vars and defines.
ifdef MAKENSISU
DEFINES += -DHAVE_MAKENSISU=1
CONFIG_DIR = instgen
ifndef SFX_MODULE
SFX_MODULE = $(MOZILLA_SRCDIR)/other-licenses/7zstub/gre/7zSD.sfx
endif # SFX_MODULE
endif # MAKENSISU

# Installer filename
PKG_INSTALLER_FILENAME = $(PKG_BASENAME).installer.exe

# Generic OS name when specifics aren't as important
PKG_GENERIC_OS = windows
else
# Archiver command and filename for not-windows target operating systems
PKG_ARCHIVER_CMD = $(TAR) cfJv
PKG_ARCHIVE_FILENAME = $(PKG_BASENAME).tar.xz

# Generic OS name when specifics aren't as important
PKG_GENERIC_OS = unix
endif

# We only use the complete mar patch file
PKG_UPDATE_FILENAME = $(PKG_BASENAME).mar

# This is the mozbuildinfo json file that contains a number of mozinfra build values
# We also use this in our infra because it exists..
PKG_JSON_FILENAME = $(PKG_BASENAME).json

# Vars that are used in faking xpi-stage to create useful packages that are normally ONLY omnijar'd
PKG_XPI_L10N_STAGE_DIR = xpi-stage/grepkgr-locale
PKG_XPI_L10N_FILENAME := $(PKG_APP_NAME)-$(PKG_APP_VERSION).en-US.langpack.zip
PKG_XPI_SKIN_STAGE_DIR = xpi-stage/grepkgr-skin
PKG_XPI_SKIN_FILENAME := $(PKG_APP_NAME)-$(PKG_APP_VERSION).$(PKG_GENERIC_OS).theme.zip

# =====================================================================================================================
