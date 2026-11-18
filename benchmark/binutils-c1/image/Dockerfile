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
RUN apt-get update && apt-get install -y make texinfo libgmp-dev libmpfr-dev
RUN apt-get update && apt-get install -y flex bison
RUN git clone --depth=1 https://github.com/DavidKorczynski/binary-samples binary-samples
RUN git clone --recursive --depth 1 git://sourceware.org/git/binutils-gdb.git binutils-gdb
WORKDIR $SRC
COPY build.sh $SRC/
COPY fuzz_*.c $SRC/
COPY generate_seeds.py $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/binutils-gdb/fuzz/fuzz_disassemble.c
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh fuzz_disassemble.c 5396494a35b99cea2b32ec5ebdefc75101eb7185 && rm -f /tmp/qf_scrub_git.sh
