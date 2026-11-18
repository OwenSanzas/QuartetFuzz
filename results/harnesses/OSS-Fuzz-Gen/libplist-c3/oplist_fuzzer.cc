#include <plist/plist.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{

	if (data == NULL || size == 0) {
		return 0;
	}

	auto run = [](const std::string &s) {
		plist_t root = NULL;

		plist_from_openstep(s.data(), static_cast<uint32_t>(s.size()), &root);
		if (root) {
			plist_free(root);
			root = NULL;
		}
	};

	const std::string raw(reinterpret_cast<const char*>(data), size);

	run(raw);

	{
		std::string wrapped;
		wrapped.reserve(size + 2);
		wrapped.push_back('{');
		wrapped.append(raw);
		wrapped.push_back('}');
		run(wrapped);
	}

	{
		std::string wrapped;
		wrapped.reserve(size + 2);
		wrapped.push_back('(');
		wrapped.append(raw);
		wrapped.push_back(')');
		run(wrapped);
	}

	{
		size_t half = size / 2;
		std::string key(reinterpret_cast<const char*>(data), half ? half : size);
		std::string val(reinterpret_cast<const char*>(data) + half, size - half);

		std::string strings_doc;
		strings_doc.reserve(key.size() * 2 + val.size() * 2 + 64);
		strings_doc += "\"";
		strings_doc += key;
		strings_doc += "\" = \"";
		strings_doc += val;
		strings_doc += "\";\n";

		strings_doc += "\"";
		strings_doc += key;
		strings_doc += "\" = \"";
		strings_doc += val;
		strings_doc += "dup\";\n";
		run(strings_doc);
	}

	{
		std::string data_block;
		data_block.reserve(size + 2);
		data_block.push_back('<');
		static const char hexish[] = "0123456789abcdefg";
		for (size_t i = 0; i < size; i++) {
			data_block.push_back(hexish[data[i] % 17]);
		}
		data_block.push_back('>');
		run(data_block);
	}

	{
		size_t depth = 520 + (size % 50);
		std::string deep;
		deep.reserve(depth + size);
		deep.append(depth, '{');
		deep.append(raw);
		run(deep);
	}

	run("{{}=0;}");
	run("{()=0;}");
	run("{<00>=0;}");

	run("{\"k\"}");
	run("{\"k\"=");
	run("{\"k\"=0");
	run("{\"k\" = 0 ");

	run("\"abc'");
	run("'abc\"");

	run("<1");
	run("<12");
	run("<1g>");

	run("{\"a\"=0;");
	run("(0,1");
	run("<0a");
	run(std::string(600, '('));

	run("{}=");
	run("()=");
	run("<00>=");
	run("0=");

	run("   \t\r\n");
	run("// just a comment\n");
	run("/* just a comment */");

	{
		std::string many;
		for (int i = 0; i < 64; i++) {
			many += "\"k" + std::to_string(i) + "\" = \"" + raw + "\";\n";
		}
		run(many);
	}

	return 0;
}