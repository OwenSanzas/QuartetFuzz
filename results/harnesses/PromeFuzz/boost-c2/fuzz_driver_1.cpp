#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/options_description.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <exception>

namespace po = boost::program_options;

static po::options_description build_options_description()
{
    po::options_description desc("Fuzz Test Options");
    desc.add_options()
        ("option1", po::value<std::string>(), "first option")
        ("option2", po::value<int>(), "second option")
        ("flag", po::value<bool>(), "boolean flag")
        ("str_option", po::value<std::string>()->default_value(""), "string option")
        ("multi_option", po::value<std::vector<std::string>>()->multitoken(), "multi token option")
        ("num_option", po::value<double>(), "numeric option");
    return desc;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0) {
        return 0;
    }

    bool allow_unregistered = (Data[0] % 2 == 0);

    std::string config_content(reinterpret_cast<const char*>(Data + 1),
                                (Size > 1) ? Size - 1 : 0);

    po::options_description desc = build_options_description();

    try {
        std::istringstream iss(config_content);
        po::basic_parsed_options<char> parsed_opts =
            po::parse_config_file(iss, desc, allow_unregistered);

        for (const auto& opt : parsed_opts.options) {
            (void)opt.string_key;
            (void)opt.value;
        }
    } catch (const std::exception &e) {

    } catch (...) {

    }

    const char* dummy_filename = "./dummy_file";
    {
        std::ofstream ofs(dummy_filename, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(config_content.data(), static_cast<std::streamsize>(config_content.size()));
            ofs.close();
        }
    }

    try {
        po::basic_parsed_options<char> parsed_opts =
            po::parse_config_file(dummy_filename, desc, allow_unregistered);

        for (const auto& opt : parsed_opts.options) {
            (void)opt.string_key;
            (void)opt.value;
        }
    } catch (const std::exception &e) {

    } catch (...) {

    }

    std::remove(dummy_filename);

    return 0;
}