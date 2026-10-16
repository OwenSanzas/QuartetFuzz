/**
 * pugixml 1.15 - library for extra-fast XML processing
 * --------------------------------------------------------
 * Copyright (C) 2006-2026, by Arseny Kapoulkine (arseny.kapoulkine@gmail.com)
 * Report bugs and download new versions at https://pugixml.org/
 *
 * This library is distributed under the MIT License. See LICENSE.md for details.
 */

#include "../src/pugixml.hpp"
#include "fuzzer/FuzzedDataProvider.h"

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

// Define a reasonable limit for query and XML size to avoid OOM/timeouts
#define MAX_INPUT_SIZE 1024

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
	if (Size < 5) return 0;

	FuzzedDataProvider fdp(Data, Size);

	// 1. Prepare XPath variable set
	// Adding variables helps exercise more complex query evaluation paths.
	pugi::xpath_variable_set vars;
	size_t var_count = fdp.ConsumeIntegralInRange<size_t>(0, 5);
	std::vector<std::string> var_names;
	for (size_t i = 0; i < var_count; ++i)
	{
		var_names.push_back(fdp.ConsumeRandomLengthString(32));
		pugi::xpath_value_type type = static_cast<pugi::xpath_value_type>(fdp.ConsumeIntegralInRange(0, 4));
		pugi::xpath_variable* var = vars.add(var_names.back().c_str(), type);
		if (var)
		{
			switch (type)
			{
			case pugi::xpath_type_boolean:
				var->set(fdp.ConsumeBool());
				break;
			case pugi::xpath_type_number:
				var->set(fdp.ConsumeFloatingPoint<double>());
				break;
			case pugi::xpath_type_string:
				var->set(fdp.ConsumeRandomLengthString(64).c_str());
				break;
			default:
				break;
			}
		}
	}

	// Split remaining data between query and XML
	std::string query_str = fdp.ConsumeRandomLengthString(MAX_INPUT_SIZE);
	std::string xml_str = fdp.ConsumeRemainingBytesAsString();

	// 2. Prepare XML document
	pugi::xml_document doc;
	// Use some common parse flags to increase coverage
	unsigned int parse_flags = pugi::parse_default | pugi::parse_comments | pugi::parse_pi;
	doc.load_string(xml_str.c_str(), parse_flags);

	// 3. Evaluate XPath
#ifndef PUGIXML_NO_EXCEPTIONS
	try
#endif
	{
		pugi::xpath_query q(query_str.c_str(), &vars);
		if (q)
		{
			// The main target
			pugi::xpath_node_set ns = q.evaluate_node_set(doc);
			
			// Exercise the resulting node set to trigger sorting/duplicate removal
			// which typically happens during evaluation or when accessing the set.
			(void)ns.size();
			if (!ns.empty())
			{
				(void)ns.first();
				// Accessing nodes in document order triggers sorting if not already sorted
				ns.sort();
				for (pugi::xpath_node_set::const_iterator it = ns.begin(); it != ns.end(); ++it)
				{
					(void)it->node().name();
					(void)it->attribute().name();
				}
			}

			// Also call other evaluation methods as they share a lot of code
			// and might be needed to reach some parts of the AST evaluation.
			(void)q.evaluate_boolean(doc);
			(void)q.evaluate_number(doc);
			(void)q.evaluate_string(doc);
			(void)q.evaluate_node(doc);
		}
	}
#ifndef PUGIXML_NO_EXCEPTIONS
	catch (const pugi::xpath_exception&) {}
	catch (const std::bad_alloc&) {}
#endif

	return 0;
}
