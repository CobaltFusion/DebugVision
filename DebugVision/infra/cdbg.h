#pragma once

#include <streambuf>
#include <ostream>
#include <string>

namespace infra {
namespace dbgstream {
namespace output {

// implement these to write the messages to the output of your choise.
void write(const char* msg);
void write(const wchar_t* msg);

} // namespace output

template <class Elem, class Tr = std::char_traits<Elem>, class Alloc = std::allocator<Elem>>
class basic_debugbuf : public std::basic_streambuf<Elem, Tr>
{
    using _int_type = typename std::basic_streambuf<Elem, Tr>::int_type;
    using _traits_type = typename std::basic_streambuf<Elem, Tr>::traits_type;

protected:
    virtual int sync() override
    {
        output::write(m_buf.c_str());
        m_buf.clear();
        return 0;
    }

    virtual _int_type overflow(_int_type c) override
    {
        if (c == _traits_type::eof())
            return 0;

        m_buf += _traits_type::to_char_type(c);
        if (c == '\n')
            sync();
        return c;
    }

private:
    std::basic_string<Elem, Tr, Alloc> m_buf;
};

template <class Elem, class Tr = std::char_traits<Elem>>
class basic_dbgstream : public std::basic_ostream<Elem, Tr>
{
public:
    basic_dbgstream() :
        std::basic_ostream<Elem, Tr>(&buf)
    {
    }

private:
    basic_debugbuf<Elem, Tr> buf;
};

using dbgstream = basic_dbgstream<char>;
using wdbgstream = basic_dbgstream<wchar_t>;

} // namespace dbgstream
} // namespace infra

inline infra::dbgstream::dbgstream& cdbg()
{
    static infra::dbgstream::dbgstream d;
    return d;
}

inline infra::dbgstream::wdbgstream& wcdbg()
{
    static infra::dbgstream::wdbgstream d;
    return d;
}
