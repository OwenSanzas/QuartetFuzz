#include <plist/plist.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static void try_json(const std::string& s)
{
	plist_t node = NULL;
	plist_from_json(s.c_str(), static_cast<uint32_t>(s.size()), &node);
	if (node) {

		char *out = NULL;
		uint32_t outlen = 0;
		if (plist_to_json(node, &out, &outlen, 1) == PLIST_ERR_SUCCESS && out) {
			plist_t rp = NULL;
			plist_from_json(out, outlen, &rp);
			plist_free(rp);
			free(out);
		}

		char *out2 = NULL;
		uint32_t outlen2 = 0;
		if (plist_to_json_with_options(node, &out2, &outlen2,
		        static_cast<plist_write_options_t>(PLIST_OPT_COMPACT | PLIST_OPT_COERCE)) == PLIST_ERR_SUCCESS && out2) {
			plist_t rp2 = NULL;
			plist_from_json(out2, outlen2, &rp2);
			plist_free(rp2);
			free(out2);
		}

		char *out3 = NULL;
		uint32_t outlen3 = 0;
		if (plist_to_json_with_options(node, &out3, &outlen3,
		        static_cast<plist_write_options_t>(PLIST_OPT_COMPACT)) == PLIST_ERR_SUCCESS && out3) {
			plist_t rp3 = NULL;
			plist_from_json(out3, outlen3, &rp3);
			plist_free(rp3);
			free(out3);
		}
	}
	plist_free(node);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{

	{
		plist_t tmp = NULL;

		plist_from_json(NULL, 0, &tmp);
		plist_free(tmp);

		tmp = NULL;
		plist_from_json(reinterpret_cast<const char*>(data), 0, &tmp);
		plist_free(tmp);

		plist_from_json(reinterpret_cast<const char*>(data), static_cast<uint32_t>(size), NULL);
	}

	{
		const int depth = 520;
		std::string deep;
		deep.reserve(static_cast<size_t>(depth) * 2);
		for (int i = 0; i < depth; i++) deep += '[';
		for (int i = 0; i < depth; i++) deep += ']';
		try_json(deep);

		std::string deep_obj;
		for (int i = 0; i < depth; i++) deep_obj += "{\"a\":";
		deep_obj += "1";
		for (int i = 0; i < depth; i++) deep_obj += "}";
		try_json(deep_obj);
	}

	{
		std::string dup = "{\"k\":1,\"k\":2,\"k\":";

		dup += "\"";
		for (size_t i = 0; i < size && i < 32; i++) {
			char c = static_cast<char>(data[i]);
			if (c == '"' || c == '\\' || (unsigned char)c < 0x20) continue;
			dup += c;
		}
		dup += "\"}";
		try_json(dup);
	}

	try_json("{\"k\":1,\"k\":[1,2,3]}");
	try_json("{\"k\":{\"x\":1},\"k\":{\"y\":2}}");
	try_json("{\"k\":[1,2],\"k\":{\"a\":[3,4,5]}}");

	{
		std::string bignum(25, '9');
		try_json("[" + bignum + "]");
		try_json("[-" + bignum + "]");
	}

	try_json("[1e400]");
	try_json("[-1e400]");

	try_json("[3.14]");
	try_json("[-2.5]");
	try_json("[1.5e10]");
	try_json("[1.5e-10]");
	try_json("[0.0]");
	try_json("[-0.0]");
	try_json("{\"f\":3.14159,\"g\":-1.0e-5}");

	try_json("[\"\\u12\"]");
	try_json("[\"\\uZZZZ\"]");
	try_json("[\"\\q\"]");

	try_json("[\"\\u0041\"]");
	try_json("[\"\\u00e9\"]");
	try_json("[\"\\u20ac\"]");
	try_json("[\"\\ud800\"]");
	try_json("[\"\\udc00\"]");
	try_json("[\"a\\u0041b\\u00e9c\\u20acd\"]");

	try_json("{true:1}");
	try_json("{1:2}");

	try_json("{\"a\":1,\"b\"");
	try_json("[1,2,3");
	try_json("{\"a\":[1,2}");
	try_json("   ");
	try_json("{");
	try_json("\"unterminated");
	try_json("nul");
	try_json("tru");
	try_json("{{{{");

	{
		plist_t num_node = NULL;
		plist_from_json("42", 2, &num_node);
		if (num_node) {
			char *o = NULL;
			uint32_t ol = 0;
			plist_to_json(num_node, &o, &ol, 1);
			if (o) free(o);
			char *o2 = NULL;
			uint32_t ol2 = 0;
			plist_to_json_with_options(num_node, &o2, &ol2,
			        static_cast<plist_write_options_t>(PLIST_OPT_COERCE));
			if (o2) free(o2);
		}
		plist_free(num_node);

		plist_t str_node = NULL;
		plist_from_json("\"hello\"", 7, &str_node);
		if (str_node) {
			char *o = NULL;
			uint32_t ol = 0;
			plist_to_json(str_node, &o, &ol, 0);
			if (o) free(o);
		}
		plist_free(str_node);
	}

	plist_t root_node = NULL;
	plist_err_t err = plist_from_json(reinterpret_cast<const char*>(data),
	                                   static_cast<uint32_t>(size), &root_node);

	if (err == PLIST_ERR_SUCCESS && root_node) {

		char *json_out = NULL;
		uint32_t json_out_len = 0;
		if (plist_to_json(root_node, &json_out, &json_out_len, 1) == PLIST_ERR_SUCCESS && json_out) {
			plist_t reparsed = NULL;
			plist_from_json(json_out, json_out_len, &reparsed);
			plist_free(reparsed);
			free(json_out);
		}

		char *json_out2 = NULL;
		uint32_t json_out2_len = 0;
		if (plist_to_json(root_node, &json_out2, &json_out2_len, 0) == PLIST_ERR_SUCCESS && json_out2) {
			plist_t reparsed2 = NULL;
			plist_from_json(json_out2, json_out2_len, &reparsed2);
			plist_free(reparsed2);
			free(json_out2);
		}

		plist_t wrapper = plist_new_dict();
		if (wrapper) {
			plist_t copy = plist_copy(root_node);
			if (copy) {
				plist_dict_set_item(wrapper, "parsed", copy);
			}

			if (size >= 8) {
				plist_t data_node = plist_new_data(
				    reinterpret_cast<const char*>(data),
				    static_cast<uint64_t>(size));
				if (data_node) {
					plist_dict_set_item(wrapper, "data", data_node);
				}

				uint64_t uid_val = 0;
				memcpy(&uid_val, data, sizeof(uid_val));
				plist_t uid_node = plist_new_uid(uid_val);
				if (uid_node) {
					plist_dict_set_item(wrapper, "uid", uid_node);
				}

				int32_t sec = 0;
				int32_t usec = 0;
				if (size >= 16) {
					memcpy(&sec, data + 8, sizeof(sec));
					memcpy(&usec, data + 12, sizeof(usec));
				} else {
					sec = static_cast<int32_t>(data[0]);
					usec = static_cast<int32_t>(data[1]);
				}
				plist_t date_node = plist_new_date(sec, usec);
				if (date_node) {
					plist_dict_set_item(wrapper, "date", date_node);
				}
			}

			char *cjson = NULL;
			uint32_t cjson_len = 0;
			if (plist_to_json_with_options(wrapper, &cjson, &cjson_len,
			        static_cast<plist_write_options_t>(PLIST_OPT_COERCE)) == PLIST_ERR_SUCCESS && cjson) {
				plist_t reparsed3 = NULL;
				plist_from_json(cjson, cjson_len, &reparsed3);
				plist_free(reparsed3);
				free(cjson);
			}

			char *cjson2 = NULL;
			uint32_t cjson2_len = 0;
			if (plist_to_json_with_options(wrapper, &cjson2, &cjson2_len,
			        static_cast<plist_write_options_t>(PLIST_OPT_COERCE | PLIST_OPT_COMPACT)) == PLIST_ERR_SUCCESS && cjson2) {
				plist_t reparsed4 = NULL;
				plist_from_json(cjson2, cjson2_len, &reparsed4);
				plist_free(reparsed4);
				free(cjson2);
			}

			plist_free(wrapper);
		}
	}

	plist_free(root_node);

	return 0;
}