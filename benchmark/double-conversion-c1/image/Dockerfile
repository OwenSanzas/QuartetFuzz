# Copyright 2019 Google LLC
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

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake ninja-build && \
    apt-get clean

RUN git clone --single-branch \
    https://github.com/google/double-conversion.git double-conversion
WORKDIR double-conversion
COPY run_tests.sh build.sh $SRC/
COPY *.cc $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/string_to_double_fuzzer.cc
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh string_to_double_fuzzer.cc 4e0ca516980ccafa9ebf9b85d0d07945705f1a9c && rm -f /tmp/qf_scrub_git.sh
