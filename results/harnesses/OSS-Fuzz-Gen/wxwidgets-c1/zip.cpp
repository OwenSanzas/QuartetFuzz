#include "wx/log.h"
#include "wx/mstream.h"
#include "wx/zipstrm.h"

#include <vector>

#if wxDEBUG_LEVEL

static void exitAssertHandler(const wxString& file,
                              int line,
                              const wxString& func,
                              const wxString& cond,
                              const wxString& msg);

static volatile wxAssertHandler_t
    origAssertHandler = wxSetAssertHandler(exitAssertHandler);

static void exitAssertHandler(const wxString& file,
                              int line,
                              const wxString& func,
                              const wxString& cond,
                              const wxString& msg)
{
    origAssertHandler(file, line, func, cond, msg);

    exit(1);
}

#endif

class NonSeekableWrapperStream : public wxFilterInputStream
{
public:
    explicit NonSeekableWrapperStream(wxInputStream& stream)
        : wxFilterInputStream(stream)
    {
    }

    virtual bool IsSeekable() const override { return false; }

protected:
    virtual size_t OnSysRead(void *buffer, size_t size) override
    {
        m_parent_i_stream->Read(buffer, size);
        size_t count = m_parent_i_stream->LastRead();
        if (count == 0)
            m_lasterror = m_parent_i_stream->Eof() ? wxSTREAM_EOF
                                                    : wxSTREAM_READ_ERROR;
        return count;
    }
};

static void ExerciseZipStream(wxInputStream& stream)
{
    wxZipInputStream zis(stream);

    wxMemoryOutputStream dummyOutBuf;
    wxZipOutputStream dummyZos(dummyOutBuf);
    dummyZos.CopyArchiveMetaData(zis);

    zis.GetTotalEntries();

    std::vector<wxZipEntry*> entries;

    while ( wxZipEntry* const ze = zis.GetNextEntry() ) {
        entries.push_back(ze);

        if ( zis.OpenEntry(*ze) ) {

            char buf[4096];
            while ( zis.IsOk() && !zis.Eof() ) {
                zis.Read(buf, sizeof(buf));
                if ( zis.LastRead() == 0 )
                    break;
            }

            if ( !zis.CloseEntry() ) {
                break;
            }
        }
    }

    zis.GetComment();

    for ( wxZipEntry* ze : entries )
        delete ze;

    dummyZos.Close();
}

static void ExerciseValidZip(const uint8_t *data, size_t size)
{
    if (size < 2)
        return;

    wxMemoryOutputStream outBuf;
    {
        wxZipOutputStream zos(outBuf);

        bool store = (data[0] & 1) != 0;
        size_t nameLen = data[0] % 32;
        if (nameLen > size - 1)
            nameLen = size - 1;
        wxString name = wxString::FromUTF8(
            reinterpret_cast<const char*>(data) + 1, nameLen);
        if (name.empty())
            name = "f";

        wxZipEntry *entry = new wxZipEntry(name);
        if (store)
            entry->SetMethod(wxZIP_METHOD_STORE);
        else
            entry->SetMethod(wxZIP_METHOD_DEFLATE);

        if (zos.PutNextEntry(entry)) {
            const uint8_t *payload = data + 1 + nameLen;
            size_t payloadLen = size - 1 - nameLen;
            if (payloadLen)
                zos.Write(payload, payloadLen);
        }
        zos.SetComment("fuzz");
        zos.Close();
    }

    {
        wxMemoryInputStream mis(outBuf);
        ExerciseZipStream(mis);
    }
    {
        wxMemoryInputStream mis(outBuf);
        NonSeekableWrapperStream nss(mis);
        ExerciseZipStream(nss);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    wxLogNull noLog;

    {
        wxMemoryInputStream mis(data, size);
        ExerciseZipStream(mis);
    }

    {
        wxMemoryInputStream mis(data, size);
        NonSeekableWrapperStream nss(mis);
        ExerciseZipStream(nss);
    }

    ExerciseValidZip(data, size);

    return 0;
}