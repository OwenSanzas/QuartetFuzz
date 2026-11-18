#include <plist/plist.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <functional>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

	{
		plist_t root_node = NULL;
		plist_from_bin(reinterpret_cast<const char*>(data),
		                static_cast<uint32_t>(size), &root_node);
		if (root_node) {
			plist_free(root_node);
		}
	}

	if (size < 4) {
		return 0;
	}

	size_t pos = 0;

	auto next_byte = [&]() -> uint8_t {
		return pos < size ? data[pos++] : 0;
	};
	auto next_u32 = [&]() -> uint32_t {
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) v = (v << 8) | next_byte();
		return v;
	};

	std::function<plist_t(int)> build_node = [&](int depth) -> plist_t {
		uint8_t choice = next_byte() % 11;
		if (depth > 8) choice = choice % 9;

		switch (choice) {
		case 0:
			return plist_new_bool(next_byte() & 1);
		case 1:
			return plist_new_int((int64_t)(int32_t)next_u32());
		case 2:
			return plist_new_uint((uint64_t)next_u32());
		case 3: {
			uint32_t bits = next_u32();
			double d = (double)(int32_t)bits / 3.14159;
			return plist_new_real(d);
		}
		case 4: {
			uint8_t len = next_byte() % 64;
			std::vector<char> buf(len);
			for (uint8_t i = 0; i < len; i++) buf[i] = (char)next_byte();
			return plist_new_data(buf.data(), len);
		}
		case 5: {

			uint8_t len = next_byte() % 32;
			uint8_t mode = next_byte() % 4;
			std::string s;
			for (uint8_t i = 0; i < len; i++) {
				if (mode == 0) {

					s += (char)(0x20 + (next_byte() % 95));
				} else if (mode == 1) {

					s += (char)(0xC2 + (next_byte() % (0xDF - 0xC2 + 1)));
					s += (char)(0x80 + (next_byte() % 0x40));
				} else if (mode == 2) {

					s += (char)0xE0;
					s += (char)(0xA0 + (next_byte() % 0x20));
					s += (char)(0x80 + (next_byte() % 0x40));
				} else {

					uint8_t b = next_byte() % 5;
					uint8_t lead = 0xF0 + b;
					s += (char)lead;
					uint8_t c1 = (lead == 0xF0) ? (0x90 + (next_byte() % 0x10))
					           : (lead == 0xF4) ? (0x80 + (next_byte() % 0x10))
					           : (0x80 + (next_byte() % 0x40));
					s += (char)c1;
					s += (char)(0x80 + (next_byte() % 0x40));
					s += (char)(0x80 + (next_byte() % 0x40));
				}
			}
			return plist_new_string(s.c_str());
		}
		case 6:
			return plist_new_uid((uint64_t)next_u32());
		case 7:
			return plist_new_unix_date((int64_t)(int32_t)next_u32());
		case 8:
			return plist_new_null();
		case 9: {
			plist_t arr = plist_new_array();
			uint8_t n = next_byte() % 20;
			for (uint8_t i = 0; i < n; i++) {
				plist_t child = build_node(depth + 1);
				if (child) plist_array_append_item(arr, child);
			}
			return arr;
		}
		default: {
			plist_t dict = plist_new_dict();
			uint8_t n = next_byte() % 20;
			for (uint8_t i = 0; i < n; i++) {
				char key[16];
				snprintf(key, sizeof(key), "k%u", (unsigned)next_byte());
				plist_t child = build_node(depth + 1);
				if (child) plist_dict_set_item(dict, key, child);
			}
			return dict;
		}
		}
	};

	plist_t tree = build_node(0);
	if (tree) {
		char *bin = NULL;
		uint32_t bin_len = 0;
		if (plist_to_bin(tree, &bin, &bin_len) == PLIST_ERR_SUCCESS && bin) {

			if (pos < size && bin_len > 0) {
				size_t flip = (size_t)next_byte() % bin_len;
				bin[flip] ^= 0xFF;
			}
			plist_t parsed = NULL;
			plist_from_bin(bin, bin_len, &parsed);
			if (parsed) {
				plist_free(parsed);
			}
			free(bin);
		}
		plist_free(tree);
	}

	if (pos + 8 <= size) {
		std::vector<uint8_t> buf;
		buf.insert(buf.end(), {'b','p','l','i','s','t','0','0'});

		uint8_t num_objects = (next_byte() % 20) + 1;
		std::vector<uint32_t> obj_offsets;
		obj_offsets.reserve(num_objects);
		for (uint8_t i = 0; i < num_objects; i++) {
			obj_offsets.push_back((uint32_t)buf.size());
			uint8_t marker_hi = next_byte() & 0xF0;
			uint8_t marker_lo = next_byte() & 0x0F;
			buf.push_back((uint8_t)(marker_hi | marker_lo));
			if ((marker_hi == 0xA0 || marker_hi == 0xC0 || marker_hi == 0xD0) && marker_lo > 0) {

				uint8_t refs = marker_lo;
				if (marker_hi == 0xD0) refs = (uint8_t)(marker_lo * 2);
				for (int k = 0; k < refs; k++) {
					buf.push_back((uint8_t)(next_byte() % (num_objects ? num_objects : 1)));
				}
			} else {

				uint8_t extra = next_byte() % 8;
				for (int k = 0; k < extra; k++) buf.push_back(next_byte());
			}
		}
		uint32_t offset_table_offset = (uint32_t)buf.size();
		uint8_t offset_size = (next_byte() % 8) + 1;
		uint8_t ref_size    = (next_byte() % 8) + 1;
		for (auto off : obj_offsets) {
			uint64_t be = __builtin_bswap64((uint64_t)off);
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf.insert(buf.end(), bep + (8 - offset_size), bep + 8);
		}

		buf.insert(buf.end(), 6, 0);
		buf.push_back(offset_size);
		buf.push_back(ref_size);
		uint64_t n_obj_be = __builtin_bswap64((uint64_t)num_objects);
		const uint8_t *n_obj_bep = reinterpret_cast<const uint8_t*>(&n_obj_be);
		buf.insert(buf.end(), n_obj_bep, n_obj_bep + 8);
		uint64_t root_be = __builtin_bswap64((uint64_t)(next_byte() % num_objects));
		const uint8_t *root_bep = reinterpret_cast<const uint8_t*>(&root_be);
		buf.insert(buf.end(), root_bep, root_bep + 8);
		uint64_t ot_off_be = __builtin_bswap64((uint64_t)offset_table_offset);
		const uint8_t *ot_off_bep = reinterpret_cast<const uint8_t*>(&ot_off_be);
		buf.insert(buf.end(), ot_off_bep, ot_off_bep + 8);

		plist_t craft_root = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf.data()),
		                (uint32_t)buf.size(), &craft_root);
		if (craft_root) plist_free(craft_root);
	}

	if (data[0] % 64 == 0) {

		uint32_t chain_len = 513 + (next_byte() % 4);

		std::vector<uint8_t> buf;
		buf.insert(buf.end(), {'b','p','l','i','s','t','0','0'});

		std::vector<uint32_t> obj_offsets;
		obj_offsets.reserve(chain_len);

		const uint8_t ref_size = 2;
		const uint8_t offset_size = 4;

		for (uint32_t i = 0; i < chain_len - 1; i++) {
			obj_offsets.push_back((uint32_t)buf.size());
			buf.push_back(0xA1);
			uint16_t ref = (uint16_t)(i + 1);
			buf.push_back((uint8_t)(ref >> 8));
			buf.push_back((uint8_t)(ref & 0xFF));
		}

		obj_offsets.push_back((uint32_t)buf.size());
		buf.push_back(0x00);

		uint32_t offset_table_offset = (uint32_t)buf.size();
		for (auto off : obj_offsets) {
			uint32_t be = __builtin_bswap32(off);
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf.insert(buf.end(), bep, bep + offset_size);
		}

		buf.insert(buf.end(), 6, 0);
		buf.push_back(offset_size);
		buf.push_back(ref_size);
		uint64_t n_obj_be = __builtin_bswap64((uint64_t)chain_len);
		const uint8_t *n_obj_bep = reinterpret_cast<const uint8_t*>(&n_obj_be);
		buf.insert(buf.end(), n_obj_bep, n_obj_bep + 8);
		uint64_t root_be = 0;
		const uint8_t *root_bep = reinterpret_cast<const uint8_t*>(&root_be);
		buf.insert(buf.end(), root_bep, root_bep + 8);
		uint64_t ot_off_be = __builtin_bswap64((uint64_t)offset_table_offset);
		const uint8_t *ot_off_bep = reinterpret_cast<const uint8_t*>(&ot_off_be);
		buf.insert(buf.end(), ot_off_bep, ot_off_bep + 8);

		plist_t deep_root = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf.data()),
		                (uint32_t)buf.size(), &deep_root);

		if (deep_root) plist_free(deep_root);

		uint32_t short_chain_len = 505 + (next_byte() % 4);

		std::vector<uint8_t> buf2;
		buf2.insert(buf2.end(), {'b','p','l','i','s','t','0','0'});
		std::vector<uint32_t> obj_offsets2;
		obj_offsets2.reserve(short_chain_len);
		for (uint32_t i = 0; i < short_chain_len - 1; i++) {
			obj_offsets2.push_back((uint32_t)buf2.size());
			buf2.push_back(0xA1);
			uint16_t ref = (uint16_t)(i + 1);
			buf2.push_back((uint8_t)(ref >> 8));
			buf2.push_back((uint8_t)(ref & 0xFF));
		}
		obj_offsets2.push_back((uint32_t)buf2.size());
		buf2.push_back(0x00);

		uint32_t offset_table_offset2 = (uint32_t)buf2.size();
		for (auto off : obj_offsets2) {
			uint32_t be = __builtin_bswap32(off);
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf2.insert(buf2.end(), bep, bep + offset_size);
		}
		buf2.insert(buf2.end(), 6, 0);
		buf2.push_back(offset_size);
		buf2.push_back(ref_size);
		uint64_t n_obj_be2 = __builtin_bswap64((uint64_t)short_chain_len);
		const uint8_t *n_obj_bep2 = reinterpret_cast<const uint8_t*>(&n_obj_be2);
		buf2.insert(buf2.end(), n_obj_bep2, n_obj_bep2 + 8);
		uint64_t root_be2 = 0;
		const uint8_t *root_bep2 = reinterpret_cast<const uint8_t*>(&root_be2);
		buf2.insert(buf2.end(), root_bep2, root_bep2 + 8);
		uint64_t ot_off_be2 = __builtin_bswap64((uint64_t)offset_table_offset2);
		const uint8_t *ot_off_bep2 = reinterpret_cast<const uint8_t*>(&ot_off_be2);
		buf2.insert(buf2.end(), ot_off_bep2, ot_off_bep2 + 8);

		plist_t shallow_root = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf2.data()),
		                (uint32_t)buf2.size(), &shallow_root);
		if (shallow_root) plist_free(shallow_root);
	}

	if (pos + 16 <= size) {
		std::vector<uint8_t> buf;
		buf.insert(buf.end(), {'b','p','l','i','s','t','0','0'});
		buf.push_back(0x00);

		uint8_t offset_size = next_byte();
		uint8_t ref_size    = next_byte();
		uint64_t num_objects = (uint64_t)next_u32();
		uint64_t root_object_index = (uint64_t)next_u32();
		uint64_t offset_table_offset = (uint64_t)next_u32();

		buf.insert(buf.end(), 6, 0);
		buf.push_back(offset_size);
		buf.push_back(ref_size);
		uint64_t be;
		be = __builtin_bswap64(num_objects);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf.insert(buf.end(), bep, bep + 8);
		}
		be = __builtin_bswap64(root_object_index);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf.insert(buf.end(), bep, bep + 8);
		}
		be = __builtin_bswap64(offset_table_offset);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf.insert(buf.end(), bep, bep + 8);
		}

		plist_t r = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf.data()),
		                (uint32_t)buf.size(), &r);
		if (r) plist_free(r);
	}

	if (pos + 16 <= size) {
		static const uint8_t fill_types[] = {0x40,0x50,0x60,0xA0,0xC0,0xD0};
		uint8_t type = fill_types[next_byte() % 6];

		std::vector<uint8_t> buf;
		buf.insert(buf.end(), {'b','p','l','i','s','t','0','0'});
		buf.push_back((uint8_t)(type | 0x0F));
		buf.push_back(0x13);
		uint64_t big_size = (((uint64_t)next_u32()) << 32) | (uint64_t)next_u32();
		uint64_t be = __builtin_bswap64(big_size);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be);
			buf.insert(buf.end(), bep, bep + 8);
		}

		uint32_t offset_table_offset = (uint32_t)buf.size();
		buf.push_back(8);

		buf.insert(buf.end(), 6, 0);
		buf.push_back(1); buf.push_back(1);
		uint64_t n1 = __builtin_bswap64((uint64_t)1);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&n1);
			buf.insert(buf.end(), bep, bep + 8);
		}
		uint64_t r0 = 0;
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&r0);
			buf.insert(buf.end(), bep, bep + 8);
		}
		uint64_t otb = __builtin_bswap64((uint64_t)offset_table_offset);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&otb);
			buf.insert(buf.end(), bep, bep + 8);
		}

		plist_t r = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf.data()),
		                (uint32_t)buf.size(), &r);
		if (r) plist_free(r);
	}

	if (pos + 4 <= size) {
		std::vector<uint8_t> buf;
		buf.insert(buf.end(), {'b','p','l','i','s','t','0','0'});
		buf.push_back(0x00);

		uint32_t offset_table_offset = (uint32_t)buf.size();
		uint8_t choice = next_byte() % 3;
		uint32_t bad_offset =
		    (choice == 0) ? 0u :
		    (choice == 1) ? 0xFFFFFFFFu : offset_table_offset;
		uint32_t be32 = __builtin_bswap32(bad_offset);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&be32);
			buf.insert(buf.end(), bep, bep + 4);
		}

		buf.insert(buf.end(), 6, 0);
		buf.push_back(4); buf.push_back(1);
		uint64_t n1 = __builtin_bswap64((uint64_t)1);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&n1);
			buf.insert(buf.end(), bep, bep + 8);
		}
		uint64_t r0 = 0;
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&r0);
			buf.insert(buf.end(), bep, bep + 8);
		}
		uint64_t otb = __builtin_bswap64((uint64_t)offset_table_offset);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&otb);
			buf.insert(buf.end(), bep, bep + 8);
		}

		plist_t r = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf.data()),
		                (uint32_t)buf.size(), &r);
		if (r) plist_free(r);
	}

	if (pos + 2 <= size) {
		static const uint16_t patterns[][2] = {
			{0xD800, 0xDC00},
			{0xD800, 0x0041},
			{0xDC00, 0x0041},
			{0xD800, 0xD800},
		};
		uint8_t which = next_byte() % 4;

		std::vector<uint8_t> buf;
		buf.insert(buf.end(), {'b','p','l','i','s','t','0','0'});
		buf.push_back(0x62);
		for (int k = 0; k < 2; k++) {
			buf.push_back((uint8_t)(patterns[which][k] >> 8));
			buf.push_back((uint8_t)(patterns[which][k] & 0xFF));
		}
		uint32_t offset_table_offset = (uint32_t)buf.size();
		buf.push_back(8);

		buf.insert(buf.end(), 6, 0);
		buf.push_back(1); buf.push_back(1);
		uint64_t n1 = __builtin_bswap64((uint64_t)1);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&n1);
			buf.insert(buf.end(), bep, bep + 8);
		}
		uint64_t r0 = 0;
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&r0);
			buf.insert(buf.end(), bep, bep + 8);
		}
		uint64_t otb = __builtin_bswap64((uint64_t)offset_table_offset);
		{
			const uint8_t *bep = reinterpret_cast<const uint8_t*>(&otb);
			buf.insert(buf.end(), bep, bep + 8);
		}

		plist_t r = NULL;
		plist_from_bin(reinterpret_cast<const char*>(buf.data()),
		                (uint32_t)buf.size(), &r);
		if (r) plist_free(r);
	}

	return 0;
}