/*
 * xplist_fuzzer.cc
 * XML plist fuzz target for libFuzzer
 *
 * Copyright (c) 2017 Nikias Bassen All Rights Reserved.
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 0x0FFFFFFF) {
		return 0;
	}
	plist_t root_node = NULL;
	plist_err_t err = plist_from_xml(reinterpret_cast<const char*>(data), static_cast<uint32_t>(size), &root_node);
	if (err == PLIST_ERR_SUCCESS && root_node) {
		char *xml_out = NULL;
		uint32_t xml_length = 0;
		plist_to_xml(root_node, &xml_out, &xml_length);
		if (xml_out) {
			plist_mem_free(xml_out);
		}
	}
	plist_free(root_node);

	return 0;
}
