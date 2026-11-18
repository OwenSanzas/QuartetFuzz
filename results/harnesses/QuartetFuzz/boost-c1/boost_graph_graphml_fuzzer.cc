#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphml.hpp>
#include <boost/graph/exception.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <cstdint>
#include <sstream>
#include <string>

using namespace boost;

typedef property< vertex_color_t, int, property< vertex_name_t, std::string > >
    vertex_prop_t;
typedef property< edge_weight_t, double > edge_prop_t;
typedef property< graph_name_t, std::string > graph_prop_t;

typedef adjacency_list< vecS, vecS, directedS, vertex_prop_t, edge_prop_t,
    graph_prop_t >
    graph_dir_t;

typedef adjacency_list< setS, vecS, undirectedS, vertex_prop_t, edge_prop_t,
    graph_prop_t >
    graph_undir_t;

template < typename G >
static void register_properties(G& g, dynamic_properties& dp)
{
    dp.property("foo", get(vertex_color_t(), g));
    dp.property("weight", get(edge_weight_t(), g));
    dp.property("name", get(vertex_name_t(), g));
    boost::ref_property_map< G*, std::string > gname_local(
        get_property(g, graph_name));
    dp.property("description", gname_local);
}

template < typename G >
static void exercise_result(G& g, dynamic_properties& dp, bool ordered_vertices)
{

    std::size_t nv = num_vertices(g);
    std::size_t ne = num_edges(g);
    (void) nv;
    (void) ne;

    typename graph_traits< G >::vertex_iterator vi, vend;
    for (boost::tie(vi, vend) = vertices(g); vi != vend; ++vi)
    {
        volatile int c = get(vertex_color_t(), g, *vi);
        (void) c;
        const std::string& nm = get(vertex_name_t(), g, *vi);
        (void) nm;
    }
    typename graph_traits< G >::edge_iterator ei, eend;
    for (boost::tie(ei, eend) = edges(g); ei != eend; ++ei)
    {
        volatile double w = get(edge_weight_t(), g, *ei);
        (void) w;
    }

    if (nv > 0)
    {
        typename graph_traits< G >::vertex_descriptor v0 = vertex(0, g);
        (void) v0;
    }
    try
    {
        std::string desc = get("description", dp, &g);
        (void) desc;
    }
    catch (const std::exception&)
    {
    }

    std::ostringstream oss;
    try
    {
        write_graphml(oss, g, dp, ordered_vertices);
    }
    catch (const std::exception&)
    {
        return;
    }

    G g2;
    dynamic_properties dp2;
    register_properties(g2, dp2);
    std::istringstream iss(oss.str());
    try
    {
        read_graphml(iss, g2, dp2);
    }
    catch (const std::exception&)
    {
    }
}

template < typename G >
static void run_one(const uint8_t* body, size_t body_size, size_t desired_idx,
    bool ordered_vertices)
{
    G g;
    dynamic_properties dp;
    register_properties(g, dp);

    std::string xml(reinterpret_cast< const char* >(body), body_size);
    std::istringstream in(xml);

    try
    {
        read_graphml(in, g, dp, desired_idx);
    }
    catch (const std::exception&)
    {

        return;
    }

    exercise_result(g, dp, ordered_vertices);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;

    uint8_t selector = data[0];
    const uint8_t* body = data + 1;
    size_t body_size = size - 1;

    size_t desired_idx = 0;
    switch (selector % 3)
    {
    case 0:
        desired_idx = 0;
        break;
    case 1:
        desired_idx = 1;
        break;
    case 2:
        desired_idx = static_cast< size_t >(-1);
        break;
    }

    bool use_undirected = (selector & 0x04) != 0;

    bool ordered_vertices = (selector & 0x08) != 0;

    if (use_undirected)
        run_one< graph_undir_t >(body, body_size, desired_idx, ordered_vertices);
    else
        run_one< graph_dir_t >(body, body_size, desired_idx, ordered_vertices);

    return 0;
}
