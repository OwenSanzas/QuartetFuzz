#include <array>
#include <bitset>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <glaze/cbor.hpp>
#include <glaze/glaze.hpp>

#include "glaze/containers/inplace_vector.hpp"

enum class FuzzPlainEnum { A, B, C, D };

enum class FuzzNamedEnum { Red, Green, Blue };

template <>
struct glz::meta<FuzzNamedEnum>
{
   static constexpr std::string_view name = "FuzzNamedEnum";
   static constexpr auto value = glz::enumerate("Red", FuzzNamedEnum::Red,
                                                 "Green", FuzzNamedEnum::Green,
                                                 "Blue", FuzzNamedEnum::Blue);
};

struct FuzzInner
{
   int32_t x{};
   std::string y{};
};

struct FuzzValueWrapper
{
   int32_t inner{};
};
template <>
struct glz::meta<FuzzValueWrapper>
{
   static constexpr auto value = &FuzzValueWrapper::inner;
};

struct FuzzArrayLike
{
   int32_t a{};
   double b{};
   std::string c{};
};
template <>
struct glz::meta<FuzzArrayLike>
{
   static constexpr auto value = glz::reflect_array{};
};

struct FuzzNullableValueLike
{
   int32_t data_{};
   bool has_val_ = false;

   bool has_value() const { return has_val_; }
   int32_t& value() { return data_; }
   const int32_t& value() const { return data_; }
   void reset() { has_val_ = false; }
   void emplace() { has_val_ = true; }
};

struct FuzzStruct
{

   int32_t i{};
   double d{};
   std::string s{};
   std::vector<int32_t> v{};
   bool b{};

   std::optional<int32_t> opt{};
   std::map<std::string, int32_t> m{};
   std::pair<int32_t, std::string> p{};
   std::tuple<int32_t, double, std::string> tup{};
   std::variant<int32_t, std::string, double> var{};
   FuzzPlainEnum plain_enum{};
   FuzzNamedEnum named_enum{};
   std::vector<uint8_t> bytes{};
   std::vector<float> fv{};
   std::vector<double> dv{};
   std::array<int32_t, 4> arr{};
   std::bitset<16> bits{};
   std::complex<double> cplx{};
   std::vector<std::complex<double>> cplx_v{};
   std::chrono::system_clock::time_point tp{};
   glz::epoch_seconds epoch{};
   std::expected<int32_t, int32_t> exp{};
   FuzzInner inner{};

   FuzzValueWrapper vw{};
   FuzzArrayLike array_like{};
   FuzzNullableValueLike nvl{};
   std::unique_ptr<int32_t> uptr{};
   std::shared_ptr<int32_t> sptr{};
   std::filesystem::path fpath{};
   uint32_t u32{};

   std::list<int32_t> lst{};
   std::deque<int32_t> dq{};
   std::set<int32_t> st{};
   std::unordered_set<int32_t> ust{};
   std::unordered_map<std::string, int32_t> umap{};
   std::list<uint8_t> byte_list{};
   std::array<char, 8> fixed_str{};
   std::array<uint8_t, 8> fixed_bytes{};
};

static volatile uint64_t g_sink = 0;

namespace
{
   void sink_struct(const FuzzStruct& value)
   {
      g_sink ^= static_cast<uint64_t>(value.i);
      g_sink ^= static_cast<uint64_t>(value.b);
      g_sink ^= value.s.size();
      g_sink ^= value.v.size();
      g_sink ^= value.opt.has_value() ? 1u : 0u;
      g_sink ^= value.m.size();
      g_sink ^= static_cast<uint64_t>(value.p.first);
      g_sink ^= value.bytes.size();
      g_sink ^= value.fv.size();
      g_sink ^= value.dv.size();
      g_sink ^= value.bits.to_ullong();
      g_sink ^= value.cplx_v.size();
      g_sink ^= static_cast<uint64_t>(value.var.index());
      g_sink ^= static_cast<uint64_t>(value.plain_enum);
      g_sink ^= static_cast<uint64_t>(value.named_enum);
      g_sink ^= value.exp.has_value() ? static_cast<uint64_t>(value.exp.value())
                                       : static_cast<uint64_t>(value.exp.error());
      g_sink ^= static_cast<uint64_t>(value.inner.x);
      g_sink ^= value.inner.y.size();
      {
         uint64_t bits = 0;
         std::memcpy(&bits, &value.d, sizeof(bits));
         g_sink ^= bits;
      }
      if (!value.v.empty()) {
         g_sink ^= static_cast<uint64_t>(value.v.front());
      }

      g_sink ^= static_cast<uint64_t>(value.vw.inner);
      g_sink ^= static_cast<uint64_t>(value.array_like.a);
      g_sink ^= value.array_like.c.size();
      g_sink ^= value.nvl.has_value() ? 1u : 0u;
      g_sink ^= static_cast<uint64_t>(value.nvl.data_);
      g_sink ^= value.uptr ? static_cast<uint64_t>(*value.uptr) : 0u;
      g_sink ^= value.sptr ? static_cast<uint64_t>(*value.sptr) : 0u;
      g_sink ^= value.fpath.native().size();
      g_sink ^= static_cast<uint64_t>(value.u32);

      g_sink ^= value.lst.size();
      g_sink ^= value.dq.size();
      g_sink ^= value.st.size();
      g_sink ^= value.ust.size();
      g_sink ^= value.umap.size();
      g_sink ^= value.byte_list.size();
      g_sink ^= static_cast<uint64_t>(static_cast<unsigned char>(value.fixed_str[0]));
      g_sink ^= static_cast<uint64_t>(value.fixed_bytes[0]);
   }

   void sink_ec(const glz::error_ctx& ec)
   {
      g_sink ^= static_cast<uint64_t>(ec.ec);
      g_sink ^= ec.count;
   }

   void append_cbor_header(std::string& s, uint8_t major_type, uint64_t arg)
   {
      const uint8_t m = static_cast<uint8_t>(major_type << 5);
      if (arg < 24) {
         s.push_back(static_cast<char>(m | static_cast<uint8_t>(arg)));
      }
      else if (arg <= 0xFFull) {
         s.push_back(static_cast<char>(m | 24));
         s.push_back(static_cast<char>(arg & 0xFF));
      }
      else if (arg <= 0xFFFFull) {
         s.push_back(static_cast<char>(m | 25));
         s.push_back(static_cast<char>((arg >> 8) & 0xFF));
         s.push_back(static_cast<char>(arg & 0xFF));
      }
      else if (arg <= 0xFFFFFFFFull) {
         s.push_back(static_cast<char>(m | 26));
         for (int idx = 3; idx >= 0; --idx) {
            s.push_back(static_cast<char>((arg >> (8 * idx)) & 0xFF));
         }
      }
      else {
         s.push_back(static_cast<char>(m | 27));
         for (int idx = 7; idx >= 0; --idx) {
            s.push_back(static_cast<char>((arg >> (8 * idx)) & 0xFF));
         }
      }
   }

   template <class V>
   void test_typed_array_tag(uint64_t tag, const std::string& source)
   {
      std::string payload;
      append_cbor_header(payload, glz::cbor::major::tag, tag);
      size_t nbytes = (source.size() / sizeof(V)) * sizeof(V);
      nbytes = std::min<size_t>(nbytes, 64);
      append_cbor_header(payload, glz::cbor::major::bstr, nbytes);
      payload.append(source.data(), nbytes);

      std::vector<V> vec{};
      sink_ec(glz::read_cbor(vec, payload));
      g_sink ^= vec.size();

      std::list<V> lst{};
      sink_ec(glz::read_cbor(lst, payload));
      g_sink ^= lst.size();
   }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

   std::string buffer(reinterpret_cast<const char*>(data), size);

   FuzzStruct value{};

   glz::error_ctx ec = glz::read_cbor(value, buffer);

   g_sink ^= static_cast<uint64_t>(ec.ec);
   g_sink ^= ec.count;
   sink_struct(value);

   FuzzStruct value2{};
   using ReadCborFnPtr = glz::error_ctx (*)(FuzzStruct&, std::string&);
   static volatile ReadCborFnPtr read_cbor_fn = &glz::read_cbor<FuzzStruct&, std::string&>;
   glz::error_ctx ec2 = read_cbor_fn(value2, buffer);

   g_sink ^= static_cast<uint64_t>(ec2.ec);
   g_sink ^= ec2.count;
   sink_struct(value2);

   FuzzStruct original{};
   if (!buffer.empty()) {
      original.i = static_cast<int32_t>(static_cast<unsigned char>(buffer[0]));
   }
   if (buffer.size() > 1) {
      original.b = (static_cast<unsigned char>(buffer[1]) & 1) != 0;
   }
   original.s = buffer;
   if (buffer.size() >= sizeof(double)) {
      std::memcpy(&original.d, buffer.data(), sizeof(double));
   }
   for (size_t idx = 0; idx + sizeof(int32_t) <= buffer.size() && original.v.size() < 16; idx += sizeof(int32_t)) {
      int32_t elem = 0;
      std::memcpy(&elem, buffer.data() + idx, sizeof(int32_t));
      original.v.push_back(elem);
   }

   if (!buffer.empty()) {
      original.opt = static_cast<int32_t>(static_cast<unsigned char>(buffer[0]));
   }
   else {
      original.opt = std::nullopt;
   }

   original.m["k1"] = original.i;
   if (buffer.size() > 2) {
      original.m["k2"] = static_cast<int32_t>(static_cast<unsigned char>(buffer[2]));
   }

   original.p = std::make_pair(original.i, original.s.substr(0, 8));

   original.tup = std::make_tuple(original.i, original.d, original.s.substr(0, 4));

   switch (size % 3) {
      case 0:
         original.var = original.i;
         break;
      case 1:
         original.var = original.s.substr(0, 6);
         break;
      default:
         original.var = original.d;
         break;
   }

   original.plain_enum = static_cast<FuzzPlainEnum>(size % 4);
   original.named_enum = static_cast<FuzzNamedEnum>(size % 3);

   original.bytes.assign(buffer.begin(), buffer.begin() + std::min<size_t>(buffer.size(), 32));

   for (size_t idx = 0; idx + sizeof(float) <= buffer.size() && original.fv.size() < 8; idx += sizeof(float)) {
      float elem = 0.0f;
      std::memcpy(&elem, buffer.data() + idx, sizeof(float));
      original.fv.push_back(elem);
   }
   for (size_t idx = 0; idx + sizeof(double) <= buffer.size() && original.dv.size() < 8; idx += sizeof(double)) {
      double elem = 0.0;
      std::memcpy(&elem, buffer.data() + idx, sizeof(double));
      original.dv.push_back(elem);
   }

   for (size_t k = 0; k < original.arr.size(); ++k) {
      original.arr[k] = static_cast<int32_t>(buffer.size() > k ? static_cast<unsigned char>(buffer[k]) : 0);
   }

   if (!buffer.empty()) {
      original.bits = std::bitset<16>(static_cast<unsigned long long>(static_cast<unsigned char>(buffer[0])) |
                                       (buffer.size() > 1
                                           ? (static_cast<unsigned long long>(static_cast<unsigned char>(buffer[1])) << 8)
                                           : 0ull));
   }

   original.cplx = std::complex<double>(original.d, static_cast<double>(original.i));
   original.cplx_v.push_back(original.cplx);
   if (buffer.size() > 4) {
      original.cplx_v.push_back(std::complex<double>(static_cast<double>(buffer[3]), static_cast<double>(buffer[4])));
   }

   original.tp = std::chrono::system_clock::time_point{
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
         std::chrono::seconds{static_cast<int64_t>(original.i)})};

   original.epoch.value = original.tp;

   if (size % 2 == 0) {
      original.exp = original.i;
   }
   else {
      original.exp = std::unexpected(original.i);
   }

   original.inner.x = original.i;
   original.inner.y = original.s.substr(0, 10);

   original.vw.inner = original.i;

   original.array_like.a = original.i;
   original.array_like.b = original.d;
   original.array_like.c = original.s.substr(0, 5);

   original.nvl.data_ = original.i;
   original.nvl.has_val_ = (size % 2 == 0);

   if (size % 2 == 0) {
      original.uptr = std::make_unique<int32_t>(original.i);
      original.sptr = std::make_shared<int32_t>(original.i);
   }
   else {
      original.uptr.reset();
      original.sptr.reset();
   }

   original.fpath = std::filesystem::path(original.s.substr(0, 10));

   original.u32 = static_cast<uint32_t>(original.i);

   for (size_t idx = 0; idx + sizeof(int32_t) <= buffer.size() && original.lst.size() < 8; idx += sizeof(int32_t)) {
      int32_t elem = 0;
      std::memcpy(&elem, buffer.data() + idx, sizeof(int32_t));
      original.lst.push_back(elem);
      original.dq.push_back(elem);
      original.st.insert(elem);
      original.ust.insert(elem);
   }
   original.byte_list.assign(original.bytes.begin(), original.bytes.end());
   original.umap["k1"] = original.i;
   if (buffer.size() > 3) {
      original.umap["k2"] = static_cast<int32_t>(static_cast<unsigned char>(buffer[3]));
   }
   {
      const size_t n = std::min<size_t>(original.s.size(), original.fixed_str.size());
      for (size_t k = 0; k < original.fixed_str.size(); ++k) {
         original.fixed_str[k] = (k < n) ? original.s[k] : '\0';
      }
   }
   for (size_t k = 0; k < original.fixed_bytes.size(); ++k) {
      original.fixed_bytes[k] = buffer.size() > k ? static_cast<uint8_t>(buffer[k]) : 0;
   }

   std::string encoded{};
   auto write_ec = glz::write_cbor(original, encoded);
   if (!write_ec) {
      FuzzStruct restored{};
      glz::error_ctx ec3 = glz::read_cbor(restored, encoded);
      g_sink ^= static_cast<uint64_t>(ec3.ec);
      g_sink ^= ec3.count;
      sink_struct(restored);
   }

   if (data != nullptr && size > 0) {
      const uint8_t selector = data[0];
      std::string sub(reinterpret_cast<const char*>(data + 1), size > 1 ? size - 1 : 0);

      switch (selector % 37) {
         case 0: {
            std::bitset<16> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.to_ullong();
            break;
         }
         case 1: {
            std::complex<double> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.real());
            break;
         }
         case 2: {
            std::vector<std::complex<double>> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 3: {
            std::variant<int32_t, std::string, double> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.index());
            break;
         }
         case 4: {
            std::optional<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.has_value() ? 1u : 0u;
            break;
         }
         case 5: {
            std::expected<int32_t, int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.has_value() ? static_cast<uint64_t>(v.value()) : static_cast<uint64_t>(v.error());
            break;
         }
         case 6: {
            std::map<std::string, int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 7: {
            std::pair<int32_t, std::string> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.first);
            break;
         }
         case 8: {
            std::tuple<int32_t, double, std::string> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(std::get<0>(v));
            break;
         }
         case 9: {
            std::array<int32_t, 4> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v[0]);
            break;
         }
         case 10: {
            std::vector<float> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 11: {
            std::vector<double> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 12: {
            std::vector<uint8_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 13: {
            FuzzPlainEnum v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v);
            break;
         }
         case 14: {
            FuzzNamedEnum v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v);
            break;
         }
         case 15: {
            std::chrono::system_clock::time_point v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.time_since_epoch().count());
            break;
         }
         case 16: {
            glz::epoch_seconds v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.value.time_since_epoch().count());
            break;
         }
         case 17: {
            FuzzInner v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.x);
            g_sink ^= v.y.size();
            break;
         }
         case 18: {
            std::string v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 19: {
            std::vector<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 20: {

            std::monostate v{};
            sink_ec(glz::read_cbor(v, sub));
            break;
         }
         case 21: {

            FuzzValueWrapper v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.inner);
            break;
         }
         case 22: {

            FuzzNullableValueLike v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.has_value() ? 1u : 0u;
            g_sink ^= static_cast<uint64_t>(v.data_);
            break;
         }
         case 23: {

            FuzzArrayLike v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v.a);
            g_sink ^= v.c.size();
            break;
         }
         case 24: {

            std::unique_ptr<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v ? static_cast<uint64_t>(*v) : 0u;
            break;
         }
         case 25: {

            std::shared_ptr<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v ? static_cast<uint64_t>(*v) : 0u;
            break;
         }
         case 26: {

            std::filesystem::path v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.native().size();
            break;
         }
         case 27: {

            std::list<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 28: {

            std::deque<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 29: {

            std::set<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 30: {

            std::unordered_set<int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 31: {

            std::unordered_map<std::string, int32_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 32: {

            std::list<uint8_t> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= v.size();
            break;
         }
         case 33: {

            std::array<char, 8> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(static_cast<unsigned char>(v[0]));
            break;
         }
         case 34: {

            std::array<uint8_t, 8> v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v[0]);
            break;
         }
         case 35: {

            glz::skip v{};
            sink_ec(glz::read_cbor(v, sub));
            break;
         }
         default: {

            uint32_t v{};
            sink_ec(glz::read_cbor(v, sub));
            g_sink ^= static_cast<uint64_t>(v);
            break;
         }
      }
   }

   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::tstr << 5) | glz::cbor::info::indefinite));
      const std::string chunk1 = buffer.substr(0, std::min<size_t>(buffer.size(), 4));
      append_cbor_header(payload, glz::cbor::major::tstr, chunk1.size());
      payload += chunk1;
      if (buffer.size() > 4) {
         const std::string chunk2 = buffer.substr(4, std::min<size_t>(buffer.size() - 4, 4));
         append_cbor_header(payload, glz::cbor::major::tstr, chunk2.size());
         payload += chunk2;
      }
      payload.push_back(static_cast<char>(0xFF));
      std::string out{};
      sink_ec(glz::read_cbor(out, payload));
      g_sink ^= out.size();
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::bstr << 5) | glz::cbor::info::indefinite));
      const std::string chunk1 = buffer.substr(0, std::min<size_t>(buffer.size(), 4));
      append_cbor_header(payload, glz::cbor::major::bstr, chunk1.size());
      payload += chunk1;
      if (buffer.size() > 4) {
         const std::string chunk2 = buffer.substr(4, std::min<size_t>(buffer.size() - 4, 4));
         append_cbor_header(payload, glz::cbor::major::bstr, chunk2.size());
         payload += chunk2;
      }
      payload.push_back(static_cast<char>(0xFF));
      std::vector<uint8_t> out{};
      sink_ec(glz::read_cbor(out, payload));
      g_sink ^= out.size();
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::array << 5) | glz::cbor::info::indefinite));
      for (size_t k = 0; k < 4 && k < buffer.size(); ++k) {
         append_cbor_header(payload, glz::cbor::major::uint, static_cast<uint64_t>(static_cast<unsigned char>(buffer[k])));
      }
      payload.push_back(static_cast<char>(0xFF));
      std::vector<int32_t> out{};
      sink_ec(glz::read_cbor(out, payload));
      g_sink ^= out.size();
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::map << 5) | glz::cbor::info::indefinite));
      const std::string key = "k";
      append_cbor_header(payload, glz::cbor::major::tstr, key.size());
      payload += key;
      const uint64_t val = buffer.empty() ? 0u : static_cast<unsigned char>(buffer[0]);
      append_cbor_header(payload, glz::cbor::major::uint, val);
      payload.push_back(static_cast<char>(0xFF));
      std::map<std::string, int32_t> out{};
      sink_ec(glz::read_cbor(out, payload));
      g_sink ^= out.size();
   }

   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::tag, glz::cbor::semantic_tag::datetime_epoch);
      const uint64_t secs = buffer.empty() ? 0u : static_cast<unsigned char>(buffer[0]);
      append_cbor_header(payload, glz::cbor::major::uint, secs);
      std::chrono::system_clock::time_point tp{};
      sink_ec(glz::read_cbor(tp, payload));
      g_sink ^= static_cast<uint64_t>(tp.time_since_epoch().count());
   }
   {

      std::string payload;
      static constexpr std::string_view datetime = "1970-01-01T00:00:00Z";
      append_cbor_header(payload, glz::cbor::major::tstr, datetime.size());
      payload += datetime;
      std::chrono::system_clock::time_point tp{};
      sink_ec(glz::read_cbor(tp, payload));
      g_sink ^= static_cast<uint64_t>(tp.time_since_epoch().count());
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::map, 0);
      std::expected<int32_t, int32_t> e{};
      sink_ec(glz::read_cbor(e, payload));
      g_sink ^= e.has_value() ? static_cast<uint64_t>(e.value()) : static_cast<uint64_t>(e.error());
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::map, 2);
      const std::string k1 = "a";
      append_cbor_header(payload, glz::cbor::major::tstr, k1.size());
      payload += k1;
      append_cbor_header(payload, glz::cbor::major::uint, 1);
      const std::string k2 = "b";
      append_cbor_header(payload, glz::cbor::major::tstr, k2.size());
      payload += k2;
      append_cbor_header(payload, glz::cbor::major::uint, 2);
      std::expected<int32_t, int32_t> e{};
      sink_ec(glz::read_cbor(e, payload));
      g_sink ^= e.has_value() ? static_cast<uint64_t>(e.value()) : static_cast<uint64_t>(e.error());
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::map, 1);
      const std::string key = "nope";
      append_cbor_header(payload, glz::cbor::major::tstr, key.size());
      payload += key;
      append_cbor_header(payload, glz::cbor::major::uint, 42);
      std::expected<int32_t, int32_t> e{};
      sink_ec(glz::read_cbor(e, payload));
      g_sink ^= e.has_value() ? static_cast<uint64_t>(e.value()) : static_cast<uint64_t>(e.error());
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::map, 1);
      const std::string key = "unexpected";
      append_cbor_header(payload, glz::cbor::major::tstr, key.size());
      payload += key;
      const uint64_t err_val = buffer.empty() ? 7u : static_cast<unsigned char>(buffer[0]);
      append_cbor_header(payload, glz::cbor::major::uint, err_val);
      std::expected<int32_t, int32_t> e{};
      sink_ec(glz::read_cbor(e, payload));
      g_sink ^= e.has_value() ? static_cast<uint64_t>(e.value()) : static_cast<uint64_t>(e.error());
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::tag, glz::cbor::semantic_tag::complex_number);
      append_cbor_header(payload, glz::cbor::major::array, 2);
      const uint64_t re = buffer.empty() ? 0u : static_cast<unsigned char>(buffer[0]);
      const uint64_t im = buffer.size() > 1 ? static_cast<unsigned char>(buffer[1]) : 0u;
      append_cbor_header(payload, glz::cbor::major::uint, re);
      append_cbor_header(payload, glz::cbor::major::uint, im);
      std::complex<double> c{};
      sink_ec(glz::read_cbor(c, payload));
      g_sink ^= static_cast<uint64_t>(c.real());
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::tag, glz::cbor::semantic_tag::ta_float64_le);
      size_t nbytes = (buffer.size() / sizeof(double)) * sizeof(double);
      nbytes = std::min<size_t>(nbytes, 64);
      append_cbor_header(payload, glz::cbor::major::bstr, nbytes);
      payload.append(buffer.data(), nbytes);
      std::vector<double> v{};
      sink_ec(glz::read_cbor(v, payload));
      g_sink ^= v.size();
   }
   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::tag, glz::cbor::semantic_tag::ta_float64_be);
      size_t nbytes = (buffer.size() / sizeof(double)) * sizeof(double);
      nbytes = std::min<size_t>(nbytes, 64);
      append_cbor_header(payload, glz::cbor::major::bstr, nbytes);
      payload.append(buffer.data(), nbytes);
      std::vector<double> v{};
      sink_ec(glz::read_cbor(v, payload));
      g_sink ^= v.size();
   }

   {

      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, buffer));
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>(0xFF));
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }
   {

      for (uint8_t info_val : {static_cast<uint8_t>(28), static_cast<uint8_t>(29), static_cast<uint8_t>(30)}) {
         std::string payload;
         payload.push_back(static_cast<char>((glz::cbor::major::simple << 5) | info_val));
         glz::skip sk{};
         sink_ec(glz::read_cbor(sk, payload));
      }
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::simple << 5) | 24));
      payload.push_back(buffer.empty() ? static_cast<char>(0) : buffer[0]);
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }
   {

      for (uint8_t info_val :
           {glz::cbor::simple::float16, glz::cbor::simple::float32, glz::cbor::simple::float64}) {
         std::string payload;
         payload.push_back(static_cast<char>((glz::cbor::major::simple << 5) | info_val));
         const size_t nbytes =
            info_val == glz::cbor::simple::float16 ? 2 : (info_val == glz::cbor::simple::float32 ? 4 : 8);
         for (size_t k = 0; k < nbytes; ++k) {
            payload.push_back(buffer.size() > k ? buffer[k] : static_cast<char>(0));
         }
         glz::skip sk{};
         sink_ec(glz::read_cbor(sk, payload));
      }
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::array << 5) | glz::cbor::info::indefinite));
      append_cbor_header(payload, glz::cbor::major::uint, 7);
      append_cbor_header(payload, glz::cbor::major::map, 1);
      append_cbor_header(payload, glz::cbor::major::tstr, 1);
      payload += "x";
      append_cbor_header(payload, glz::cbor::major::uint, 1);
      append_cbor_header(payload, glz::cbor::major::tag, 0);
      append_cbor_header(payload, glz::cbor::major::tstr, 3);
      payload += "abc";
      payload.push_back(static_cast<char>((glz::cbor::major::bstr << 5) | glz::cbor::info::indefinite));
      append_cbor_header(payload, glz::cbor::major::bstr, 2);
      payload += "\x01\x02";
      payload.push_back(static_cast<char>(0xFF));
      payload.push_back(static_cast<char>(0xFF));
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::tstr << 5) | glz::cbor::info::indefinite));
      payload.push_back(static_cast<char>((glz::cbor::major::tstr << 5) | glz::cbor::info::indefinite));
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::tstr << 5) | glz::cbor::info::indefinite));
      append_cbor_header(payload, glz::cbor::major::bstr, 1);
      payload += "a";
      payload.push_back(static_cast<char>(0xFF));
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::map << 5) | glz::cbor::info::indefinite));
      append_cbor_header(payload, glz::cbor::major::tstr, 1);
      payload += "k";
      append_cbor_header(payload, glz::cbor::major::uint, 9);
      payload.push_back(static_cast<char>(0xFF));
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }
   {

      std::string payload;
      constexpr int kDepth = 300;
      for (int i = 0; i < kDepth; ++i) {
         append_cbor_header(payload, glz::cbor::major::array, 1);
      }
      append_cbor_header(payload, glz::cbor::major::uint, 0);
      glz::skip sk{};
      sink_ec(glz::read_cbor(sk, payload));
   }

   test_typed_array_tag<uint16_t>(glz::cbor::semantic_tag::ta_uint16_be, buffer);
   test_typed_array_tag<uint32_t>(glz::cbor::semantic_tag::ta_uint32_be, buffer);
   test_typed_array_tag<uint64_t>(glz::cbor::semantic_tag::ta_uint64_be, buffer);
   test_typed_array_tag<int16_t>(glz::cbor::semantic_tag::ta_sint16_be, buffer);
   test_typed_array_tag<int32_t>(glz::cbor::semantic_tag::ta_sint32_be, buffer);
   test_typed_array_tag<int64_t>(glz::cbor::semantic_tag::ta_sint64_be, buffer);
   test_typed_array_tag<float>(glz::cbor::semantic_tag::ta_float32_be, buffer);

   test_typed_array_tag<uint16_t>(glz::cbor::semantic_tag::ta_uint16_le, buffer);
   test_typed_array_tag<int32_t>(glz::cbor::semantic_tag::ta_sint32_le, buffer);
   test_typed_array_tag<float>(glz::cbor::semantic_tag::ta_float32_le, buffer);

   {
      std::string payload;
      append_cbor_header(payload, glz::cbor::major::tag, glz::cbor::semantic_tag::complex_array);
      append_cbor_header(payload, glz::cbor::major::tag, glz::cbor::semantic_tag::ta_float64_be);
      size_t nbytes = (buffer.size() / sizeof(double)) * sizeof(double);
      nbytes -= nbytes % (2 * sizeof(double));
      nbytes = std::min<size_t>(nbytes, 64);
      append_cbor_header(payload, glz::cbor::major::bstr, nbytes);
      payload.append(buffer.data(), nbytes);
      std::vector<std::complex<double>> v{};
      sink_ec(glz::read_cbor(v, payload));
      g_sink ^= v.size();
   }

   {

      std::string payload;
      append_cbor_header(payload, glz::cbor::major::array, 10);
      for (int i = 0; i < 10; ++i) {
         append_cbor_header(payload, glz::cbor::major::uint, static_cast<uint64_t>(i));
      }
      glz::inplace_vector<int32_t, 4> iv{};
      sink_ec(glz::read_cbor(iv, payload));
      g_sink ^= iv.size();
   }
   {

      std::string payload;
      payload.push_back(static_cast<char>((glz::cbor::major::array << 5) | glz::cbor::info::indefinite));
      for (int i = 0; i < 10; ++i) {
         append_cbor_header(payload, glz::cbor::major::uint, static_cast<uint64_t>(i));
      }
      payload.push_back(static_cast<char>(0xFF));
      glz::inplace_vector<int32_t, 4> iv2{};
      sink_ec(glz::read_cbor(iv2, payload));
      g_sink ^= iv2.size();
   }

   return 0;
}