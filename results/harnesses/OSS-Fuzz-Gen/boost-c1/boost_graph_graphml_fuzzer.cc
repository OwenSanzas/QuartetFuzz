#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphml.hpp>
#include <boost/property_map/dynamic_property_map.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/any.hpp>
#include <boost/shared_ptr.hpp>

#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

using namespace boost;

typedef property< vertex_color_t, int,
    property< vertex_name_t, std::string,
        property< vertex_distance_t, float,
            property< vertex_discover_time_t, long,
                property< vertex_root_t, bool > > > > >
    vertex_props_t;

typedef property< edge_weight_t, double,
    property< edge_weight2_t, float,
        property< edge_capacity_t, long, property< edge_flow_t, bool > > > >
    edge_props_t;

typedef property< graph_name_t, std::string > graph_props_t;

typedef adjacency_list< setS, vecS, directedS, vertex_props_t, edge_props_t,
    graph_props_t >
    directed_graph_t;
typedef adjacency_list< setS, vecS, undirectedS, vertex_props_t, edge_props_t,
    graph_props_t >
    undirected_graph_t;

namespace {

class generated_dummy_map : public dynamic_property_map
{
public:
    generated_dummy_map(const std::type_info& kt, const std::type_info& vt)
    : kt_(kt), vt_(vt)
    {
    }

    boost::any get(const boost::any&  ) BOOST_OVERRIDE
    {
        return last_value_;
    }

    std::string get_string(const boost::any&  ) BOOST_OVERRIDE
    {
        return std::string();
    }

    void put(const boost::any&  , const boost::any& value) BOOST_OVERRIDE
    {
        last_value_ = value;
    }

    const std::type_info& key() const BOOST_OVERRIDE { return kt_; }
    const std::type_info& value() const BOOST_OVERRIDE { return vt_; }

private:
    boost::any last_value_;
    const std::type_info& kt_;
    const std::type_info& vt_;
};

std::string pick_malformed_fragment(FuzzedDataProvider& fdp)
{
    static const char* kFragments[] = {
        "<data key=\"d0\" broken",
        "<data key=d0>5</data>",
        "<data key=\"d0\">5</data",
        "<![CDATA[unterminated",
        "&#65",
        "&#x41",
        "&#99999999999;",
        "<node id=\"n0\" id=\"n0\">",
        "<node id='n0\">",
        "<key id=\"d0\" for=\"node\" attr.name=\"x\" attr.type=\"int\""

    };
    return kFragments[fdp.ConsumeIntegralInRange< int >(0, 9)];
}

std::string make_string_value(FuzzedDataProvider& fdp)
{
    int choice = fdp.ConsumeIntegralInRange< int >(0, 6);
    if (choice == 0)
    {

        static const char* kEntities[] = { "&lt;", "&gt;", "&amp;", "&apos;",
            "&quot;", "&#65;", "&#x41;" };
        return std::string(
                   kEntities[fdp.ConsumeIntegralInRange< int >(0, 6)])
            + fdp.ConsumeRandomLengthString(8);
    }
    if (choice == 1)
    {

        return "&#"
            + std::to_string(
                  fdp.ConsumeIntegralInRange< long >(0, 99999999999L))
            + fdp.ConsumeRandomLengthString(4);
    }
    if (choice == 2)
    {

        return "&#x" + fdp.ConsumeRandomLengthString(4);
    }
    return fdp.ConsumeRandomLengthString(16);
}

std::string pick_type(FuzzedDataProvider& fdp)
{
    static const char* kTypes[] = { "boolean", "int", "long", "float",
        "double", "string", "unknown_type" };
    return kTypes[fdp.ConsumeIntegralInRange< int >(0, 6)];
}

std::string pick_valid_type(FuzzedDataProvider& fdp)
{

    static const char* kValidTypes[]
        = { "boolean", "int", "long", "float", "double", "string" };
    return kValidTypes[fdp.ConsumeIntegralInRange< int >(0, 5)];
}

std::string pick_for(FuzzedDataProvider& fdp)
{
    static const char* kFor[] = { "graph", "node", "edge", "hyperedge",
        "port", "endpoint", "all", "graphml", "bogus" };
    return kFor[fdp.ConsumeIntegralInRange< int >(0, 8)];
}

std::string pick_value_for_type(
    FuzzedDataProvider& fdp, const std::string& type)
{
    if (!fdp.ConsumeBool())
        return fdp.ConsumeRandomLengthString(8);
    if (type == "boolean")
        return fdp.ConsumeBool() ? "1" : "0";
    if (type == "int")
        return std::to_string(fdp.ConsumeIntegral< int >());
    if (type == "long")
        return std::to_string(fdp.ConsumeIntegral< long >());
    if (type == "float")
        return std::to_string(fdp.ConsumeFloatingPoint< float >());
    if (type == "double")
        return std::to_string(fdp.ConsumeFloatingPoint< double >());
    if (type == "string")
        return make_string_value(fdp);
    return make_string_value(fdp);
}

struct KeyDef
{
    const char* id;
    const char* name;
};

static const KeyDef kKeys[] = {
    { "d0", "foo" },
    { "d1", "weight" },
    { "d2", "name" },
    { "d3", "description" },
    { "d4", "vfloat" },
    { "d5", "vlong" },
    { "d6", "vbool" },
    { "d7", "efloat" },
    { "d8", "elong" },
    { "d9", "ebool" },
    { "d10", "gbool" },
    { "d11", "glong" },
    { "d12", "gfloat" },
};
static const int kNumKeys = 13;

static const KeyDef kBogusKey = { "dbogus", "totally_unregistered_name" };

static const char* kNaturalType[kNumKeys] = { "int", "double", "string",
    "string", "float", "long", "boolean", "float", "long", "boolean",
    "boolean", "long", "float" };
static const char* kNaturalFor[kNumKeys] = { "node", "edge", "node", "graph",
    "node", "node", "node", "edge", "edge", "edge", "graph", "graph",
    "graph" };

static const int kVertexKeyIdx[] = { 0, 2, 4, 5, 6 };
static const int kNumVertexKeys = 5;
static const int kEdgeKeyIdx[] = { 1, 7, 8, 9 };
static const int kNumEdgeKeys = 4;
static const int kGraphKeyIdx[] = { 3, 10, 11, 12 };
static const int kNumGraphKeys = 4;

std::string build_structured_graphml(FuzzedDataProvider& fdp)
{
    std::ostringstream xml;
    xml << "<graphml>\n";

    if (fdp.ConsumeIntegralInRange< int >(0, 4) == 0)
        xml << "<!-- " << fdp.ConsumeRandomLengthString(16) << " -->\n";

    std::string key_type[kNumKeys];
    bool key_defined[kNumKeys];
    for (int i = 0; i < kNumKeys; ++i)
        key_defined[i] = false;

    for (int i = 0; i < kNumKeys; ++i)
    {
        if (!fdp.ConsumeBool())
            continue;

        std::string type = (fdp.ConsumeIntegralInRange< int >(0, 9) < 8)
            ? kNaturalType[i]
            : pick_type(fdp);
        std::string for_ = (fdp.ConsumeIntegralInRange< int >(0, 9) < 7)
            ? kNaturalFor[i]
            : pick_for(fdp);

        key_type[i] = type;
        key_defined[i] = true;
        xml << "  <key id=\"" << kKeys[i].id << "\" for=\"" << for_
            << "\" attr.name=\"" << kKeys[i].name << "\" attr.type=\""
            << type << "\">\n";
        if (fdp.ConsumeBool())
            xml << "    <default>" << pick_value_for_type(fdp, type)
                << "</default>\n";
        xml << "  </key>\n";
    }

    std::string bogus_type = pick_valid_type(fdp);
    xml << "  <key id=\"" << kBogusKey.id << "\" for=\"" << pick_for(fdp)
        << "\" attr.name=\"" << kBogusKey.name << "\" attr.type=\""
        << bogus_type << "\">\n  </key>\n";

    auto emit_data
        = [&](std::ostringstream& out, int idx, const char* fallback_type)
    {
        bool use_defined = key_defined[idx]
            && fdp.ConsumeIntegralInRange< int >(0, 9) != 0;
        std::string type = use_defined ? key_type[idx] : fallback_type;

        std::string value;
        bool inject_malformed = fdp.ConsumeIntegralInRange< int >(0, 14) == 0;
        if (inject_malformed)
            value = pick_malformed_fragment(fdp);
        else
            value = pick_value_for_type(fdp, type);

        bool omit_key_attr = fdp.ConsumeIntegralInRange< int >(0, 19) == 0;
        bool use_cdata = fdp.ConsumeIntegralInRange< int >(0, 9) == 0;
        out << "      <data";
        if (!omit_key_attr)
            out << " key=\"" << kKeys[idx].id << "\"";
        out << ">";
        if (use_cdata)
            out << "<![CDATA[" << value << "]]>";
        else
            out << value;
        out << "</data>\n";
    };

    auto maybe_emit_malformed_fragment = [&](std::ostringstream& out)
    {
        if (fdp.ConsumeIntegralInRange< int >(0, 11) == 0)
            out << "      " << pick_malformed_fragment(fdp) << "\n";
    };

    auto emit_bogus_data = [&](std::ostringstream& out)
    {
        out << "      <data key=\"" << kBogusKey.id << "\">"
            << pick_value_for_type(fdp, bogus_type) << "</data>\n";
    };

    auto emit_cross_category
        = [&](std::ostringstream& out, const int* pool, int pool_size)
    {
        if (pool_size <= 0)
            return;
        int idx = pool[fdp.ConsumeIntegralInRange< int >(0, pool_size - 1)];
        if (key_defined[idx])
            emit_data(out, idx, kNaturalType[idx]);
    };

    int num_graphs = fdp.ConsumeIntegralInRange< int >(1, 3);
    for (int gi = 0; gi < num_graphs; ++gi)
    {
        bool directed = fdp.ConsumeBool();

        if (fdp.ConsumeIntegralInRange< int >(0, 9) == 0)
            xml << "  <!-- " << fdp.ConsumeRandomLengthString(8) << " -->\n";

        bool omit_edgedefault = fdp.ConsumeIntegralInRange< int >(0, 19) == 0;
        xml << "  <graph id=\"G" << gi << "\"";
        if (!omit_edgedefault)
            xml << " edgedefault=\"" << (directed ? "directed" : "undirected")
                << "\"";
        xml << ">\n";

        maybe_emit_malformed_fragment(xml);

        for (int k = 0; k < kNumGraphKeys; ++k)
        {
            int idx = kGraphKeyIdx[k];
            if (key_defined[idx] || fdp.ConsumeIntegralInRange< int >(0, 9) == 0)
                emit_data(xml, idx, kNaturalType[idx]);
        }
        if (fdp.ConsumeIntegralInRange< int >(0, 7) == 0)
            emit_bogus_data(xml);
        if (fdp.ConsumeIntegralInRange< int >(0, 7) == 0)
        {
            static const int kOtherForGraph[]
                = { kVertexKeyIdx[0], kVertexKeyIdx[1], kVertexKeyIdx[2],
                      kVertexKeyIdx[3], kVertexKeyIdx[4], kEdgeKeyIdx[0],
                      kEdgeKeyIdx[1], kEdgeKeyIdx[2], kEdgeKeyIdx[3] };
            emit_cross_category(xml, kOtherForGraph, 9);
        }

        int num_nodes = fdp.ConsumeIntegralInRange< int >(0, 6);
        for (int n = 0; n < num_nodes; ++n)
        {

            bool omit_id = fdp.ConsumeIntegralInRange< int >(0, 19) == 0;
            if (omit_id)
                xml << "    <node>\n";
            else
                xml << "    <node id=\"n" << n << "\">\n";
            for (int k = 0; k < kNumVertexKeys; ++k)
            {
                int idx = kVertexKeyIdx[k];
                if (fdp.ConsumeBool())
                    emit_data(xml, idx, kNaturalType[idx]);
            }
            if (fdp.ConsumeIntegralInRange< int >(0, 7) == 0)
                emit_bogus_data(xml);
            if (fdp.ConsumeIntegralInRange< int >(0, 7) == 0)
            {
                static const int kOtherForNode[]
                    = { kEdgeKeyIdx[0], kEdgeKeyIdx[1], kEdgeKeyIdx[2],
                          kEdgeKeyIdx[3], kGraphKeyIdx[0], kGraphKeyIdx[1],
                          kGraphKeyIdx[2], kGraphKeyIdx[3] };
                emit_cross_category(xml, kOtherForNode, 8);
            }
            maybe_emit_malformed_fragment(xml);
            xml << "    </node>\n";
        }

        if (num_nodes > 0 && fdp.ConsumeIntegralInRange< int >(0, 4) == 0)
        {
            int dup = fdp.ConsumeIntegralInRange< int >(0, num_nodes - 1);
            xml << "    <node id=\"n" << dup << "\">\n";
            for (int k = 0; k < kNumVertexKeys; ++k)
            {
                int idx = kVertexKeyIdx[k];
                if (fdp.ConsumeBool())
                    emit_data(xml, idx, kNaturalType[idx]);
            }
            xml << "    </node>\n";
        }

        int num_edges = fdp.ConsumeIntegralInRange< int >(0, 6);
        int hi = num_nodes == 0 ? 0 : num_nodes - 1;
        for (int e = 0; e < num_edges; ++e)
        {
            int src = fdp.ConsumeIntegralInRange< int >(0, hi);
            int dst = fdp.ConsumeIntegralInRange< int >(0, hi);
            bool omit_source = fdp.ConsumeIntegralInRange< int >(0, 19) == 0;
            bool omit_target = fdp.ConsumeIntegralInRange< int >(0, 19) == 0;
            xml << "    <edge id=\"e" << e << "\"";
            if (!omit_source)
                xml << " source=\"n" << src << "\"";
            if (!omit_target)
                xml << " target=\"n" << dst << "\"";
            if (fdp.ConsumeBool())
                xml << " directed=\""
                    << (fdp.ConsumeBool() ? "true" : "false") << "\"";
            xml << ">\n";
            for (int k = 0; k < kNumEdgeKeys; ++k)
            {
                int idx = kEdgeKeyIdx[k];
                if (fdp.ConsumeBool())
                    emit_data(xml, idx, kNaturalType[idx]);
            }
            if (fdp.ConsumeIntegralInRange< int >(0, 7) == 0)
                emit_bogus_data(xml);
            if (fdp.ConsumeIntegralInRange< int >(0, 7) == 0)
            {
                static const int kOtherForEdge[]
                    = { kVertexKeyIdx[0], kVertexKeyIdx[1], kVertexKeyIdx[2],
                          kVertexKeyIdx[3], kVertexKeyIdx[4], kGraphKeyIdx[0],
                          kGraphKeyIdx[1], kGraphKeyIdx[2], kGraphKeyIdx[3] };
                emit_cross_category(xml, kOtherForEdge, 9);
            }
            maybe_emit_malformed_fragment(xml);
            xml << "    </edge>\n";
        }

        if (fdp.ConsumeIntegralInRange< int >(0, 3) == 0)
        {
            bool omit_nested_edgedefault
                = fdp.ConsumeIntegralInRange< int >(0, 9) == 0;
            xml << "    <graph id=\"G" << gi << "N\"";
            if (!omit_nested_edgedefault)
                xml << " edgedefault=\""
                    << (fdp.ConsumeBool() ? "directed" : "undirected")
                    << "\"";
            xml << ">\n"
                << "      <node id=\"n" << num_nodes << "\"/>\n"
                << "    </graph>\n";
        }

        xml << "  </graph>\n";
    }
    xml << "</graphml>\n";
    return xml.str();
}

template < typename GraphT >
std::string build_baseline_graphml(FuzzedDataProvider& fdp)
{
    GraphT seed_g;
    typedef typename graph_traits< GraphT >::vertex_descriptor vdesc_t;

    int n = fdp.ConsumeIntegralInRange< int >(1, 5);
    std::vector< vdesc_t > verts;
    verts.reserve(n);
    for (int i = 0; i < n; ++i)
        verts.push_back(add_vertex(seed_g));

    int e = fdp.ConsumeIntegralInRange< int >(0, 5);
    for (int i = 0; i < e && n > 0; ++i)
    {
        int s = fdp.ConsumeIntegralInRange< int >(0, n - 1);
        int t = fdp.ConsumeIntegralInRange< int >(0, n - 1);
        add_edge(verts[s], verts[t], seed_g);
    }

    auto vcolor = get(vertex_color_t(), seed_g);
    auto vname = get(vertex_name_t(), seed_g);
    for (auto v : verts)
    {
        put(vcolor, v, fdp.ConsumeIntegral< int >());
        put(vname, v, fdp.ConsumeRandomLengthString(8));
    }
    auto eweight = get(edge_weight_t(), seed_g);
    typename graph_traits< GraphT >::edge_iterator ei, ei_end;
    for (boost::tie(ei, ei_end) = edges(seed_g); ei != ei_end; ++ei)
        put(eweight, *ei, fdp.ConsumeFloatingPoint< double >());

    get_property(seed_g, graph_name) = fdp.ConsumeRandomLengthString(8);

    dynamic_properties dp_write;
    dp_write.property("foo", get(vertex_color_t(), seed_g));
    dp_write.property("name", get(vertex_name_t(), seed_g));
    dp_write.property("weight", get(edge_weight_t(), seed_g));
    boost::ref_property_map< GraphT*, std::string > gname_w(
        get_property(seed_g, graph_name));
    dp_write.property("description", gname_w);

    std::ostringstream oss;
    write_graphml(oss, seed_g, dp_write, fdp.ConsumeBool());
    return oss.str();
}

template < typename GraphT >
void run_read_graphml(FuzzedDataProvider& fdp, int build_mode,
    size_t desired_idx, bool use_generate_fn)
{
    try
    {
        GraphT g;

        dynamic_properties::generate_fn_type gen_fn
            = [](const std::string&  , const boost::any& key,
                   const boost::any& value)
            -> boost::shared_ptr< dynamic_property_map >
        {
            return boost::shared_ptr< dynamic_property_map >(
                new generated_dummy_map(key.type(), value.type()));
        };

        dynamic_properties dp
            = use_generate_fn ? dynamic_properties(gen_fn) : dynamic_properties();

        dp.property("foo", get(vertex_color_t(), g));
        dp.property("name", get(vertex_name_t(), g));
        dp.property("vfloat", get(vertex_distance_t(), g));
        dp.property("vlong", get(vertex_discover_time_t(), g));
        dp.property("vbool", get(vertex_root_t(), g));

        dp.property("weight", get(edge_weight_t(), g));
        dp.property("efloat", get(edge_weight2_t(), g));
        dp.property("elong", get(edge_capacity_t(), g));
        dp.property("ebool", get(edge_flow_t(), g));

        boost::ref_property_map< GraphT*, std::string > gname(
            get_property(g, graph_name));
        dp.property("description", gname);

        bool g_bool_val = false;
        long g_long_val = 0;
        float g_float_val = 0.f;
        boost::ref_property_map< GraphT*, bool > gboolmap(g_bool_val);
        boost::ref_property_map< GraphT*, long > glongmap(g_long_val);
        boost::ref_property_map< GraphT*, float > gfloatmap(g_float_val);
        dp.property("gbool", gboolmap);
        dp.property("glong", glongmap);
        dp.property("gfloat", gfloatmap);

        std::string xml_content;
        if (build_mode == 0)
        {

            xml_content = fdp.ConsumeRemainingBytesAsString();
        }
        else if (build_mode <= 7)
        {

            xml_content = build_structured_graphml(fdp);
        }
        else
        {

            xml_content = build_baseline_graphml< GraphT >(fdp);
            if (!xml_content.empty())
            {
                size_t n_mutations = fdp.ConsumeIntegralInRange< size_t >(0, 5);
                for (size_t i = 0; i < n_mutations && !xml_content.empty(); ++i)
                {

                    bool target_syntax = fdp.ConsumeBool();
                    size_t pos = 0;
                    bool found_syntax_pos = false;
                    if (target_syntax)
                    {
                        std::vector< size_t > syntax_positions;
                        for (size_t j = 0; j < xml_content.size(); ++j)
                        {
                            if (std::strchr("<>\"'=&;/", xml_content[j])
                                != nullptr)
                                syntax_positions.push_back(j);
                        }
                        if (!syntax_positions.empty())
                        {
                            pos = syntax_positions[fdp.ConsumeIntegralInRange<
                                size_t >(0, syntax_positions.size() - 1)];
                            found_syntax_pos = true;
                        }
                    }
                    if (!found_syntax_pos)
                        pos = fdp.ConsumeIntegralInRange< size_t >(
                            0, xml_content.size() - 1);

                    xml_content[pos] = (char)fdp.ConsumeIntegral< uint8_t >();
                }
            }
        }

        std::istringstream iss(xml_content);

        read_graphml(iss, g, dp, desired_idx);
    }
    catch (const std::exception&)
    {

    }
    catch (...)
    {

    }
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);

    int build_mode = fdp.ConsumeIntegralInRange< int >(0, 9);

    bool use_undirected = fdp.ConsumeBool();

    size_t desired_idx = fdp.ConsumeIntegralInRange< uint32_t >(0, 3);
    if (fdp.ConsumeBool())
        desired_idx = (size_t)-1;

    bool use_generate_fn = fdp.ConsumeBool();

    if (use_undirected)
        run_read_graphml< undirected_graph_t >(
            fdp, build_mode, desired_idx, use_generate_fn);
    else
        run_read_graphml< directed_graph_t >(
            fdp, build_mode, desired_idx, use_generate_fn);

    return 0;
}