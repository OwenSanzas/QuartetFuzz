#include <cstddef>
#include <cstdint>
#include <glaze/glaze.hpp>
#include <glaze/beve.hpp>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct nested
{
   int n = 0;
   std::string label{};
   std::vector<double> data{};
};

struct my_struct
{
   int i = 287;
   double d = 3.14;
   std::string hello = "Hello World";
   std::array<uint64_t, 3> arr = {1, 2, 3};
   std::optional<std::string> maybe{};
   std::map<std::string, int> dict{};
   std::vector<nested> kids{};
   std::variant<int, std::string, double> alt{};
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size == 0) {
      return 0;
   }

   const uint8_t selector = data[size - 1];
   const uint8_t* payload = data;
   size_t payload_size = size - 1;

   std::vector<char> buffer{reinterpret_cast<const char*>(payload),
                             reinterpret_cast<const char*>(payload) + payload_size};

   my_struct value{};
   glz::error_ctx ec{};
   if (selector & 0b001) {
      constexpr glz::opts lenient{.format = glz::BEVE, .error_on_unknown_keys = false, .partial_read = true};
      ec = glz::read<lenient>(value, buffer);
   }
   else {
      ec = glz::read_beve(value, buffer);
   }

   if (!ec) {

      std::string rewritten{};
      [[maybe_unused]] auto wec = glz::write_beve(value, rewritten);

      std::string json_output{};
      auto jec = glz::beve_to_json(buffer, json_output);
      if (!jec) {
         glz::generic generic_value{};
         [[maybe_unused]] auto gec = glz::read_json(generic_value, json_output);
      }
   }

   if (selector & 0b010) {

      my_struct value2{};
      [[maybe_unused]] auto ec2 = glz::read_beve_untagged(value2, buffer);
   }
   else {

      std::vector<my_struct> values{};
      [[maybe_unused]] auto ec3 = glz::read_beve_delimited(values, buffer);
   }

   return 0;
}
