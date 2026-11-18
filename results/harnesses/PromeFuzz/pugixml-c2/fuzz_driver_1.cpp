#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>

#include "pugixml.hpp"

static void exercise_document(pugi::xml_document &doc)
{

    for (pugi::xml_node node = doc.first_child(); node; node = node.next_sibling())
    {
        node.name();
        node.value();
        for (pugi::xml_attribute attr = node.first_attribute(); attr; attr = attr.next_attribute())
        {
            attr.name();
            attr.value();
            attr.as_int();
            attr.as_double();
            attr.as_bool();
        }

        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            child.text().as_string();
            child.text().as_int();
            child.text().as_double();
        }
    }

    try
    {
        pugi::xpath_node_set nodes = doc.select_nodes("//*");
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            pugi::xpath_node xnode = nodes[i];
            if (xnode.node())
            {
                xnode.node().name();
            }
        }

        pugi::xpath_node single = doc.select_node("/*");
        if (single)
        {
            single.node().name();
        }
    }
    catch (const pugi::xpath_exception &)
    {

    }

    pugi::xml_node root = doc.root();
    if (root)
    {
        pugi::xml_node new_node = root.append_child("fuzz_node");
        new_node.append_attribute("fuzz_attr") = "fuzz_value";
        new_node.append_child(pugi::node_pcdata).set_value("fuzz_text");

        pugi::xml_node first = root.first_child();
        if (first)
        {
            root.remove_child(first);
        }
    }

    std::ostringstream oss;
    doc.save(oss);
    std::string serialized = oss.str();

    std::ostringstream oss2;
    doc.save(oss2, "  ", pugi::format_indent | pugi::format_no_declaration);

    std::string print_buffer;
    pugi::xml_node first_child = doc.first_child();
    if (first_child)
    {
        std::ostringstream oss3;
        first_child.print(oss3);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Data == nullptr || Size == 0)
    {
        return 0;
    }

    std::string input(reinterpret_cast<const char *>(Data), Size);

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(input.data(), input.size());
        if (result)
        {
            exercise_document(doc);
        }
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(
            input.data(), input.size(),
            pugi::parse_full | pugi::parse_ws_pcdata);
        if (result)
        {
            exercise_document(doc);
        }
    }

    {
        char *mutable_buffer = new char[input.size() + 1];
        std::memcpy(mutable_buffer, input.data(), input.size());
        mutable_buffer[input.size()] = '\0';

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer_inplace(mutable_buffer, input.size());
        if (result)
        {
            exercise_document(doc);
        }

        delete[] mutable_buffer;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(input.c_str());
        if (result)
        {
            exercise_document(doc);
        }
    }

    {
        const char *filename = "./dummy_file";
        std::ofstream ofs(filename, std::ios::binary);
        if (ofs)
        {
            ofs.write(reinterpret_cast<const char *>(Data), Size);
            ofs.close();

            pugi::xml_document doc;
            pugi::xml_parse_result result = doc.load_file(filename);
            if (result)
            {
                exercise_document(doc);
            }
        }
    }

    {
        pugi::xml_document doc;
        pugi::xml_node decl = doc.append_child(pugi::node_declaration);
        decl.append_attribute("version") = "1.0";

        pugi::xml_node root = doc.append_child("root");
        root.append_attribute("id") = static_cast<int>(Size);

        if (Size > 0)
        {
            std::string child_name(1, static_cast<char>('a' + (Data[0] % 26)));
            pugi::xml_node child = root.append_child(child_name.c_str());
            child.append_child(pugi::node_pcdata).set_value(input.c_str());
        }

        std::ostringstream oss;
        doc.save(oss);

        pugi::xml_document doc2;
        doc2.load_string(oss.str().c_str());
        exercise_document(doc2);
    }

    return 0;
}