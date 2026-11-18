# Copyright 2016 Google Inc.
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
RUN apt-get update && apt-get install -y \
  make \
  autoconf \
  automake \
  libtool \
  pkg-config

RUN git clone --recursive --shallow-submodules --depth 1 https://github.com/nghttp2/nghttp2.git
WORKDIR nghttp2
COPY run_tests.sh build.sh *.options $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/nghttp2/fuzz/fuzz_target_fdp.cc
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh fuzz_target_fdp.cc 5ba240184d52c6014d2e62eaf7e433831aeace34 && rm -f /tmp/qf_scrub_git.sh
