#include "../src/pugixml.hpp"

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

class ByteReader {
public:
	ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}

	uint8_t u8() {
		if (pos_ >= size_) return 0;
		return data_[pos_++];
	}

	std::string chunk(size_t max_len) {
		size_t len = u8();
		if (len > max_len) len = max_len;
		std::string out;
		for (size_t i = 0; i < len && pos_ < size_; ++i)
			out.push_back(static_cast<char>(data_[pos_++]));
		return out;
	}

	std::string rest() {
		std::string out;
		if (pos_ < size_)
			out.assign(reinterpret_cast<const char*>(data_ + pos_), size_ - pos_);
		return out;
	}

	bool exhausted() const { return pos_ >= size_; }

private:
	const uint8_t* data_;
	size_t size_;
	size_t pos_;
};

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 4) return 0;

	ByteReader reader(data, size);

	std::string xml_text = reader.chunk(96);

	pugi::xml_document doc;
	doc.load_buffer(xml_text.c_str(), xml_text.size());

	pugi::xpath_variable_set varset;
	std::vector<std::string> var_names;

	uint8_t var_count = reader.u8() & 0x3;
	for (uint8_t i = 0; i < var_count && !reader.exhausted(); ++i)
	{
		std::string name = reader.chunk(8);
		if (name.empty()) continue;
		var_names.push_back(name);

		uint8_t kind = reader.u8() & 0x3;
		switch (kind)
		{
		case 0:
			varset.set(name.c_str(), (reader.u8() & 1) != 0);
			break;
		case 1:
		{
			uint8_t b = reader.u8();
			varset.set(name.c_str(), static_cast<double>(b) - 128.0);
			break;
		}
		case 2:
		{
			std::string val = reader.chunk(16);
			varset.set(name.c_str(), val.c_str());
			break;
		}
		case 3:
		{

			std::vector<pugi::xpath_node> nodes;
			if (doc.first_child())
				nodes.push_back(pugi::xpath_node(doc.first_child()));
			pugi::xpath_node_set ns(nodes.empty() ? NULL : &nodes[0],
			                         nodes.empty() ? NULL : &nodes[0] + nodes.size());
			varset.set(name.c_str(), ns);
			break;
		}
		}
	}

	for (size_t i = 0; i < var_names.size(); ++i)
	{
		pugi::xpath_variable* v = varset.get(var_names[i].c_str());
		if (!v) continue;
		switch (v->type())
		{
		case pugi::xpath_type_boolean: (void)v->get_boolean(); break;
		case pugi::xpath_type_number: (void)v->get_number(); break;
		case pugi::xpath_type_string: (void)v->get_string(); break;
		case pugi::xpath_type_node_set: (void)v->get_node_set(); break;
		default: break;
		}
	}

	std::string query_text = reader.rest();
	if (query_text.empty()) return 0;

	try
	{

		pugi::xpath_query query(query_text.c_str(), &varset);

		if (!query)
		{

			const pugi::xpath_parse_result& r = query.result();
			(void)r.description();
			return 0;
		}

		pugi::xpath_node contexts[2] = {
			pugi::xpath_node(doc.document_element()),
			pugi::xpath_node(doc)
		};

		for (int ci = 0; ci < 2; ++ci)
		{
			const pugi::xpath_node& ctx = contexts[ci];

			bool b = query.evaluate_boolean(ctx);
			(void)b;

			double d = query.evaluate_number(ctx);
			(void)d;

			std::string s = query.evaluate_string(ctx);
			(void)s;

			char buffer[64];
			size_t written = query.evaluate_string(buffer, sizeof(buffer), ctx);
			(void)written;

			pugi::xpath_node_set ns = query.evaluate_node_set(ctx);
			for (size_t i = 0; i < ns.size(); ++i)
			{
				pugi::xpath_node n = ns[i];
				if (n.node())
					(void)n.node().name();
				else if (n.attribute())
					(void)n.attribute().name();
			}

			pugi::xpath_node n = query.evaluate_node(ctx);
			if (n.node())
				(void)n.node().name();
		}

		pugi::xpath_value_type rt = query.return_type();
		(void)rt;
		(void)query.result().description();

		pugi::xpath_node selected = doc.select_node(query);
		(void)selected;

		pugi::xpath_node_set selected_set = doc.select_nodes(query);
		(void)selected_set;
	}
	catch (const pugi::xpath_exception&)
	{

	}
	catch (const std::bad_alloc&)
	{

	}

	try
	{
		pugi::xpath_node n2 = doc.select_node(query_text.c_str(), &varset);
		(void)n2;

		pugi::xpath_node_set ns2 = doc.select_nodes(query_text.c_str(), &varset);
		(void)ns2;
	}
	catch (const pugi::xpath_exception&)
	{
	}
	catch (const std::bad_alloc&)
	{
	}

	return 0;
}
