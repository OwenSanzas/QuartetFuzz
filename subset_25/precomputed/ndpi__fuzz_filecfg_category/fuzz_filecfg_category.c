#include "ndpi_api.h"
#include "ndpi_private.h"
#include "fuzz_common_code.h"
#include <string.h>
#include <stdio.h>

/*
 * Copyright (C) 2011-26 - ntop.org
 *
 * This file is part of nDPI, an open source deep packet inspection
 * library based on the OpenDPI and PACE technology by ipoque GmbH
 *
 * nDPI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nDPI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with nDPI.  If not, see <http://www.gnu.org/licenses/>.
 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  struct ndpi_detection_module_struct *ndpi_struct;
  struct ndpi_global_context *g_ctx;
  FILE *fd;
  uint32_t category_val;

  if (size < sizeof(uint32_t)) return 0;

  /* To allow memory allocation failures */
  fuzz_set_alloc_callbacks_and_seed(size);

  memcpy(&category_val, data, sizeof(uint32_t));
  ndpi_protocol_category_t category_id = (ndpi_protocol_category_t)(category_val % NDPI_PROTOCOL_NUM_CATEGORIES);
  
  const uint8_t *file_data = data + sizeof(uint32_t);
  size_t file_size = size - sizeof(uint32_t);

  g_ctx = ndpi_global_init();
  ndpi_struct = ndpi_init_detection_module(g_ctx);

  if(ndpi_struct) {
    ndpi_set_config(ndpi_struct, NULL, "log.level", "3");
    ndpi_set_config(ndpi_struct, "all", "log", "1");

    fd = fmemopen((void *)file_data, file_size, "r");
    if(fd) {
      load_category_file_fd(ndpi_struct, fd, category_id);
      fclose(fd);
    }

    ndpi_finalize_initialization(ndpi_struct);
    ndpi_exit_detection_module(ndpi_struct);
  }

  if(g_ctx) {
    ndpi_global_deinit(g_ctx);
  }

  return 0;
}
