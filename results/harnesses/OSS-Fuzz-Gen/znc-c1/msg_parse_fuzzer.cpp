#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <znc/Message.h>
#include <znc/Utils.h>
#include <znc/ZNCString.h>

namespace {

const char* kCommands[] = {
    "PRIVMSG", "NOTICE", "ACCOUNT", "AUTHENTICATE", "AWAY", "CAP",
    "CHGHOST", "ERROR", "INVITE", "JOIN", "KICK", "MODE", "NICK",
    "PART", "PING", "PONG", "QUIT", "TAGMSG", "TOPIC", "WALLOPS",
    "123",
    "1A2",
    "12A",
    "XYZZY",
    "privmsg", "Join", "cap", "Mode",
};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0) {
    CMessage Message;
    Message.Parse(CString());
    return 0;
  }

  uint8_t mode = data[0];
  const uint8_t *rest = data + 1;
  size_t restSize = size - 1;
  CString sRest(reinterpret_cast<const char *>(rest), restSize);

  CString sMessage;
  if ((mode & 1) == 0) {

    sMessage = CString(reinterpret_cast<const char *>(data), size);
  } else {

    size_t cmdIdx = (restSize > 0 ? rest[0] : mode) %
                    (sizeof(kCommands) / sizeof(kCommands[0]));
    CString sCommand(kCommands[cmdIdx]);

    CString sTags;
    if (restSize > 1) {
      uint8_t tagMode = rest[1] % 8;
      switch (tagMode) {
        case 0:

          sTags = "";
          break;
        case 1:

          sTags = "@onlykey ";
          break;
        case 2:

          sTags = "@ ";
          break;
        case 3:

          sTags =
              "@id=a\\:b\\sc\\\\d\\re\\nf\\0g\\xh;time=2021-01-02T03:04:05.000Z ";
          break;
        case 4: {

          CString sTimeVal = sRest.Token(0, false, " ");
          sTags = "@time=" + sTimeVal + " ";
          break;
        }
        case 5: {

          if (restSize > 2 && (rest[2] & 1)) {
            sTags = "@time=123456-01-02T03:04:05.000Z ";
          } else {
            sTags = "@time=12345-01-02T03:04:05.000Z ";
          }
          break;
        }
        case 6:

          sTags = "@id=foo\\ ";
          break;
        default:
          sTags = "@time=2021-01-02T03:04:05.000Z;id=" +
                  sRest.Token(0, false, " ") + " ";
          break;
      }
    }

    CString sPrefix;
    if (restSize > 2) {
      switch (rest[2] % 6) {
        case 0:
          sPrefix = "";
          break;
        case 1:
          sPrefix = ": ";
          break;
        case 2:
          sPrefix = ":nick ";
          break;
        case 3:
          sPrefix = ":nick@host ";
          break;
        case 4:
          sPrefix = ":nick!identonly ";
          break;
        default:
          sPrefix = ":nick!ident@host ";
          break;
      }
    }

    CString sParams;
    if (sCommand.Equals("PRIVMSG") || sCommand.Equals("NOTICE")) {

      CString sTrailer = sRest;
      uint8_t ctcpFlavor = (restSize > 3 ? rest[3] : 0) % 5;
      if (ctcpFlavor == 1) {

        sTrailer = "\001" + sTrailer + "\001";
      } else if (ctcpFlavor == 2) {

        sTrailer = "\001ACTION " + sTrailer + "\001";
      } else if (ctcpFlavor == 3) {

        sTrailer = "\001" + sTrailer;
      } else if (ctcpFlavor == 4) {

        sTrailer = sTrailer + "\001";
      }

      sParams = "target :" + sTrailer;
    } else {
      sParams = sRest;
    }

    sMessage = sTags + sPrefix + sCommand + " " + sParams;
  }

  CMessage Message(sMessage);

  Message.Parse(sMessage);

  return 0;
}