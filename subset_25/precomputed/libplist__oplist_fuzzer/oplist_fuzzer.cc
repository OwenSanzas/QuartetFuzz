/*
 * oplist_fuzzer.cc
 * OpenStep plist fuzz target for libFuzzer
 *
 * Copyright (c) 2023 Nikias Bassen All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <plist/plist.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0) {
    return 0;
  }

  // plist_from_openstep takes uint32_t length.
  if (size > 0xFFFFFFFF) {
    return 0;
  }

  plist_t root_node = NULL;
  plist_from_openstep(reinterpret_cast<const char *>(data), static_cast<uint32_t>(size), &root_node);

  if (root_node) {
    plist_free(root_node);
  }

  return 0;
}
