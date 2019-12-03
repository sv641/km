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
# Dockerfile for build python image. There are two stages:
#
# buildenv-cpython - based on kontain/buildenv-km-fedora, git clone, compile and test node
# linkenv - based on km-build-env and just copy the objects and test files

ARG MODE=Release
ARG VERS=v3.7.4

FROM kontain/buildenv-km-fedora AS buildenv-cpython
ARG VERS

RUN git clone https://github.com/python/cpython.git -b $VERS
RUN cd cpython && ./configure
COPY --chown=appuser:appuser Setup.local cpython/Modules/Setup.local
RUN make -C cpython -W Modules/Setup.local -j`expr 2 \* $(nproc)`

FROM kontain/buildenv-km-fedora
ENV PYTHONTOP=/home/appuser/cpython
#
# The following copies two sets of artifacts - objects needed to build (link) python.km,
# and the sets of files to run set of tests. The former is used by link-km.sh,
# the latter is copied to the output by Makefile using NODE_DISTRO_FILES.
#
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Lib/ cpython/Lib
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Modules/ cpython/Modules
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py \
   cpython/build/lib.linux-x86_64-3.7/_sysconfigdata_m_linux_x86_64-linux-gnu.py
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/Programs/python.o cpython/Programs/python.o
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/libpython3.7m.a cpython
COPY --from=buildenv-cpython --chown=appuser:appuser /home/$USER/cpython/pybuilddir.txt cpython