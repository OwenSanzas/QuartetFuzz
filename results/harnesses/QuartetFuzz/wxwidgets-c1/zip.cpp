#include "wx/log.h"
#include "wx/mstream.h"
#include "wx/zipstrm.h"

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
    explicit NonSeekableWrapperStream(wxInputStream& parent)
        : wxFilterInputStream(parent)
    {
    }

    bool IsSeekable() const override { return false; }

protected:
    size_t OnSysRead(void *buffer, size_t size) override
    {
        m_parent_i_stream->Read(buffer, size);
        size_t n = m_parent_i_stream->LastRead();
        if (n == 0)
            m_lasterror = m_parent_i_stream->GetLastError();
        return n;
    }

    wxDECLARE_NO_COPY_CLASS(NonSeekableWrapperStream);
};

static volatile wxUint64 g_sink = 0;

static void ExerciseZip(wxInputStream& stream, bool seekable)
{
    wxZipInputStream zis(stream);

    g_sink += (wxUint64)zis.GetTotalEntries();

    int iterations = 0;
    while ( wxZipEntry* const ze = zis.GetNextEntry() ) {

        g_sink += (wxUint64)ze->GetName().length();
        g_sink += (wxUint64)ze->GetSize();
        g_sink += (wxUint64)ze->GetDateTime().GetTicks();
        g_sink += ze->IsDir() ? 1 : 0;

        bool opened = true;
        if ( seekable )
            opened = zis.OpenEntry(*ze);

        if ( opened ) {

            char buf[4096];
            wxFileOffset totalRead = 0;
            while ( zis.IsOk() && !zis.Eof() ) {
                zis.Read(buf, sizeof(buf));
                size_t n = zis.LastRead();
                if ( n == 0 )
                    break;
                totalRead += (wxFileOffset)n;
            }

            g_sink += (wxUint64)zis.GetLength();
            g_sink += (wxUint64)totalRead;

            zis.CloseEntry();
        }
        delete ze;

        if ( ++iterations > 10000 )
            break;
    }

    g_sink += (wxUint64)zis.GetComment().length();
}

extern "C" int LLVMFuzzerTestOneInput(const wxUint8 *data, size_t size)
{
    wxLogNull noLog;

    if ( size < 1 )
        return 0;

    const bool nonSeekable = (data[0] & 1) != 0;
    const wxUint8 *archiveData = data + 1;
    const size_t archiveSize = size - 1;

    wxMemoryInputStream mis(archiveData, archiveSize);

    if ( nonSeekable ) {
        NonSeekableWrapperStream nss(mis);
        ExerciseZip(nss, false);
    } else {
        ExerciseZip(mis, true);
    }

    return 0;
}
