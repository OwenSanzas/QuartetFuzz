/*
 * bplist_fuzzer.cc
 * binary plist fuzz target for libFuzzer
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 0xFFFFFFFF)
	{
		return 0;
	}

	plist_t root_node = NULL;
	plist_from_bin(reinterpret_cast<const char *>(data), (uint32_t)size, &root_node);

	if (root_node)
	{
		char *out_bin = NULL;
		uint32_t out_length = 0;
		plist_to_bin(root_node, &out_bin, &out_length);
		if (out_bin)
		{
			plist_mem_free(out_bin);
		}

		char *out_xml = NULL;
		uint32_t out_xml_length = 0;
		plist_to_xml(root_node, &out_xml, &out_xml_length);
		if (out_xml)
		{
			plist_mem_free(out_xml);
		}

		char *out_json = NULL;
		uint32_t out_json_length = 0;
		plist_to_json(root_node, &out_json, &out_json_length, 0);
		if (out_json)
		{
			plist_mem_free(out_json);
		}

		plist_free(root_node);
	}

	return 0;
}
