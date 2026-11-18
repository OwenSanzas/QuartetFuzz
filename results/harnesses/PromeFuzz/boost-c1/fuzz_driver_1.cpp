#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <boost/graph/graphml.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/properties.hpp>

#include <sstream>
#include <fstream>
#include <string>
#include <cstdint>
#include <cstddef>
#include <exception>

typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
    boost::property<boost::vertex_name_t, std::string>,
    boost::property<boost::edge_weight_t, double> > FuzzGraph;

static void run_template_overload(const std::string& input, size_t idx)
{
    FuzzGraph g;
    boost::dynamic_properties dp;
    dp.property("name", boost::get(boost::vertex_name, g));
    dp.property("weight", boost::get(boost::edge_weight, g));

    std::istringstream iss(input);
    try
    {
        boost::read_graphml(iss, g, dp, idx);
    }
    catch (const std::exception&)
    {

    }
    catch (...)
    {

    }
}

static void run_mutate_graph_overload(const std::string& input, size_t idx)
{
    FuzzGraph g;
    boost::dynamic_properties dp;
    dp.property("name", boost::get(boost::vertex_name, g));
    dp.property("weight", boost::get(boost::edge_weight, g));

    boost::mutate_graph_impl<FuzzGraph> mg(g, dp);

    std::istringstream iss(input);
    try
    {
        boost::read_graphml(iss, mg, idx);
    }
    catch (const std::exception&)
    {

    }
    catch (...)
    {

    }
}

static void run_file_based_test(const uint8_t* Data, size_t Size)
{
    FuzzGraph g;
    boost::dynamic_properties dp;
    dp.property("name", boost::get(boost::vertex_name, g));
    dp.property("weight", boost::get(boost::edge_weight, g));

    std::ofstream ofs("./dummy_file", std::ios::binary | std::ios::trunc);
    if (ofs)
    {
        ofs.write(reinterpret_cast<const char*>(Data), static_cast<std::streamsize>(Size));
        ofs.close();
    }

    std::ifstream ifs("./dummy_file", std::ios::binary);
    if (ifs)
    {
        size_t idx = (Size > 0) ? static_cast<size_t>(Data[0] % 3) : 0;
        try
        {
            boost::read_graphml(ifs, g, dp, idx);
        }
        catch (const std::exception&)
        {
        }
        catch (...)
        {
        }
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    if (Size == 0)
    {
        return 0;
    }

    std::string input(reinterpret_cast<const char*>(Data), Size);

    size_t idx = (Size > 0) ? static_cast<size_t>(Data[0] % 3) : 0;

    run_template_overload(input, idx);

    run_mutate_graph_overload(input, idx);

    run_file_based_test(Data, Size);

    return 0;
}