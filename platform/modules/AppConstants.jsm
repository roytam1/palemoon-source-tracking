#filter substitution
#include @TOPOBJDIR@/source-repo.h
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// XXXTobin: This file is deprecated but too much annoying shit still uses it.
// Half of what it provides is just hard-coded to false.
// So don't fuckin trust it, moron.

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Services", "resource://gre/modules/Services.jsm");

this.EXPORTED_SYMBOLS = ["AppConstants"];

// Immutable for export.
this.AppConstants = Object.freeze({

  // We don't use nightly or alpha channels here. Flag all as release.
  NIGHTLY_BUILD: false,
  RELEASE_OR_BETA: true,

  ACCESSIBILITY:
#ifdef ACCESSIBILITY
  true,
#else
  false,
#endif
  
#ifdef MC_OFFICIAL
  MOZILLA_OFFICIAL: true,
  MC_OFFICIAL: true,
#else
  MOZILLA_OFFICIAL: false,
  MC_OFFICIAL: false,
#endif

  MOZ_OFFICIAL_BRANDING:
#ifdef MOZ_OFFICIAL_BRANDING
  true,
#else
  false,
#endif

  MOZ_UPDATER:
#ifdef MOZ_UPDATER
  true,
#else
  false,
#endif

  MOZ_WIDGET_GTK:
#ifdef MOZ_WIDGET_GTK
  true,
#else
  false,
#endif

  XP_UNIX:
#ifdef XP_UNIX
  true,
#else
  false,
#endif

  DEBUG:
#ifdef DEBUG
  true,
#else
  false,
#endif

  ASAN:
#ifdef MOZ_ASAN
  true,
#else
  false,
#endif

  MOZ_PLACES:
#ifdef MOZ_PLACES
  true,
#else
  false,
#endif

  MENUBAR_CAN_AUTOHIDE:
#ifdef MENUBAR_CAN_AUTOHIDE
  true,
#else
  false,
#endif

  CAN_DRAW_IN_TITLEBAR:
#ifdef MOZ_CAN_DRAW_IN_TITLEBAR
  true,
#else
  false,
#endif

  MOZ_TOOLKIT_SEARCH:
#ifdef MOZ_TOOLKIT_SEARCH
  true,
#else
  false,
#endif

  // URL to the hg revision this was built from (e.g.
  // "https://hg.mozilla.org/mozilla-central/rev/6256ec9113c1")
  // On unofficial builds, this is an empty string.
#ifndef MOZ_SOURCE_URL
#define MOZ_SOURCE_URL
#endif
  SOURCE_REVISION_URL: "@MOZ_SOURCE_URL@",

  HAVE_USR_LIB64_DIR:
#ifdef HAVE_USR_LIB64_DIR
    true,
#else
    false,
#endif

  HAVE_SHELL_SERVICE:
#ifdef HAVE_SHELL_SERVICE
    true,
#else
    false,
#endif

  DLL_PREFIX: "@DLL_PREFIX@",
  DLL_SUFFIX: "@DLL_SUFFIX@",

  MOZ_APP_NAME: "@MOZ_APP_NAME@",
  MOZ_APP_VERSION: "@MOZ_APP_VERSION@",
  MOZ_APP_VERSION_DISPLAY: "@MOZ_APP_VERSION_DISPLAY@",
  MOZ_BUILD_APP: "@MOZ_BUILD_APP@",
  MOZ_MACBUNDLE_NAME: "FakeMacBundleName.app",
  MOZ_UPDATE_CHANNEL: "@MOZ_UPDATE_CHANNEL@",
  INSTALL_LOCALE: "@AB_CD@",
  MOZ_WIDGET_TOOLKIT: "@MOZ_WIDGET_TOOLKIT@",
  ANDROID_PACKAGE_NAME: "name.package.android.fake",
  MOZ_B2G_VERSION: "2.6.0.0-prerelease",
  MOZ_B2G_OS_NAME: "Boot2Failure",

  DEBUG_JS_MODULES: "@DEBUG_JS_MODULES@",

  MOZ_DEV_EDITION: false,
  MOZ_SERVICES_HEALTHREPORT: false,
  MOZ_DATA_REPORTING: false,
  MOZ_SANDBOX: false,
  MOZ_CONTENT_SANDBOX: false,
  MOZ_TELEMETRY_REPORTING: false,
  MOZ_TELEMETRY_ON_BY_DEFAULT: false,
  MOZ_SERVICES_CLOUDSYNC: false,
  MOZ_SWITCHBOARD: false,
  MOZ_WEBRTC: false,
  MOZ_B2G: false,
  MOZ_CRASHREPORTER: false,
  MOZ_VERIFY_MAR_SIGNATURE: false,
  MOZ_MAINTENANCE_SERVICE: false,
  E10S_TESTING_ONLY: false,
  MOZ_B2G_RIL: false,
  MOZ_GRAPHENE: false,
  MOZ_SYSTEM_NSS: false,
  MOZ_REQUIRE_SIGNING: false,
  MOZ_ANDROID_HISTORY: false,
  MOZ_ENABLE_PROFILER_SPS: false,
  MOZ_ANDROID_ACTIVITY_STREAM: false,

  platform:
#ifdef MOZ_WIDGET_GTK
  "linux",
#elif XP_WIN
  "win",
#elif XP_LINUX
  "linux",
#else
  "other",
#endif

  isPlatformAndVersionAtLeast(platform, version) {
    let platformVersion = Services.sysinfo.getProperty("version");
    return platform == this.platform &&
           Services.vc.compare(platformVersion, version) >= 0;
  },

  isPlatformAndVersionAtMost(platform, version) {
    let platformVersion = Services.sysinfo.getProperty("version");
    return platform == this.platform &&
           Services.vc.compare(platformVersion, version) <= 0;
  },
});
