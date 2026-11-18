# Copyright 2019 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

FROM gcr.io/oss-fuzz-base/base-builder
RUN apt-get update && apt-get install -y make autoconf automake libtool libtool-bin pkg-config gettext sudo default-jdk
RUN git clone --depth 1 https://github.com/xiph/flac.git
RUN git clone --depth 1 https://github.com/xiph/ogg.git
RUN git clone --depth 1 https://github.com/google/ExoPlayer.git
COPY fuzzer_exo.cpp $SRC/
COPY build.sh $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/flac/oss-fuzz/encoder.cc
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh encoder.cc 1da911a977f42c27bdb41def59cd2333297af3a2 && rm -f /tmp/qf_scrub_git.sh
