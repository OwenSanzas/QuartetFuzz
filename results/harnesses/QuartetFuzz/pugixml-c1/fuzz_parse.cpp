#include "../src/pugixml.hpp"
#include "fuzzer/FuzzedDataProvider.h"

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <sstream>

static unsigned long walk(const pugi::xml_node& node, int depth)
{
	unsigned long acc = 0;
	if (depth > 64) return acc;

	for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
	{
		const char* n = attr.name();
		const char* v = attr.value();
		acc += n ? strlen(n) : 0;
		acc += v ? strlen(v) : 0;
	}

	const char* nm = node.name();
	const char* val = node.value();
	acc += nm ? strlen(nm) : 0;
	acc += val ? strlen(val) : 0;
	acc += (unsigned long)node.type();

	for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
		acc += walk(child, depth + 1);

	return acc;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
	FuzzedDataProvider fdp(Data, Size);

	unsigned int options1 = fdp.ConsumeIntegral<uint16_t>() & 0x7FFFu;
	int enc1 = fdp.ConsumeIntegralInRange<int>(0, 9);
	size_t len1 = fdp.ConsumeIntegralInRange<size_t>(0, fdp.remaining_bytes());
	std::vector<uint8_t> buf1 = fdp.ConsumeBytes<uint8_t>(len1);

	unsigned int options2 = fdp.ConsumeIntegral<uint16_t>() & 0x7FFFu;
	int enc2 = fdp.ConsumeIntegralInRange<int>(0, 9);
	size_t len2 = fdp.ConsumeIntegralInRange<size_t>(0, fdp.remaining_bytes());
	std::vector<uint8_t> buf2 = fdp.ConsumeBytes<uint8_t>(len2);

	unsigned int options3 = fdp.ConsumeIntegral<uint16_t>() & 0x7FFFu;
	int enc3 = fdp.ConsumeIntegralInRange<int>(0, 9);
	size_t len3 = fdp.ConsumeIntegralInRange<size_t>(0, fdp.remaining_bytes());
	std::vector<char> buf3 = fdp.ConsumeBytes<char>(len3);

	unsigned int options4 = fdp.ConsumeIntegral<uint16_t>() & 0x7FFFu;
	int enc4 = fdp.ConsumeIntegralInRange<int>(0, 9);
	size_t len4 = fdp.ConsumeIntegralInRange<size_t>(0, fdp.remaining_bytes());
	std::vector<uint8_t> buf4src = fdp.ConsumeBytes<uint8_t>(len4);

	unsigned int options5 = fdp.ConsumeIntegral<uint16_t>() & 0x7FFFu;
	std::string strcontent = fdp.ConsumeRemainingBytesAsString();

	pugi::xml_document doc;
	volatile unsigned long sink = 0;

	pugi::xml_parse_result r1 = doc.load_buffer(buf1.empty() ? NULL : buf1.data(), buf1.size(), options1, (pugi::xml_encoding)enc1);
	(void)r1;
	sink += walk(doc, 0);
	{
		std::ostringstream oss;
		doc.print(oss, "  ", pugi::format_default, pugi::encoding_utf8);
		(void)oss.str();
	}

	pugi::xml_parse_result r2 = doc.append_buffer(buf2.empty() ? NULL : buf2.data(), buf2.size(), options2, (pugi::xml_encoding)enc2);
	(void)r2;
	sink += walk(doc, 0);
	{
		std::ostringstream oss;
		doc.print(oss, "  ", pugi::format_raw, pugi::encoding_utf8);
		(void)oss.str();
	}

	pugi::xml_parse_result r3 = doc.load_buffer_inplace(buf3.empty() ? NULL : buf3.data(), buf3.size(), options3, (pugi::xml_encoding)enc3);
	(void)r3;
	sink += walk(doc, 0);
	{
		std::ostringstream oss;
		doc.print(oss, "\t", pugi::format_indent, pugi::encoding_utf8);
		(void)oss.str();
	}

	size_t ownlen = buf4src.size();
	void* ownbuf = NULL;
	if (ownlen > 0)
	{
		ownbuf = pugi::get_memory_allocation_function()(ownlen);
		if (ownbuf) memcpy(ownbuf, buf4src.data(), ownlen);
	}
	if (ownlen == 0 || ownbuf != NULL)
	{
		pugi::xml_parse_result r4 = doc.load_buffer_inplace_own(ownbuf, ownlen, options4, (pugi::xml_encoding)enc4);
		(void)r4;
		sink += walk(doc, 0);
		std::ostringstream oss;
		doc.print(oss, " ", pugi::format_no_declaration, pugi::encoding_utf8);
		(void)oss.str();
	}

	pugi::xml_parse_result r5 = doc.load_string(strcontent.c_str(), options5);
	(void)r5;
	sink += walk(doc, 0);
	{
		std::ostringstream oss;
		doc.print(oss);
		(void)oss.str();
	}

	(void)sink;
	return 0;
}
