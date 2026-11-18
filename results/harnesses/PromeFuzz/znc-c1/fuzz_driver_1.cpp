#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <Message.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {
        return 0;
    }

    std::string sInput(reinterpret_cast<const char*>(Data), Size);
    CString sMessage(sInput);
    CString sTaggedMessage = "@" + sMessage;
    CString sPrefixedMessage = ":" + sMessage;
    CString sFullMessage = "@tag=value :prefix " + sMessage;
    CString sTrailingMessage = sMessage + " :trailing parameter here";

    {
        CMessage message;
        try {
            message.Parse(sMessage);
        } catch (...) {

        }
    }

    {
        CMessage taggedMsg;
        try {
            taggedMsg.Parse(sTaggedMessage);
        } catch (...) {
        }
    }

    {
        CMessage prefixedMsg;
        try {
            prefixedMsg.Parse(sPrefixedMessage);
        } catch (...) {
        }
    }

    {
        CMessage fullMsg;
        try {
            fullMsg.Parse(sFullMessage);
        } catch (...) {
        }
    }

    {
        CMessage emptyMsg;
        try {
            emptyMsg.Parse("");
        } catch (...) {
        }
    }

    {
        CMessage trailingMsg;
        try {
            trailingMsg.Parse(sTrailingMessage);
        } catch (...) {
        }
    }

    {
        CMessage reuseMsg;
        try {
            reuseMsg.Parse(sMessage);
            reuseMsg.Parse(sTaggedMessage);
        } catch (...) {
        }
    }

    {
        FILE* fp = fopen("./dummy_file", "wb");
        if (fp) {
            fwrite(Data, 1, Size, fp);
            fclose(fp);
        }
    }

    return 0;
}