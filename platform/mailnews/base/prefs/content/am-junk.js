/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
Components.utils.import("resource:///modules/mailServices.js");
Components.utils.import("resource:///modules/iteratorUtils.jsm");
Components.utils.import("resource:///modules/MailUtils.js");

var gDeferredToAccount = "";

function onInit(aPageId, aServerId)
{
  // manually adjust several pref UI elements
  document.getElementById('server.spamLevel.visible').setAttribute("checked",
    document.getElementById('server.spamLevel').value > 0);

  let deferredToURI = null;
  if (gDeferredToAccount)
    deferredToURI = MailServices.accounts
                                .getAccount(gDeferredToAccount)
                                .incomingServer.serverURI;

  let spamActionTargetAccountElement =
    document.getElementById("server.spamActionTargetAccount");
  let spamActionTargetFolderElement =
    document.getElementById("server.spamActionTargetFolder");

  let spamActionTargetAccount = spamActionTargetAccountElement.value;
  let spamActionTargetFolder = spamActionTargetFolderElement.value;

  let moveOnSpamCheckbox = document.getElementById("server.moveOnSpam");
  let moveOnSpamValue = moveOnSpamCheckbox.checked;

  // Check if there are any invalid junk targets and fix them.
  [ spamActionTargetAccount, spamActionTargetFolder, moveOnSpamValue ] =
    sanitizeJunkTargets(spamActionTargetAccount,
                        spamActionTargetFolder,
                        deferredToURI || aServerId,
                        document.getElementById("server.moveTargetMode").value,
                        MailUtils.getFolderForURI(aServerId, false).server.spamSettings,
                        moveOnSpamValue);

  spamActionTargetAccountElement.value = spamActionTargetAccount;
  spamActionTargetFolderElement.value = spamActionTargetFolder;
  moveOnSpamCheckbox.checked = moveOnSpamValue;

  let server = MailUtils.getFolderForURI(spamActionTargetAccount, false);
  document.getElementById("actionAccountPopup").selectFolder(server);

  let folder = MailUtils.getFolderForURI(spamActionTargetFolder, true);
  document.getElementById("actionFolderPopup").selectFolder(folder);

  var currentArray = [];
  if (document.getElementById("server.useWhiteList").checked)
    currentArray = document.getElementById("server.whiteListAbURI").value.split(" ");

  // set up the whitelist UI
  var wList = document.getElementById("whiteListAbURI");
  // Ensure the whitelist is empty
  for (let i = wList.itemCount - 1; i >= 0; i--) {
    wList.removeItemAt(i);
  }

  // Populate the listbox with address books
  let abItems = [];
  for (let ab in fixIterator(MailServices.ab.directories,
                             Components.interfaces.nsIAbDirectory)) {
    // We skip mailing lists and remote address books.
    if (ab.isMailList || ab.isRemote)
      continue;

    abItems.push({ label: ab.dirName, URI: ab.URI });
  }

  // Sort the list
  function sortFunc(a, b) {
    return a.label.localeCompare(b.label);
  }
  abItems.sort(sortFunc);

  // And then append each item to the listbox
  for (let abItem of abItems) {
    let item = wList.appendItem(abItem.label, abItem.URI);
    item.setAttribute("type", "checkbox");
    item.setAttribute("class", "listitem-iconic");

    // Due to bug 448582, we have to use setAttribute to set the
    // checked value of the listitem.
    item.setAttribute("checked", currentArray.includes(abItem.URI));
  }

  // enable or disable the whitelist
  onAdaptiveJunkToggle();

  // set up trusted IP headers
  var serverFilterList = document.getElementById("useServerFilterList");
  serverFilterList.value =
    document.getElementById("server.serverFilterName").value;
  if (!serverFilterList.selectedItem)
    serverFilterList.selectedIndex = 0;

  // enable or disable the useServerFilter checkbox
  onCheckItem("useServerFilterList", ["server.useServerFilter"]);

  updateJunkTargetsAndRetention();
}

function onPreInit(account, accountValues)
{
  if (getAccountValue(account, accountValues, "server", "type", null, false) == "pop3")
    gDeferredToAccount = getAccountValue(account, accountValues,
                                         "pop3", "deferredToAccount",
                                         null, false);

  buildServerFilterMenuList();
}

/**
 * Called when someone checks or unchecks the adaptive junk mail checkbox.
 * set the value of the hidden element accordingly
 *
 * @param aValue  the boolean value of the checkbox
 */
function updateSpamLevel(aValue)
{
  document.getElementById('server.spamLevel').value = aValue ? 100 : 0;
  onAdaptiveJunkToggle();
}

/**
 * Propagate changes to the server filter menu list back to
 * our hidden wsm element.
 */
function onServerFilterListChange()
{
  document.getElementById('server.serverFilterName').value =
    document.getElementById("useServerFilterList").value;
}

/**
 * Called when someone checks or unchecks the adaptive junk mail checkbox.
 * We need to enable or disable the whitelist accordingly.
 */
function onAdaptiveJunkToggle()
{
  onCheckItem("whiteListAbURI", ["server.spamLevel.visible"]);
  onCheckItem("whiteListLabel", ["server.spamLevel.visible"]);

  // Enable/disable individual listbox rows.
  // Setting enable/disable on the parent listbox does not seem to work.
  let wList = document.getElementById("whiteListAbURI");
  let wListDisabled = wList.disabled;

  for (let i = 0; i < wList.getRowCount(); i++) {
    wList.getItemAtIndex(i).setAttribute("disabled", wListDisabled);
  }
}

/**
 * Called when someone checks or unchecks the "move new junk messages to"
 * Enable/disable the radio group accordingly.
 */
function updateJunkTargetsAndRetention() {
  onCheckItem("server.moveTargetMode", ["server.moveOnSpam"]);
  updateJunkTargets();
  onCheckItem("server.purgeSpam", ["server.moveOnSpam"]);
  document.getElementById("purgeLabel").disabled =
    document.getElementById("server.purgeSpam").disabled;
  updateJunkRetention();
}

/**
 * Enable/disable the folder pickers depending on which radio item is selected.
 */
function updateJunkTargets() {
  onCheckItem("actionTargetAccount", ["server.moveOnSpam", "moveTargetMode0"]);
  onCheckItem("actionTargetFolder",  ["server.moveOnSpam", "moveTargetMode1"]);
}

/**
 * Enable/disable the junk deletion interval depending on the state
 * of the controlling checkbox.
 */
function updateJunkRetention() {
  onCheckItem("server.purgeSpamInterval", ["server.purgeSpam", "server.moveOnSpam"]);
}

function onSave()
{
  onSaveWhiteList();
}

/**
 * Propagate changes to the whitelist menu list back to
 * our hidden wsm element.
 */
function onSaveWhiteList()
{
  var wList = document.getElementById("whiteListAbURI");
  var wlArray = [];

  for (var i = 0; i < wList.getRowCount(); i++)
  {
    // Due to bug 448582, do not trust any properties of the listitems
    // as they may not return the right value or may even not exist.
    // Always get the attributes only.
    var wlNode = wList.getItemAtIndex(i);
    if (wlNode.getAttribute("checked") == "true") {
      let abURI = wlNode.getAttribute("value");
      wlArray.push(abURI);
    }
  }
  var wlValue = wlArray.join(" ");
  document.getElementById("server.whiteListAbURI").setAttribute("value", wlValue);
  document.getElementById("server.useWhiteList").checked = (wlValue != "");
}

/**
 * Called when a new value is chosen in one of the junk target folder pickers.
 * Sets the menu label according to the folder name.
 */
function onActionTargetChange(aEvent, aWSMElementId)
{
  let folder = aEvent.target._folder;
  document.getElementById(aWSMElementId).value = folder.URI;
  document.getElementById("actionFolderPopup").selectFolder(folder);
}

/**
 * Enumerates over the "ISPDL" directories, calling buildServerFilterListFromDir
 * for each one.
 */
function buildServerFilterMenuList()
{
  const KEY_ISP_DIRECTORY_LIST = "ISPDL";
  let ispHeaderList = document.getElementById("useServerFilterList");

  // Ensure the menulist is empty.
  ispHeaderList.removeAllItems();

  // Now walk through the isp directories looking for sfd files.
  let ispDirectories = Services.dirsvc.get(KEY_ISP_DIRECTORY_LIST,
                                           Components.interfaces.nsISimpleEnumerator);

  let menuEntries = [];
  while (ispDirectories.hasMoreElements())
  {
    let ispDirectory = ispDirectories.getNext()
                                     .QueryInterface(Components.interfaces.nsIFile);
    if (ispDirectory)
      menuEntries.push.apply(menuEntries, buildServerFilterListFromDir(ispDirectory, menuEntries));
  }

  menuEntries.sort((a, b) => a.localeCompare(b));
  for (let entry of menuEntries) {
    ispHeaderList.appendItem(entry, entry);
  }
}

/**
 * Helper function called by buildServerFilterMenuList. Enumerates over the
 * passed in directory looking for .sfd files. For each entry found, it gets
 * appended to the menu list.
 *
 * @param aDir              directory to look for .sfd files
 * @param aExistingEntries  Filter names already found.
 */
function buildServerFilterListFromDir(aDir, aExistingEntries)
{
  let newEntries = [];
  // Now iterate over each file in the directory looking for .sfd files.
  const kSuffix = ".sfd";
  let entries = aDir.directoryEntries
                    .QueryInterface(Components.interfaces.nsIDirectoryEnumerator);

  while (entries.hasMoreElements()) {
    let entry = entries.nextFile;
    // we only care about files that end in .sfd
    if (entry.isFile() && entry.leafName.endsWith(kSuffix)) {
      let fileName = entry.leafName.slice(0, -kSuffix.length);
      // If we've already added an item with this name, then don't add it again.
      if (aExistingEntries.indexOf(fileName) == -1)
        newEntries.push(fileName);
    }
  }
  return newEntries;
}

/**
 * Open the Preferences dialog on the Junk settings tab.
 */
function showGlobalJunkPrefs()
{
  openPrefsFromAccountManager("paneSecurity", "junkTab", null, "junk_pane");
}
