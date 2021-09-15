/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Message DB Cache manager
 */

/* :::::::: Constants and Helpers ::::::::::::::: */

this.EXPORTED_SYMBOLS = ["msgDBCacheManager"];

var Cc = Components.classes;
var Ci = Components.interfaces;
var Cu = Components.utils;

Cu.import("resource:///modules/mailServices.js");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/gloda/log4moz.js");
var log = Log4Moz.getConfiguredLogger("mailnews.database.dbcache");

/**
 */
var DBCACHE_INTERVAL_DEFAULT_MS = 60000; // 1 minute

/* :::::::: The Module ::::::::::::::: */

var msgDBCacheManager =
{
  _initialized: false,

  _msgDBCacheTimer: null,

  _msgDBCacheTimerIntervalMS: DBCACHE_INTERVAL_DEFAULT_MS,

  _dbService: null,

  /**
   * This is called on startup
   */
  init: function dbcachemgr_init()
  {
    if (this._initialized)
      return;

    this._dbService = Cc["@mozilla.org/msgDatabase/msgDBService;1"]
                        .getService(Ci.nsIMsgDBService);

    // we listen for "quit-application-granted" instead of
    // "quit-application-requested" because other observers of the
    // latter can cancel the shutdown.
    Services.obs.addObserver(this, "quit-application-granted", false);

    this.startPeriodicCheck();

    this._initialized = true;
  },

/* ........ Timer Callback ................*/

  _dbCacheCheckTimerCallback: function dbCache_CheckTimerCallback()
  {
    msgDBCacheManager.checkCachedDBs();
  },

/* ........ Observer Notification Handler ................*/

  observe: function dbCache_observe(aSubject, aTopic, aData) {
    switch (aTopic) {
    // This is observed before any windows start unloading if something other
    // than the last 3pane window closing requested the application be
    // shutdown. For example, when the user quits via the file menu.
    case "quit-application-granted":
      Services.obs.removeObserver(this, "quit-application-granted");
      this.stopPeriodicCheck();
      break;
    }
  },

/* ........ Public API ................*/

  /**
   * Stops db cache check
   */
  stopPeriodicCheck: function dbcache_stopPeriodicCheck()
  {
    if (this._dbCacheCheckTimer) {
      this._dbCacheCheckTimer.cancel();

      delete this._dbCacheCheckTimer;
      this._dbCacheCheckTimer = null;
    }
  },

  /**
   * Starts periodic db cache check
   */
  startPeriodicCheck: function dbcache_startPeriodicCheck()
  {
    if (!this._dbCacheCheckTimer) {
      this._dbCacheCheckTimer = Cc["@mozilla.org/timer;1"]
                                   .createInstance(Ci.nsITimer);

      this._dbCacheCheckTimer.initWithCallback(
                                   this._dbCacheCheckTimerCallback,
                                   this._msgDBCacheTimerIntervalMS,
                                   Ci.nsITimer.TYPE_REPEATING_SLACK);
    }
  },

  /**
   * Checks if any DBs need to be closed due to inactivity or too many of them open.
   */
  checkCachedDBs: function()
  {
    let idleLimit = Services.prefs.getIntPref("mail.db.idle_limit");
    let maxOpenDBs = Services.prefs.getIntPref("mail.db.max_open");

    // db.lastUseTime below is in microseconds while Date.now and idleLimit pref
    // is in milliseconds.
    let closeThreshold = (Date.now() - idleLimit) * 1000;
    let cachedDBs = this._dbService.openDBs;
    log.info("Periodic check of cached folder databases (DBs), count=" + cachedDBs.length);
    // Count databases that are already closed or get closed now due to inactivity.
    let numClosing = 0;
    // Count databases whose folder is open in a window.
    let numOpenInWindow = 0;
    let dbs = [];
    for (let i = 0; i < cachedDBs.length; i++) {
      let db = cachedDBs.queryElementAt(i, Ci.nsIMsgDatabase);
      if (!db.folder.databaseOpen) {
        // The DB isn't really open anymore.
        log.debug("Skipping, DB not open for folder: " + db.folder.name);
        numClosing++;
        continue;
      }

      if (MailServices.mailSession.IsFolderOpenInWindow(db.folder)) {
        // The folder is open in a window so this DB must not be closed.
        log.debug("Skipping, DB open in window for folder: " + db.folder.name);
        numOpenInWindow++;
        continue;
      }

      if (db.lastUseTime < closeThreshold)
      {
        // DB open too log without activity.
        log.debug("Closing expired DB for folder: " + db.folder.name);
        db.folder.msgDatabase = null;
        numClosing++;
        continue;
      }

      // Database eligible for closing.
      dbs.push(db);
    }
    log.info("DBs open in a window: " + numOpenInWindow + ", DBs open: " + dbs.length + ", DBs already closing: " + numClosing);
    let dbsToClose = Math.max(dbs.length - Math.max(maxOpenDBs - numOpenInWindow, 0), 0);
    if (dbsToClose > 0) {
      // Close some DBs so that we do not have more than maxOpenDBs.
      // However, we skipped DBs for folders that are open in a window
      // so if there are so many windows open, it may be possible for
      // more than maxOpenDBs folders to stay open after this loop.
      log.info("Need to close " + dbsToClose + " more DBs");
      // Order databases by lowest lastUseTime (oldest) at the end.
      dbs.sort((a, b) => b.lastUseTime - a.lastUseTime);
      while (dbsToClose > 0) {
        let db = dbs.pop();
        log.debug("Closing DB for folder: " + db.folder.name);
        db.folder.msgDatabase = null;
        dbsToClose--;
      }
    }
  },
};
