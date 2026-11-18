#include <stddef.h>
#include <stdint.h>
#include <string>

#include <znc/Message.h>
#include <znc/ZNCString.h>
#include <znc/Nick.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    CString sLine(reinterpret_cast<const char*>(data), size);

    CMessage Message(sLine);

    volatile CMessage::Type eType = Message.GetType();
    (void)eType;

    CString sCommand = Message.GetCommand();
    (void)sCommand;

    const VCString& vParams = Message.GetParams();
    for (unsigned int i = 0; i < vParams.size() + 2; ++i) {
        CString sParam = Message.GetParam(i);
        (void)sParam;
    }

    CString sOOB = Message.GetParam(0xFFFFFFFFu);
    (void)sOOB;

    unsigned int uIdx = size > 0 ? data[0] % 8 : 0;
    unsigned int uLen = size > 1 ? data[1] % 8 : (unsigned int)-1;
    CString sColon = Message.GetParamsColon(uIdx, uLen);
    (void)sColon;
    CString sColonAll = Message.GetParamsColon(0);
    (void)sColonAll;

    CString sTimeTag = Message.GetTag("time");
    (void)sTimeTag;
    CString sFuzzTag = Message.GetTag(sCommand);
    (void)sFuzzTag;

    for (const auto& kv : Message.GetTags()) {
        CString sKey = kv.first;
        CString sVal = kv.second;
        (void)sKey;
        (void)sVal;
    }

    const CNick& Nick = Message.GetNick();
    CString sNick = Nick.GetNick();
    CString sIdent = Nick.GetIdent();
    CString sHost = Nick.GetHost();
    CString sHostMask = Nick.GetHostMask();
    (void)sNick;
    (void)sIdent;
    (void)sHost;
    (void)sHostMask;

    const timeval& ts = Message.GetTime();
    (void)ts;

    for (unsigned int uFlags = 0; uFlags <= (CMessage::ExcludePrefix |
                                              CMessage::ExcludeTags);
         ++uFlags) {
        CString sSerialized = Message.ToString(uFlags);
        CMessage Reparsed(sSerialized);
        (void)Reparsed.GetType();
        (void)Reparsed.GetCommand();
        volatile bool bEqual = Message.Equals(Reparsed);
        (void)bEqual;
    }

    CMessage Reused;
    Reused.Parse(sLine);
    (void)Reused.GetType();

    CMessage Cloned;
    Cloned.Clone(Reused);
    (void)Cloned.GetCommand();

    return 0;
}
