#include "dvcore/Encoding.h"

#include <cctype>
#include <cerrno>
#include <iconv.h>

namespace dv {

namespace {

// Converts a legacy charset to UTF-8 with iconv; the default charset is windows-1252,
// because 8-bit log files on Linux and macOS usually originate from Windows systems.
class IconvLegacyCodec : public LegacyCodec
{
public:
    explicit IconvLegacyCodec(std::string charset) :
        m_charset(std::move(charset))
    {
        m_cd = iconv_open("UTF-8", m_charset.c_str());
        if (m_cd == reinterpret_cast<iconv_t>(-1))
        {
            m_charset = "WINDOWS-1252";
            m_cd = iconv_open("UTF-8", m_charset.c_str());
        }
    }

    ~IconvLegacyCodec() override
    {
        if (m_cd != reinterpret_cast<iconv_t>(-1))
            iconv_close(m_cd);
    }

    std::string name() const override { return m_charset; }

    void toUtf8(std::string_view text, std::string& out) override
    {
        if (m_cd == reinterpret_cast<iconv_t>(-1))
        {
            appendSanitizedUtf8(text, out);
            return;
        }
        iconv(m_cd, nullptr, nullptr, nullptr, nullptr); // reset shift state
        auto in = const_cast<char*>(text.data());
        size_t inLeft = text.size();
        char buffer[4096];
        while (inLeft > 0)
        {
            char* outPtr = buffer;
            size_t outLeft = sizeof(buffer);
            auto rc = iconv(m_cd, &in, &inLeft, &outPtr, &outLeft);
            out.append(buffer, sizeof(buffer) - outLeft);
            if (rc == static_cast<size_t>(-1))
            {
                if (errno == E2BIG)
                    continue;
                // invalid or incomplete sequence: replace one byte and continue
                out.append("\xEF\xBF\xBD");
                ++in;
                --inLeft;
            }
        }
    }

private:
    std::string m_charset;
    iconv_t m_cd;
};

} // namespace

std::unique_ptr<LegacyCodec> LegacyCodec::create(std::string_view charset)
{
    std::string name(charset);
    if (name.empty() || name == "system" || name == "ansi" || name == "oem")
        name = "WINDOWS-1252";
    else if (name.rfind("cp", 0) == 0 && name.size() > 2 && std::isdigit(static_cast<unsigned char>(name[2])))
        name = "CP" + name.substr(2);
    else if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0])))
        name = "CP" + name;
    return std::make_unique<IconvLegacyCodec>(name);
}

} // namespace dv
