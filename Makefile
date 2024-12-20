#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# Basic build for kontain machine (km) and related tests/examples.
#
# Run 'make help' for help.
# Dependencies are enumerated in tests/buildenv-fedora.dockerfile.
# See 'make -C tests buildenv-fedora-local' for installing dependencies
#

TOP := $(shell git rev-parse --show-toplevel)

# scan all these and 'make' stuff there
# do not build dynamic krun for ci
ifneq (${RUN_IN_CI},)
	SUBDIRS := lib km km_cli runtime tests tools/bin include
else
	SUBDIRS := lib km km_cli runtime tests container-runtime tools/bin include
endif

include ${TOP}/make/actions.mk

# build VMM and runtime library before trying to build tests
tests: km runtime lib tools/bin

.PHONY: clang-format clang-format-check
clang-format-check:
	clang-format --dry-run -Werror km/*.h km/*.c tests/*.h tests/*.c tests/*.cpp km_cli/*.c

clang-format:
	clang-format -i km/*.h km/*.c tests/*.h tests/*.c tests/*.cpp km_cli/*.c

withdocker runtime: | ${KM_OPT}/alpine-lib/gcc-libs-path.txt

# for local build
${KM_OPT}/alpine-lib/gcc-libs-path.txt:
	make -C tests .buildenv-local-lib

#for withdocker
${DOCKER_OPT_KONTAIN}/alpine-lib/gcc-libs-path.txt:
	make -C tests .buildenv-local-lib

# On mac $(MAKE) evaluates to '/Applications/Xcode.app/Contents/Developer/usr/bin/make'
ifeq ($(shell uname), Darwin)
MAKE := make
endif

# Package KM and libs+tools+docs for release
KM_RELEASE := ${BLDTOP}/kontain.tar.gz
KM_KKM_RELEASE := ${BLDTOP}/kkm.run
KM_BIN_RELEASE := ${BLDTOP}/kontain_bin.tar.gz
KM_BINARIES := -C ${BLDTOP}/opt/kontain \
					shim/containerd-shim-krun-v2 \
					shim/containerd-shim-krun-label-trigger-v2 \
					bin/km \
					bin/km_cli \
					bin/kkm.run \
					bin/krun \
					bin/krun-label-trigger

# Show this on the release page
RELEASE_MESSAGE ?= Kontain KM Beta Release. branch: ${SRC_BRANCH} sha: ${SRC_SHA}

kkm-pkg: ## Build KKM module self-extracting package.
	@if ! type makeself >& /dev/null ; then echo Please install \"makeself\" first; false; fi
	@# We only need source code, so running 'clean' first
	-$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP}/kkm/kkm clean >& /dev/null
	-$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" -C ${TOP}/kkm/test_kkm clean >& /dev/null
	makeself -q kkm ${BLDTOP}/kkm.run "beta-release" ./installer/build-script.sh

release: ${KM_RELEASE} ${KM_BIN_RELEASE} ## Package kontain.tar.gz file for release
	ls -lh ${KM_RELEASE} ${KM_BIN}

k8s-bundle: ${KM_BIN_RELEASE} ## Build a k8s kontain-bin.tar.gz

${KM_BIN_RELEASE}: ${TOP}/container-runtime/crun/krun.static
	cp -f ${BLDTOP}/kkm.run ${BLDTOP}/opt/kontain/bin/kkm.run
	cp -f ${TOP}/container-runtime/crun/krun.static ${BLDTOP}/opt/kontain/bin/krun
	ln -f ${BLDTOP}/opt/kontain/bin/krun ${BLDTOP}/opt/kontain/bin/krun-label-trigger
	mkdir -p ${BLDTOP}/opt/kontain/shim/
	cp -f ${BLDTOP}/cloud/k8s/shim/containerd-shim-krun-v2 ${BLDTOP}/opt/kontain/shim/containerd-shim-krun-v2
	cp -f ${BLDTOP}/cloud/k8s/shim/containerd-shim-krun-label-trigger-v2 ${BLDTOP}/opt/kontain/shim/containerd-shim-krun-label-trigger-v2
	tar -czvf $@ ${KM_BINARIES}

${KM_RELEASE}: ${TOP}/container-runtime/crun/krun.static ${TOP}/tools/bin/create_release.sh ## Build a release tar.gz file for KM (called from release: target)
	${TOP}/tools/bin/create_release.sh

${TOP}/container-runtime/crun/krun.static:
	make -C container-runtime static

clean-release: ## Clean the release tar files
	rm -f ${KM_RELEASE} ${KM_BIN_RELEASE}

publish-release: ## Publish release with RELEASE_TAG to github
	cd ${TOP}/tools/release; ./gh_release.sh  --token=${GITHUB_RELEASE_TOKEN} --tag=${RELEASE_TAG} --message="${RELEASE_MESSAGE}" ${KM_RELEASE} ${KM_BIN_RELEASE} ${KM_KKM_RELEASE}

EDGE_RELEASE_MESSAGE ?= Kontain KM Edge - date: $(shell date) sha: $(shell git rev-parse HEAD)
REPO_URL := https://${GITHUB_TOKEN}@github.com/kontainapp/km.git
edge-release: ## Trigger edge-release building pipeline
	git config user.email "release@kontain.app"
	git config user.name "Kontain Release Pipeline Bot"
	@echo Delete the ${RELEASE_TAG} tag. Can fail if there is no such tag yet
	-git tag -d ${RELEASE_TAG} && git push --delete ${REPO_URL} ${RELEASE_TAG}
	@echo Now tag the source and push the tag to trigger the pipeline
	git tag -a ${RELEASE_TAG} --message "${EDGE_RELEASE_MESSAGE}"
	git push ${REPO_URL} ${RELEASE_TAG}

install-dev-runtime: ## Configure docker and podman to use build/opt/kontain/krun and km
	make -C container-runtime install-dev-runtime MAKEFLAGS="$(MAKEFLAGS)"

uninstall-dev-runtime: ## Remove docker and podman configuration for local build
	make -C container-runtime uninstall-dev-runtime MAKEFLAGS="$(MAKEFLAGS)"

## Prepares release by
##  -- updating km-releases/current_release.txt  with passed in version number
##  --
##  Requires RELEASE_TAG to be passed in
release-prep:
	@echo ${RELEASE_TAG}
	@echo -n "Confirm the tag for release!!! [y/N] " && read ans && [ $${ans:-N} = y ]
	@echo ${RELEASE_TAG} > km-releases/current_release.txt
	@echo Tagging Release
	git tag -f ${RELEASE_TAG}
ifneq ("${RELEASE_TAG}", "v0.1-test")
	@echo setting current
	git tag -f current ${RELEASE_TAG}
endif
	git push -f origin current ${RELEASE_TAG}

# Install git hooks, if needed
GITHOOK_DIR ?= .githooks
git-hooks-init: ## make git use GITHOOK_DIR (default .githooks) for pre-commit and other hooks
	git config core.hooksPath $(GITHOOK_DIR)
git-hooks-reset: ## reset git hooks location to git default of .git/hooks (not version controlled)
	git config core.hooksPath .git/hooks

compile-commands: ## rebuild compile_commands.json. Assumes 'bear' is installed
	make clean
	bear make -j
	sed -i 's-${CURDIR}-$${workspaceFolder}-' compile_commands.json
