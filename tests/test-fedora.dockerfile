# Copyright © 2019 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.
#
# Dockerfile for tests.
# Usage will be 'docker run -it --rm --env BRANCH=$(git rev-parse --abbrev-ref HEAD) --device=/dev/kvm --user 0 test-km-fedora km_cdocker run <container> make TARGET=<target> - see ../../Makefile
#
ARG DTYPE=fedora
FROM kontain/buildenv-km-${DTYPE}:latest

ARG branch

ENV BATS time bats/bin/bats
ENV TEST_FILES km_core_tests.bats
ENV TIME_INFO /tests/time_info.txt
ENV KM_BIN /tests/km
ENV BRANCH=${branch}

COPY --chown=appuser:appuser . /tests
WORKDIR /tests
ENV PATH=/tests/bats/bin:.:$PATH

# ENTRYPOINT ["run_bats_tests.sh"]