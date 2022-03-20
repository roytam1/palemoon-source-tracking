# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# == | Setup | ========================================================================================================

TOOLKIT_NSIS_FILES = \
	common.nsh \
	locale.nlf \
	locale-fonts.nsh \
	locale-rtl.nlf \
	locales.nsi \
	overrides.nsh \
	setup.ico \
	$(NULL)

CUSTOM_NSIS_PLUGINS = \
	AccessControl.dll \
	AppAssocReg.dll \
	ApplicationID.dll \
	CertCheck.dll \
	CityHash.dll \
	InetBgDL.dll \
	InvokeShellVerb.dll \
	liteFirewallW.dll \
	ServicesHelper.dll \
	ShellLink.dll \
	UAC.dll \
	$(NULL)

CUSTOM_UI = nsisui.exe

# =====================================================================================================================

# == | Makefile Targets | =============================================================================================

$(CONFIG_DIR)/7zSD.sfx:
	$(CYGWIN_WRAPPER) upx --best -o $(CONFIG_DIR)/7zSD.sfx $(SFX_MODULE)

# ---------------------------------------------------------------------------------------------------------------------

$(CONFIG_DIR)/setup.exe::
	$(INSTALL) $(addprefix $(srcdir)/,$(INSTALLER_FILES)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(DIST)/branding/,$(BRANDING_FILES)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(MOZINST_PATH)/windows/nsis/,$(TOOLKIT_NSIS_FILES)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(MOZILLA_DIR)/other-licenses/nsis/Plugins/,$(CUSTOM_NSIS_PLUGINS)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(MOZILLA_DIR)/other-licenses/nsis/,$(CUSTOM_UI)) $(CONFIG_DIR)
	$(call py_action,preprocessor,-Fsubstitution $(DEFINES) $(ACDEFINES) \
	  $(srcdir)/$(INSTALLER_DEFINES) -o $(CONFIG_DIR)/defines.nsi)
	cd $(CONFIG_DIR) && $(MAKENSISU) installer.nsi

# ---------------------------------------------------------------------------------------------------------------------

installer:: stage-package
	@rm -rf $(DIST)/installer-stage $(DIST)/xpt
	@echo 'Staging installer files...'
	@$(NSINSTALL) -D $(DIST)/installer-stage/core
	@$(TOOLCHAIN_PREFIX)cp -av $(DIST)/$(PKG_STAGE_DIR)/. $(DIST)/installer-stage/core
	$(RM) -r $(CONFIG_DIR) && mkdir $(CONFIG_DIR)
	$(MAKE) $(CONFIG_DIR)/setup.exe
	$(INSTALL) $(CONFIG_DIR)/setup.exe $(DIST)/installer-stage
	@echo 'Compressing installer files...'
	cd $(DIST)/installer-stage && $(CYGWIN_WRAPPER) $(PKG_ARCHIVER_CMD) $(abspath $(CONFIG_DIR))/app.7z
	$(MAKE) $(CONFIG_DIR)/7zSD.sfx
	cat $(CONFIG_DIR)/7zSD.sfx $(CONFIG_DIR)/app.tag $(CONFIG_DIR)/app.7z > "$(DIST)/$(PKG_INSTALLER_FILENAME)"

# ---------------------------------------------------------------------------------------------------------------------

uninstaller::
	$(RM) -r $(CONFIG_DIR) && mkdir $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(srcdir)/,$(INSTALLER_FILES)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(DIST)/branding/,$(BRANDING_FILES)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(MOZINST_PATH)/windows/nsis/,$(TOOLKIT_NSIS_FILES)) $(CONFIG_DIR)
	$(INSTALL) $(addprefix $(MOZILLA_DIR)/other-licenses/nsis/Plugins/,$(CUSTOM_NSIS_PLUGINS)) $(CONFIG_DIR)
	$(call py_action,preprocessor,-Fsubstitution $(DEFINES) $(ACDEFINES) \
	  $(srcdir)/src/defines.nsi.in -o $(CONFIG_DIR)/defines.nsi)
	$(PYTHON) $(MOZINST_PATH)/windows/nsis/preprocess-locale.py --preprocess-locale \
		$(MOZILLA_SRCDIR) $(abspath $(srcdir))/locale $(AB_CD) $(CONFIG_DIR)
	$(NSINSTALL) -D $(DIST)/bin/uninstall
	cd $(CONFIG_DIR) && $(MAKENSISU) uninstaller.nsi
	cp $(CONFIG_DIR)/helper.exe $(DIST)/bin/uninstall

# =====================================================================================================================
