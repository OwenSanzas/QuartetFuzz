# Copyright 2018 Google Inc.
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

FROM gcr.io/oss-fuzz-base/base-builder-python
RUN apt-get update && apt-get install -y make cmake
RUN pip3 install --upgrade setuptools build wheel pip
RUN git clone --depth 1 --branch v5 https://github.com/capstone-engine/capstone.git capstonev5
RUN git clone --depth 1 --branch next https://github.com/capstone-engine/capstone.git capstonenext
WORKDIR $SRC
COPY build.sh $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/capstonenext/suite/fuzz/fuzz_disasm.c
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh fuzz_disasm.c 13aba5db6f567f54ce9aaefce6c371929e2fc71b && rm -f /tmp/qf_scrub_git.sh
