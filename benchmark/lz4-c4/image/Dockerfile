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

RUN git clone --depth 1 https://github.com/lz4/lz4.git /src/lz4

WORKDIR $SRC/lz4
COPY build.sh run_tests.sh $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/lz4/ossfuzz/round_trip_hc_fuzzer.c
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh round_trip_hc_fuzzer.c 7d03ee2d87dee6cf7887c8d858f7260adb43f898 && rm -f /tmp/qf_scrub_git.sh
