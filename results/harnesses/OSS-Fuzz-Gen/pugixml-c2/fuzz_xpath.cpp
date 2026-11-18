#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "../src/pugixml.hpp"

static std::string make_xpath_name(FuzzedDataProvider& stream, size_t max_len)
{
	static const char start_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
	static const char symbol_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.";

	if (max_len == 0)
		max_len = 1;

	size_t len = stream.ConsumeIntegralInRange<size_t>(1, max_len);

	std::string name;
	name += start_chars[stream.ConsumeIntegralInRange<uint8_t>(0, sizeof(start_chars) - 2)];

	for (size_t i = 1; i < len && stream.remaining_bytes() > 0; ++i)
		name += symbol_chars[stream.ConsumeIntegralInRange<uint8_t>(0, sizeof(symbol_chars) - 2)];

	return name;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	FuzzedDataProvider stream(data, size);

	size_t avail = stream.remaining_bytes();
	size_t doc_cap = avail > 64 ? (avail / 4) : 0;
	if (doc_cap > 512)
		doc_cap = 512;
	size_t doc_len = doc_cap ? stream.ConsumeIntegralInRange<size_t>(0, doc_cap) : 0;
	std::string doc_xml = stream.ConsumeBytesAsString(doc_len);

	pugi::xml_document doc;

	doc.load_buffer(doc_xml.data(), doc_xml.size(), pugi::parse_full);

	bool use_variables = stream.ConsumeBool();

	pugi::xpath_variable_set variables;
	pugi::xpath_variable_set* variables_ptr = PUGIXML_NULL;
	std::vector<std::string> var_names;

	std::string string_var_name;
	std::string number_var_name;

	if (use_variables)
	{

		uint8_t variable_count = stream.ConsumeIntegralInRange<uint8_t>(0, 4);

		for (uint8_t i = 0; i < variable_count; ++i)
		{
			if (stream.remaining_bytes() == 0)
				break;

			size_t max_len = stream.ConsumeBool() ? 40 : 16;
			std::string name = make_xpath_name(stream, max_len);
			if (name.empty())
				continue;

			uint8_t type_selector = stream.ConsumeIntegralInRange<uint8_t>(0, 3);

			bool added = false;

			switch (type_selector)
			{
			case 0:
			{
				pugi::xpath_variable* var = variables.add(name.c_str(), pugi::xpath_type_boolean);
				if (var)
				{
					bool value = stream.ConsumeBool();
					variables.set(name.c_str(), value);
					added = true;
				}
				break;
			}
			case 1:
			{
				pugi::xpath_variable* var = variables.add(name.c_str(), pugi::xpath_type_number);
				if (var)
				{
					double value = stream.ConsumeFloatingPoint<double>();
					variables.set(name.c_str(), value);
					added = true;
					if (number_var_name.empty())
						number_var_name = name;
				}
				break;
			}
			case 2:
			{
				pugi::xpath_variable* var = variables.add(name.c_str(), pugi::xpath_type_string);
				if (var)
				{
					std::string value = stream.ConsumeRandomLengthString(32);
					variables.set(name.c_str(), value.c_str());
					added = true;
					if (string_var_name.empty())
						string_var_name = name;
				}
				break;
			}
			default:
			{

				pugi::xpath_variable* var = variables.add(name.c_str(), pugi::xpath_type_node_set);
				added = (var != PUGIXML_NULL);
				break;
			}
			}

			if (added)
				var_names.push_back(name);
		}

		if (stream.remaining_bytes() > 0 && stream.ConsumeBool())
		{
			std::string qname = make_xpath_name(stream, 8) + ":" + make_xpath_name(stream, 8);
			pugi::xpath_variable* var = variables.add(qname.c_str(), pugi::xpath_type_number);
			if (var)
			{
				double value = stream.ConsumeFloatingPoint<double>();
				variables.set(qname.c_str(), value);
				var_names.push_back(qname);
			}
		}

		variables_ptr = &variables;
	}

	std::string query = stream.ConsumeRemainingBytesAsString();

	if (!var_names.empty())
	{
		std::string augmented;
		for (size_t i = 0; i < var_names.size(); ++i)
		{
			switch (i % 3)
			{
			case 0:

				augmented += "$" + var_names[i] + "+";
				break;
			case 1:

				augmented += "//*[$" + var_names[i] + "]";
				break;
			default:

				augmented += "@a=$" + var_names[i] + " or ";
				break;
			}
		}
		query = augmented + query;
	}

	if (stream.remaining_bytes() > 0 ? stream.ConsumeBool() : (query.size() % 2 == 0))
	{
		size_t digit_count = 32 + (query.size() % 33);
		std::string long_number(digit_count, '1');
		query = long_number + "+" + query;
	}

	{
		std::vector<std::string> snippets;
		snippets.push_back("@__fuzzattr=\"literal\" or ");
		snippets.push_back("translate(\"abcXY\",\"ab\",\"XY\")+");
		snippets.push_back("//*[position()=1] or ");
		snippets.push_back("//*[last()] or ");
		snippets.push_back("@child::text() or ");
		snippets.push_back("bogusaxisname::node() or ");
		snippets.push_back("processing-instruction(zz) or ");
		snippets.push_back("namespace::node() or ");

		if (!string_var_name.empty())
			snippets.push_back("@__fuzzattr=$" + string_var_name + " or ");

		if (!number_var_name.empty())
			snippets.push_back("//*[$" + number_var_name + "] or ");

		if (use_variables)
			snippets.push_back("$__definitely_not_declared_xyz+");

		if (stream.ConsumeBool())
		{
			size_t idx = stream.ConsumeIntegralInRange<size_t>(0, snippets.size() - 1);
			query = snippets[idx] + query;
		}
	}

	if (stream.remaining_bytes() > 0 ? stream.ConsumeBool() : (query.size() % 3 == 0))
	{
		size_t depth = 1025 + (query.size() % 76);
		query = std::string(depth, '(') + "(" + query + ")" + std::string(depth, ')');
	}

#if !defined(PUGIXML_NO_EXCEPTIONS)
	try
	{
		pugi::xpath_query q(query.c_str(), variables_ptr);

		(void)q.evaluate_boolean(doc);
		(void)q.evaluate_number(doc);
		(void)q.evaluate_string(doc);

		try
		{
			pugi::xpath_node_set ns = q.evaluate_node_set(doc);
			(void)ns.size();
			if (!ns.empty())
				(void)q.evaluate_node(doc);
		}
		catch (const pugi::xpath_exception&)
		{

		}
	}
	catch (const pugi::xpath_exception&)
	{

	}
	catch (const std::bad_alloc&)
	{

	}
#else
	pugi::xpath_query q(query.c_str(), variables_ptr);
	(void)q.evaluate_boolean(doc);
	(void)q.evaluate_number(doc);
	(void)q.evaluate_string(doc);
#endif

	return 0;
}