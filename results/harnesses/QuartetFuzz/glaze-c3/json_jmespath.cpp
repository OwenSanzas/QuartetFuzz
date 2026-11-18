#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "glaze/json.hpp"
#include "glaze/json/jmespath.hpp"

namespace
{
   struct Person
   {
      std::string first_name{};
      std::string last_name{};
      uint16_t age{};
   };

   struct Family
   {
      Person father{};
      Person mother{};
      std::vector<Person> children{};
   };

   struct Home
   {
      Family family{};
      std::string address{};
   };
}

template <>
struct glz::meta<Person>
{
   using T = Person;
   static constexpr auto value = glz::object("first_name", &T::first_name, "last_name", &T::last_name, "age", &T::age);
};

template <>
struct glz::meta<Family>
{
   using T = Family;
   static constexpr auto value = glz::object("father", &T::father, "mother", &T::mother, "children", &T::children);
};

template <>
struct glz::meta<Home>
{
   using T = Home;
   static constexpr auto value = glz::object("family", &T::family, "address", &T::address);
};

namespace
{

   template <class T>
   void run_main(const glz::jmespath_expression& expression, std::string_view json_bytes, bool use_const_buffer,
                 bool null_terminated)
   {
      T value{};

      if (use_const_buffer) {
         const std::string const_buffer{json_bytes};
         if (null_terminated) {
            static constexpr glz::opts options{.null_terminated = true};
            auto ec = glz::read_jmespath<options>(expression, value, const_buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, const_buffer);
            }
         }
         else {
            static constexpr glz::opts options{.null_terminated = false};
            auto ec = glz::read_jmespath<options>(expression, value, const_buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, const_buffer);
            }
         }
      }
      else {
         std::string mutable_buffer{json_bytes};
         if (null_terminated) {
            static constexpr glz::opts options{.null_terminated = true};
            auto ec = glz::read_jmespath<options>(expression, value, mutable_buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, mutable_buffer);
            }
         }
         else {
            static constexpr glz::opts options{.null_terminated = false};
            auto ec = glz::read_jmespath<options>(expression, value, mutable_buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, mutable_buffer);
            }
         }
      }
   }

   void run_addition(int choice, std::string_view json_bytes)
   {
      switch (choice & 0x3) {
         case 0: {

            std::vector<int> value{};
            std::string buffer{json_bytes};
            auto ec = glz::read_jmespath<"[0:3]">(value, buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, buffer);
            }
            break;
         }
         case 1: {

            std::tuple<int, int, int> value{};
            std::string buffer{json_bytes};
            auto ec = glz::read_jmespath<"[0:3]">(value, buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, buffer);
            }
            break;
         }
         case 2: {

            double value{};
            std::string buffer{json_bytes};
            auto ec = glz::read_jmespath<"[0:3]">(value, buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value, out);
            }
            else {
               (void)glz::format_error(ec, buffer);
            }
            break;
         }
         default: {

            Home value{};
            std::string buffer{json_bytes};
            auto ec = glz::read_jmespath<"family.father.first_name">(value.family.father.first_name, buffer);
            if (not ec) {
               std::string out{};
               (void)glz::write_json(value.family.father.first_name, out);
            }
            else {
               (void)glz::format_error(ec, buffer);
            }
            break;
         }
      }
   }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
   if (size < 3) {
      return 0;
   }

   const uint8_t control = data[0];
   const uint8_t addition_control = data[1];
   const uint8_t path_len_byte = data[2];
   const uint8_t* rest = data + 3;
   const size_t rest_size = size - 3;

   const size_t path_len = (rest_size == 0) ? 0 : (static_cast<size_t>(path_len_byte) % (rest_size + 1));

   const std::string path(reinterpret_cast<const char*>(rest), path_len);
   const std::string_view json_bytes(reinterpret_cast<const char*>(rest + path_len), rest_size - path_len);

   const glz::jmespath_expression expression{std::string_view{path}};

   const bool use_generic = (control & 0x1) != 0;
   const bool null_terminated = (control & 0x2) != 0;
   const bool use_const_buffer = (control & 0x4) != 0;
   const bool do_addition = (control & 0x8) != 0;

   if (use_generic) {
      run_main<glz::generic>(expression, json_bytes, use_const_buffer, null_terminated);
   }
   else {
      run_main<Home>(expression, json_bytes, use_const_buffer, null_terminated);
   }

   if (do_addition) {
      run_addition(addition_control, json_bytes);
   }

   return 0;
}
