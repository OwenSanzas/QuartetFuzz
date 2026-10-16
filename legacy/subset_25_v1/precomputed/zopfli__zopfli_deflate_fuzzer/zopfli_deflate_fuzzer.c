#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "src/zopfli/zopfli.h"
#include "src/zopfli/deflate.h"

/*
Copyright 2011 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
*/

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 4) {
    return 0;
  }

  ZopfliOptions options;
  ZopfliInitOptions(&options);

  /* Map first 4 bytes to options */
  int btype = data[0] % 3;
  options.numiterations = (data[1] % 15) + 1;
  options.blocksplitting = (data[2] % 2);
  options.blocksplittingmax = (data[3] % 16);

  const unsigned char* in = (const unsigned char*)(data + 4);
  size_t insize = size - 4;

  unsigned char bp = 0;
  unsigned char* out = NULL;
  size_t outsize = 0;

  ZopfliDeflate(&options, btype, 1 /* final */, in, insize, &bp, &out, &outsize);

  free(out);

  return 0;
}
