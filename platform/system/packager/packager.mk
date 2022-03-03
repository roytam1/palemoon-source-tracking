# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# == | Makefile Targets | =============================================================================================

libs:: stage-package

ifeq ($(OS_ARCH),WINNT)
ifndef MAKENSISU
installer::
	$(error "make installer" requires NSIS)
endif # MAKENSISU
else
installer::
	$(error "make installer" only supports Windows at this time)
endif

# Handle the package manifest(s) if it is used
ifdef MOZ_PKG_MANIFEST_P
MOZ_PKG_MANIFEST = package-manifest

$(MOZ_PKG_MANIFEST): $(MOZ_PKG_MANIFEST_P) $(GLOBAL_DEPS)
	$(call py_action,preprocessor,$(DEFINES) $(ACDEFINES) $< -o $@)

GARBAGE += $(MOZ_PKG_MANIFEST)
endif

# ---------------------------------------------------------------------------------------------------------------------

# Create the mozbuildinfo json file
.PHONY: json-metadata
json-metadata:
	@$(RM) -f $(DIST)/$(PKG_JSON_FILENAME)
	$(PYTHON) $(MOZINST_PATH)/informulate.py \
		$(DIST)/$(PKG_JSON_FILENAME) \
		BUILDID=$(BUILDID) \
		MOZ_PKG_PLATFORM=$(PKG_BUILD_TARGET)

# ---------------------------------------------------------------------------------------------------------------------

# Stage and strip the package
stage-package: $(MOZ_PKG_MANIFEST) json-metadata
	@echo 'Staging application files...'
	OMNIJAR_NAME=$(OMNIJAR_NAME) \
	NO_PKG_FILES="$(NO_PKG_FILES)" \
	$(PYTHON) $(MOZINST_PATH)/packager.py $(DEFINES) $(ACDEFINES) \
		--format $(MOZ_PACKAGER_FORMAT) \
		$(if $(MOZ_PKG_REMOVALS),$(addprefix --removals ,$(MOZ_PKG_REMOVALS))) \
		$(if $(filter-out 0,$(MOZ_PKG_FATAL_WARNINGS)),,--ignore-errors) \
		$(if $(OPTIMIZEJARS),--optimizejars) \
		$(addprefix --compress ,$(JAR_COMPRESSION)) \
		$(MOZ_PKG_MANIFEST) '$(DIST)' '$(DIST)'/$(PKG_STAGE_DIR)$(if $(MOZ_PKG_MANIFEST),,$(_BINPATH)) \
		$(if $(filter omni,$(MOZ_PACKAGER_FORMAT)),$(if $(NON_OMNIJAR_FILES),--non-resource $(NON_OMNIJAR_FILES)))
	$(PYTHON) $(MOZINST_PATH)/find-dupes.py $(DIST)/$(PKG_STAGE_DIR)
	@(cd $(DIST)/$(PKG_STAGE_DIR) && $(CREATE_PRECOMPLETE_CMD))

# ---------------------------------------------------------------------------------------------------------------------

# Install the application to the local system
install::
ifeq ($(OS_ARCH),WINNT)
ifeq (,$(wildcard $(DIST)/$(PKG_INSTALLER_FILENAME)))
	$(error You need to run "make installer" first)
endif
	@echo 'Starting the application installer...'
	start $(DIST)/$(PKG_INSTALLER_FILENAME)
	@echo 'Please complete installation wizard.'
else
	# XXXTobin: We should not be using MOZ_APP_NAME and MOZ_APP_VERSION with this but the package forms.
	# However, that could cause issues and requires further testing. For now just do it more or less the same
	# as mozinstaller.
	$(MAKE) stage-package
	@echo 'Installing application files to $(DESTDIR)$(installdir)...'
	$(NSINSTALL) -D $(DESTDIR)$(installdir)
	@$(TOOLCHAIN_PREFIX)cp -rv $(DIST)/$(PKG_STAGE_DIR)/* $(DESTDIR)$(installdir)
	$(NSINSTALL) -D $(DESTDIR)$(bindir)
	@$(RM) -f $(DESTDIR)$(bindir)/$(MOZ_APP_NAME)
	$(TOOLCHAIN_PREFIX)ln -sv $(installdir)/$(MOZ_APP_NAME) $(DESTDIR)$(bindir)
	@echo 'To run the installed application, execute: ./$(DESTDIR)$(bindir)/$(MOZ_APP_NAME) .'
endif

# ---------------------------------------------------------------------------------------------------------------------

# Package the application as an archive
archive: stage-package
	@echo 'Compressing application archive...'
ifeq ($(OS_ARCH),WINNT)
	cd $(DIST); $(CYGWIN_WRAPPER) $(PKG_ARCHIVER_CMD) $(PKG_ARCHIVE_FILENAME) $(PKG_STAGE_DIR)
else
	cd $(DIST); XZ_OPT=-9e $(PKG_ARCHIVER_CMD) $(PKG_ARCHIVE_FILENAME) $(PKG_STAGE_DIR)
endif

# ---------------------------------------------------------------------------------------------------------------------

update: $(call mkdir_deps,$(ABS_DIST)/$(PKG_STAGE_DIR))
	@echo 'Creating update mar (xz compressed)...'
	MAR=$(DIST)/host/bin/mar$(HOST_BIN_SUFFIX) \
	  $(MOZILLA_DIR)/system/packager/make-update.sh \
	  '$(DIST)/$(PKG_UPDATE_FILENAME)' '$(DIST)/$(PKG_STAGE_DIR)'

# ---------------------------------------------------------------------------------------------------------------------

update-bz2: $(call mkdir_deps,$(ABS_DIST)/$(PKG_STAGE_DIR))
	@echo 'Creating update mar (bz2 compressed)...'
	MAR_OLD_FORMAT=1 MAR=$(DIST)/host/bin/mar$(HOST_BIN_SUFFIX) \
	  $(MOZILLA_DIR)/system/packager/make-update.sh \
	  '$(DIST)/$(PKG_BASENAME).complete.mar' '$(DIST)/$(PKG_STAGE_DIR)'

# ---------------------------------------------------------------------------------------------------------------------

# Package debugging symbols
.PHONY: symbols
symbols:
ifeq ($(OS_ARCH),WINNT)
	@echo 'Staging debugging symbols...'
	@$(RM) -rf $(DIST)/$(PKG_SYMBOLS_STAGE_DIR) $(DIST)/$(PKG_SYMBOLS_FILENAME)
	cd $(topobjdir); find . -type f -name "*.pdb" \
		-not -path "./dist/*" \
		-not -name "generated.pdb" \
		-not -name "vc*.pdb" \
		-exec install -Dv {} ./dist/$(PKG_SYMBOLS_STAGE_DIR)/{} \;
	@echo 'Compressing debugging symbols...'
	cd $(DIST); $(CYGWIN_WRAPPER) $(PKG_ARCHIVER_CMD) $(PKG_SYMBOLS_FILENAME) $(PKG_SYMBOLS_STAGE_DIR) 
else
	$(error "make symbols" only supports Windows at this time)
endif

# ---------------------------------------------------------------------------------------------------------------------

# For some reason when manually regenerating the chrome files with a fully built dist/bin it fucks up compiling
# the startupcache.
# So we only invoke this phony-target purging and regenerating when the application is NOT fully built.
# Otherwise one should treat the locale and skin targets like any other packaging target, namely, issuing a build
# command to regenerate.
# tl;dr hacks are hack-y.
.PHONY: regenerate-chrome
regenerate-chrome:
	@echo '(Re)generating chrome files...'
	@test -f $(topobjdir)/source-repo.h || $(MAKE) -C $(DEPTH) export
	@$(RM) -rf $(DIST)/bin/chrome.manifest $(DIST)/bin/chrome;
	@$(MAKE) -C $(DEPTH) chrome

# ---------------------------------------------------------------------------------------------------------------------

# Create an en-US language pack without needing a whole custom build system codepath
.PHONY: locale
locale:
	@$(RM) -rf $(DIST)/$(PKG_XPI_L10N_STAGE_DIR) $(DIST)/$(PKG_XPI_L10N_FILENAME)
	@test -f $(DIST)/bin/$(MOZ_APP_NAME)$(BIN_SUFFIX) || $(MAKE) regenerate-chrome
	@echo 'Staging l10n files...'
	@$(NSINSTALL) -D $(DIST)/$(PKG_XPI_L10N_STAGE_DIR)/chrome
	@$(TOOLCHAIN_PREFIX)cp -rv $(DIST)/bin/chrome/en-US.manifest $(DIST)/bin/chrome/en-US $(DIST)/$(PKG_XPI_L10N_STAGE_DIR)/chrome
	@echo manifest chrome/en-US.manifest > $(DIST)/$(PKG_XPI_L10N_STAGE_DIR)/chrome.manifest
	$(call py_action,preprocessor,-Fsubstitution $(DEFINES) $(ACDEFINES) \
		$(GREPKGR_PATH)/locale-install.rdf.in -o $(DIST)/$(PKG_XPI_L10N_STAGE_DIR)/install.rdf)
	@echo 'Compressing l10n files...'
	cd $(DIST)/$(PKG_XPI_L10N_STAGE_DIR); $(ZIP) -Dr9X ../../$(PKG_XPI_L10N_FILENAME) *

# ---------------------------------------------------------------------------------------------------------------------

# Create an os-dependent skin without needing a whole custom build system codepath
.PHONY: skin
skin:
	@$(RM) -rf $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR) $(DIST)/$(PKG_XPI_SKIN_FILENAME)
	@test -f $(DIST)/bin/$(MOZ_APP_NAME)$(BIN_SUFFIX) || $(MAKE) regenerate-chrome
	@echo 'Staging theme files...'
	@$(NSINSTALL) -D $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR)/chrome
	@$(TOOLCHAIN_PREFIX)cp -rv $(DIST)/bin/chrome/classic.manifest $(DIST)/bin/chrome/classic $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR)/chrome
	@$(TOOLCHAIN_PREFIX)cp -rv $(DIST)/bin/extensions/{972ce4c6-7e08-4474-a285-3208198ce6fd}/chrome.manifest $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR)
	@echo manifest chrome/classic.manifest >> $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR)/chrome.manifest
	$(call py_action,preprocessor,-Fsubstitution $(DEFINES) $(ACDEFINES) \
		$(GREPKGR_PATH)/skin-install.rdf.in -o $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR)/install.rdf)
	@echo 'Compressing theme files...'
	cd $(DIST)/$(PKG_XPI_SKIN_STAGE_DIR); $(ZIP) -Dr9X ../../$(PKG_XPI_SKIN_FILENAME) *

# =====================================================================================================================
