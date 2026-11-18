# Copyright 2021 Google LLC
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

FROM gcr.io/oss-fuzz-base/base-builder:ubuntu-24-04
RUN apt-get update && apt-get install -y make cmake
RUN git clone https://github.com/mity/md4c

# Dictionaries:
RUN git clone --depth 1 https://github.com/google/fuzzing && \
    mv fuzzing/dictionaries/markdown.dict $SRC/fuzz-mdhtml.dict && \
    rm -rf fuzzing

# Seed corpus:
RUN zip -j $SRC/fuzz-mdhtml_seed_corpus.zip $SRC/md4c/test/fuzzers/seed-corpus/*

WORKDIR $SRC/md4c
COPY run_tests.sh build.sh $SRC/

# --- QuartetFuzz baseline setup (pinned revisions, gold stubbed) ---
COPY qf /qf
RUN bash /qf/pin.sh
RUN bash /qf/stub.sh /src/md4c/test/fuzzers/fuzz-mdhtml.c
RUN apt-get update -q

# --- QuartetFuzz: remove the gold harness from git history ---
COPY qf_scrub_git.sh /tmp/qf_scrub_git.sh
RUN bash /tmp/qf_scrub_git.sh fuzz-mdhtml.c 4e0f60157782bb5322a611c7c8d897d496d7703b && rm -f /tmp/qf_scrub_git.sh
