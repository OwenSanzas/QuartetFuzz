#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "muParser.h"

using namespace mu;

namespace {

value_type MyFun1(value_type v) { return v * 2.0; }
value_type MyFun2(value_type v1, value_type v2) { return v1 + v2; }
value_type MyInfixFun(value_type v) { return -v; }
value_type MyPostfixFun(value_type v) { return v + 1.0; }
value_type MyOprtFun(value_type v1, value_type v2) { return v1 * v2 + 1.0; }

value_type* MyVarFactory(const char_type*  , void*  )
{
    static value_type s_autoVar[16];
    return s_autoVar;
}

int MyIdentFun(const char_type* sExpr, int* pPos, value_type* pVal)
{
    if (sExpr != nullptr && strncmp(sExpr, "zz", 2) == 0)
    {
        *pVal = 42.0;
        *pPos += 2;
        return 1;
    }
    return 0;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 8)
        return 0;

    uint8_t config = data[0];
    uint8_t config2 = data[1];
    char decSep = (char)(data[2] ? data[2] : '.');
    char argSep = (char)(data[3] ? data[3] : ',');

    size_t off = 4;
    size_t nameCharsLen = (size - off) / 4;
    std::string nameChars(reinterpret_cast<const char*>(data + off), nameCharsLen);
    off += nameCharsLen;

    size_t exprLen = size - off;
    std::string expr(reinterpret_cast<const char*>(data + off), exprLen);
    if (expr.size() > 4000)
        expr.resize(4000);

    const int kBulkSize = 8;
    value_type xVar[kBulkSize];
    value_type yVar[kBulkSize];
    value_type zVar[kBulkSize];
    for (int i = 0; i < kBulkSize; ++i)
    {
        xVar[i] = (value_type)i;
        yVar[i] = (value_type)i * 0.5;
        zVar[i] = (value_type)i * 2.0 + 1.0;
    }

    Parser parser;

    char sink[256];
    size_t sinkLen = 0;
    sink[0] = '\0';

    try
    {
        if (config & 0x01) parser.SetDecSep(decSep);
        if (config & 0x02) parser.SetArgSep(argSep);
        if (config & 0x04) parser.SetThousandsSep((char_type)data[2]);

        parser.DefineVar("x", xVar);
        parser.DefineVar("y", yVar);
        parser.DefineVar("z", zVar);

        parser.DefineConst("k1", 3.14159);
        parser.DefineStrConst("s1", "hello");

        parser.DefineFun("f1", MyFun1);
        parser.DefineFun("f2", MyFun2);

        if (config & 0x08) parser.DefineInfixOprt("!", MyInfixFun);
        if (config & 0x10) parser.DefinePostfixOprt("M", MyPostfixFun);
        if (config & 0x20)
            parser.DefineOprt("shr", MyOprtFun, 0,
                               (config & 0x01) ? oaLEFT : oaRIGHT);
        if (config & 0x40) parser.SetVarFactory(MyVarFactory, nullptr);
        if (config & 0x80) parser.AddValIdent(MyIdentFun);

        if (!nameChars.empty())
        {
            if (config2 & 0x01) parser.DefineNameChars(nameChars.c_str());
            if (config2 & 0x02) parser.DefineOprtChars(nameChars.c_str());
            if (config2 & 0x04) parser.DefineInfixOprtChars(nameChars.c_str());
        }

        if (config2 & 0x08) parser.EnableOptimizer(false);
        if (config2 & 0x10) parser.EnableBuiltInOprt(false);

        parser.SetExpr(expr);

        const varmap_type& used = parser.GetUsedVar();
        for (varmap_type::const_iterator it = used.begin();
             it != used.end() && sinkLen + 1 < sizeof(sink); ++it)
        {
            sink[sinkLen++] = it->first.empty() ? '?' : it->first[0];
        }

        value_type scalar = parser.Eval();
        int n = snprintf(sink + sinkLen, sizeof(sink) - sinkLen, "%g", scalar);
        if (n > 0) sinkLen += (size_t)n < sizeof(sink) - sinkLen ? (size_t)n : 0;

        int nNum = parser.GetNumResults();
        if (nNum > 1)
        {
            int nStackSize = 0;
            value_type* v = parser.Eval(nStackSize);
            for (int i = 0; i < nNum && i < nStackSize &&
                            sinkLen + 1 < sizeof(sink); ++i)
            {
                int m = snprintf(sink + sinkLen, sizeof(sink) - sinkLen, "%g;", v[i]);
                if (m > 0)
                    sinkLen += (size_t)m < sizeof(sink) - sinkLen ? (size_t)m : 0;
            }
        }

        if (config2 & 0x20)
        {
            value_type results[kBulkSize];
            parser.Eval(results, kBulkSize);
            for (int i = 0; i < kBulkSize && sinkLen + 1 < sizeof(sink); ++i)
            {
                int m = snprintf(sink + sinkLen, sizeof(sink) - sinkLen, "%g,", results[i]);
                if (m > 0)
                    sinkLen += (size_t)m < sizeof(sink) - sinkLen ? (size_t)m : 0;
            }
        }
    }
    catch (Parser::exception_type& e)
    {

        int code = (int)e.GetCode();
        int pos = e.GetPos();
        const std::string& msg = e.GetMsg();
        const std::string& tok = e.GetToken();
        const std::string& exprStr = e.GetExpr();
        int n = snprintf(sink, sizeof(sink), "err:%d:%d:%zu:%zu:%zu", code, pos,
                          msg.size(), tok.size(), exprStr.size());
        if (n > 0) sinkLen = (size_t)n < sizeof(sink) ? (size_t)n : sizeof(sink) - 1;
    }
    catch (...)
    {
    }

    if (sinkLen == (size_t)-1)
        return 1;

    return 0;
}
