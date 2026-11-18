#include <cstdint>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }

    uint8_t control = data[0];
    bool allow_unregistered = (control & 0x01) != 0;
    po::collect_unrecognized_mode mode =
        (control & 0x02) ? po::include_positional : po::exclude_positional;
    bool add_short_only_option = (control & 0x04) != 0;

    std::string content(reinterpret_cast<const char *>(data + 1), size - 1);

    po::options_description desc;
    try {
        po::options_description_easy_init easy = desc.add_options();
        easy("apple", po::value<std::string>(), "apple option")
            ("banana", po::value<int>(), "banana option")
            ("verbose", po::bool_switch(), "verbose flag")
            ("section.value", po::value<std::string>(), "sectioned option")
            ("plug*", po::value<std::string>(), "wildcard option");
        if (add_short_only_option) {

            easy(",o", po::value<std::string>(), "short only option");
        }
    } catch (const std::exception &) {

        return 0;
    }

    std::istringstream iss(content);

    po::parsed_options parsed(&desc);
    bool have_parsed = false;
    try {

        parsed = po::parse_config_file(iss, desc, allow_unregistered);
        have_parsed = true;
    } catch (const std::exception &) {

    }

    if (have_parsed) {

        size_t checksum = 0;
        for (size_t i = 0; i < parsed.options.size(); ++i) {
            const po::basic_option<char> &opt = parsed.options[i];
            checksum += opt.string_key.size();
            checksum += static_cast<size_t>(opt.unregistered);
            for (size_t j = 0; j < opt.value.size(); ++j) {
                checksum += opt.value[j].size();
            }
        }

        std::vector<std::string> unrecognized =
            po::collect_unrecognized(parsed.options, mode);
        for (size_t i = 0; i < unrecognized.size(); ++i) {
            checksum += unrecognized[i].size();
        }
        if (checksum == static_cast<size_t>(-1)) {

            return 1;
        }

        po::variables_map vm;
        try {
            po::store(parsed, vm);
            po::notify(vm);
        } catch (const std::exception &) {

        }
    }

    return 0;
}
