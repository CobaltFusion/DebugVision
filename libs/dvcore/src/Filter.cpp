#include "dvcore/Filter.h"

#include "dvcore/Platform.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dv {

namespace {

constexpr std::array filterTypeNames = {"Include", "Exclude", "Highlight", "Token", "Stop", "Track", "Once", "Clear", "Beep", "MatchColor", "Bookmark"};
constexpr std::array matchTypeNames = {"Simple", "Wildcard", "Regex", "RegexGroups", "RegexCase"};

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// Minimal tree used to read DebugView++ filter files: XML elements or boost property-tree JSON (which may contain duplicate keys)
struct Node
{
    std::string name;
    std::string value;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Node> children;

    const Node* child(std::string_view n) const
    {
        for (auto& c : children)
            if (c.name == n)
                return &c;
        return nullptr;
    }

    std::string childValue(std::string_view n, std::string fallback = {}) const
    {
        auto c = child(n);
        return c ? c->value : fallback;
    }

    std::string attribute(std::string_view n) const
    {
        for (auto& [k, v] : attributes)
            if (k == n)
                return v;
        // property-tree JSON stores attributes as a "<xmlattr>" child
        if (auto a = child("<xmlattr>"))
            return a->childValue(n);
        return {};
    }
};

// ------------------------------------------------------------------------------------- tiny XML reader

class XmlReader
{
public:
    explicit XmlReader(std::string_view text) :
        m_text(text)
    {
    }

    Node parseDocument()
    {
        skipMisc();
        Node root = parseElement();
        return root;
    }

private:
    [[noreturn]] void fail(const std::string& message) { throw std::runtime_error("XML error at offset " + std::to_string(m_pos) + ": " + message); }

    bool startsWith(std::string_view s) const { return m_text.substr(m_pos, s.size()) == s; }

    void skipWhitespace()
    {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos])))
            ++m_pos;
    }

    void skipUntil(std::string_view end)
    {
        auto p = m_text.find(end, m_pos);
        if (p == std::string_view::npos)
            fail("unterminated construct");
        m_pos = p + end.size();
    }

    // skips whitespace, the XML declaration, comments, processing instructions and DOCTYPE
    void skipMisc()
    {
        for (;;)
        {
            skipWhitespace();
            if (startsWith("<?"))
                skipUntil("?>");
            else if (startsWith("<!--"))
                skipUntil("-->");
            else if (startsWith("<!"))
                skipUntil(">");
            else
                return;
        }
    }

    std::string parseName()
    {
        auto start = m_pos;
        while (m_pos < m_text.size() && !std::isspace(static_cast<unsigned char>(m_text[m_pos])) && m_text[m_pos] != '>' && m_text[m_pos] != '/' && m_text[m_pos] != '=')
            ++m_pos;
        if (start == m_pos)
            fail("expected a name");
        return std::string(m_text.substr(start, m_pos - start));
    }

    static std::string decode(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] != '&')
            {
                out.push_back(s[i]);
                continue;
            }
            auto semi = s.find(';', i);
            if (semi == std::string_view::npos)
            {
                out.push_back(s[i]);
                continue;
            }
            auto entity = s.substr(i + 1, semi - i - 1);
            if (entity == "lt") out.push_back('<');
            else if (entity == "gt") out.push_back('>');
            else if (entity == "amp") out.push_back('&');
            else if (entity == "quot") out.push_back('"');
            else if (entity == "apos") out.push_back('\'');
            else if (!entity.empty() && entity[0] == '#')
            {
                char32_t cp = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X') ? static_cast<char32_t>(std::stoul(std::string(entity.substr(2)), nullptr, 16))
                                                                                         : static_cast<char32_t>(std::stoul(std::string(entity.substr(1))));
                if (cp < 0x80) out.push_back(static_cast<char>(cp));
                else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
                else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
                else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
            }
            else
            {
                out.append(s.substr(i, semi - i + 1));
            }
            i = semi;
        }
        return out;
    }

    Node parseElement()
    {
        if (!startsWith("<"))
            fail("expected '<'");
        ++m_pos;
        Node node;
        node.name = parseName();
        for (;;)
        {
            skipWhitespace();
            if (startsWith("/>"))
            {
                m_pos += 2;
                return node;
            }
            if (startsWith(">"))
            {
                ++m_pos;
                break;
            }
            auto key = parseName();
            skipWhitespace();
            if (!startsWith("="))
                fail("expected '='");
            ++m_pos;
            skipWhitespace();
            if (m_pos >= m_text.size() || (m_text[m_pos] != '"' && m_text[m_pos] != '\''))
                fail("expected a quoted attribute value");
            auto quote = m_text[m_pos++];
            auto end = m_text.find(quote, m_pos);
            if (end == std::string_view::npos)
                fail("unterminated attribute value");
            node.attributes.emplace_back(key, decode(m_text.substr(m_pos, end - m_pos)));
            m_pos = end + 1;
        }
        // content
        std::string text;
        for (;;)
        {
            if (m_pos >= m_text.size())
                fail("unexpected end of document in <" + node.name + ">");
            if (startsWith("</"))
            {
                m_pos += 2;
                auto name = parseName();
                if (name != node.name)
                    fail("mismatched </" + name + ">, expected </" + node.name + ">");
                skipWhitespace();
                ++m_pos; // '>'
                break;
            }
            if (startsWith("<!--"))
            {
                skipUntil("-->");
                continue;
            }
            if (startsWith("<![CDATA["))
            {
                m_pos += 9;
                auto end = m_text.find("]]>", m_pos);
                if (end == std::string_view::npos)
                    fail("unterminated CDATA");
                text.append(m_text.substr(m_pos, end - m_pos));
                m_pos = end + 3;
                continue;
            }
            if (startsWith("<"))
            {
                node.children.push_back(parseElement());
                continue;
            }
            auto next = m_text.find('<', m_pos);
            text.append(decode(m_text.substr(m_pos, next - m_pos)));
            m_pos = next;
        }
        if (node.children.empty())
            node.value = text;
        return node;
    }

    std::string_view m_text;
    size_t m_pos = 0;
};

// ------------------------------------------------------------------------------------- property-tree JSON reader

// boost::property_tree::write_json writes every value as a string and uses duplicate keys for lists,
// which nlohmann::json's DOM would collapse; the SAX interface sees all of them.
class PtreeSax : public nlohmann::json_sax<nlohmann::json>
{
public:
    Node root;

    bool null() override { return setValue(""); }
    bool boolean(bool val) override { return setValue(val ? "true" : "false"); }
    bool number_integer(number_integer_t val) override { return setValue(std::to_string(val)); }
    bool number_unsigned(number_unsigned_t val) override { return setValue(std::to_string(val)); }
    bool number_float(number_float_t, const string_t& s) override { return setValue(s); }
    bool string(string_t& val) override { return setValue(val); }
    bool binary(binary_t&) override { return true; }

    bool key(string_t& val) override
    {
        auto top = m_stack.back();
        top->children.push_back(Node{val, {}, {}, {}});
        m_target = &top->children.back();
        return true;
    }

    bool start_object(std::size_t) override { return push(); }
    bool start_array(std::size_t) override { return push(); }

    bool end_object() override
    {
        m_stack.pop_back();
        return true;
    }

    bool end_array() override
    {
        m_stack.pop_back();
        return true;
    }

    bool parse_error(std::size_t position, const std::string&, const nlohmann::detail::exception& ex) override
    {
        throw std::runtime_error("JSON error at offset " + std::to_string(position) + ": " + ex.what());
    }

private:
    // the node that receives the next value: the node of the last key, or a new (unnamed) array element
    Node* target()
    {
        if (m_stack.empty())
            return &root;
        if (m_target)
            return std::exchange(m_target, nullptr);
        m_stack.back()->children.push_back(Node{});
        return &m_stack.back()->children.back();
    }

    bool push()
    {
        m_stack.push_back(target());
        return true;
    }

    bool setValue(const std::string& v)
    {
        target()->value = v;
        return true;
    }

    // ancestors stay valid: only the children of the top node are modified
    std::vector<Node*> m_stack;
    Node* m_target = nullptr;
};

// ------------------------------------------------------------------------------------- conversion

Color colorFromNode(const Node* node, Color fallback)
{
    if (!node)
        return fallback;
    if (equalsIgnoreCase(node->attribute("Auto"), "true"))
        return Color::autoColor();
    auto component = [&](const char* name) { return static_cast<uint8_t>(std::stoi(node->childValue(name, "0"))); };
    return Color::fromRgb(component("Red"), component("Green"), component("Blue"));
}

Filter filterFromNode(const Node& n)
{
    Filter f;
    f.enabled = !equalsIgnoreCase(n.childValue("Enable", "true"), "false");
    f.text = n.childValue("Text");
    f.matchType = parseMatchType(n.childValue("MatchType", "Simple")).value_or(MatchType::Simple);
    f.filterType = parseFilterType(n.childValue("FilterType", "Include")).value_or(FilterType::Include);
    f.background = colorFromNode(n.child("BackColor"), f.background);
    f.foreground = colorFromNode(n.child("TextColor"), f.foreground);
    if (f.filterType == FilterType::MatchColor)
    {
        f.filterType = FilterType::Token;
        f.background = Color::autoColor();
    }
    return f;
}

std::vector<Filter> filtersFromNode(const Node* n)
{
    std::vector<Filter> result;
    if (!n)
        return result;
    for (auto& c : n->children)
    {
        if (c.name == "Filter" || c.name == "MessageFilter" || c.name == "ProcessFilter" || c.name.empty())
            result.push_back(filterFromNode(c));
    }
    return result;
}

FilterSet filterSetFromNode(const Node& root)
{
    const Node* filter = root.name == "Filter" ? &root : root.child("Filter");
    if (!filter)
        throw std::runtime_error("not a DebugView++ filter file: missing <Filter> element");
    FilterSet set;
    set.name = filter->childValue("Name");
    set.messageFilters = filtersFromNode(filter->child("MessageFilters"));
    set.processFilters = filtersFromNode(filter->child("ProcessFilters"));
    return set;
}

std::string readFile(const std::string& utf8Path)
{
    std::ifstream in(platform::utf8ToPath(utf8Path), std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open '" + utf8Path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    auto content = ss.str();
    if (content.size() >= 3 && content.compare(0, 3, "\xEF\xBB\xBF") == 0)
        content.erase(0, 3);
    return content;
}

} // namespace

const char* toText(FilterType type)
{
    auto i = static_cast<size_t>(type);
    return i < filterTypeNames.size() ? filterTypeNames[i] : "Include";
}

const char* toText(MatchType type)
{
    auto i = static_cast<size_t>(type);
    return i < matchTypeNames.size() ? matchTypeNames[i] : "Simple";
}

std::optional<FilterType> parseFilterType(std::string_view text)
{
    for (size_t i = 0; i < filterTypeNames.size(); ++i)
        if (equalsIgnoreCase(text, filterTypeNames[i]))
            return static_cast<FilterType>(i);
    return std::nullopt;
}

std::optional<MatchType> parseMatchType(std::string_view text)
{
    for (size_t i = 0; i < matchTypeNames.size(); ++i)
        if (equalsIgnoreCase(text, matchTypeNames[i]))
            return static_cast<MatchType>(i);
    return std::nullopt;
}

void to_json(nlohmann::json& j, const Color& c)
{
    if (c.isAuto)
    {
        j = "auto";
        return;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06x", c.rgb & 0xffffff);
    j = buf;
}

void from_json(const nlohmann::json& j, Color& c)
{
    if (!j.is_string())
        return;
    auto s = j.get<std::string>();
    if (equalsIgnoreCase(s, "auto"))
    {
        c = Color::autoColor();
        return;
    }
    if (s.size() == 7 && s[0] == '#')
        c = Color{static_cast<uint32_t>(std::stoul(s.substr(1), nullptr, 16)), false};
}

void to_json(nlohmann::json& j, const Filter& f)
{
    j = {{"enabled", f.enabled}, {"text", f.text}, {"matchType", toText(f.matchType)}, {"filterType", toText(f.filterType)}, {"background", f.background}, {"foreground", f.foreground}};
}

void from_json(const nlohmann::json& j, Filter& f)
{
    f.enabled = j.value("enabled", true);
    f.text = j.value("text", std::string());
    f.matchType = parseMatchType(j.value("matchType", std::string("Simple"))).value_or(MatchType::Simple);
    f.filterType = parseFilterType(j.value("filterType", std::string("Include"))).value_or(FilterType::Include);
    if (j.contains("background"))
        from_json(j["background"], f.background);
    if (j.contains("foreground"))
        from_json(j["foreground"], f.foreground);
    if (f.filterType == FilterType::MatchColor)
    {
        f.filterType = FilterType::Token;
        f.background = Color::autoColor();
    }
}

void to_json(nlohmann::json& j, const FilterSet& s)
{
    j = {{"name", s.name}, {"messageFilters", s.messageFilters}, {"processFilters", s.processFilters}};
}

void from_json(const nlohmann::json& j, FilterSet& s)
{
    s.name = j.value("name", std::string());
    s.messageFilters = j.value("messageFilters", std::vector<Filter>());
    s.processFilters = j.value("processFilters", std::vector<Filter>());
}

FilterSet parseFilterFile(std::string_view content, bool xml)
{
    if (xml)
        return filterSetFromNode(XmlReader(content).parseDocument());

    // our own JSON format has "messageFilters"; DebugView++'s property-tree JSON has "Filter"
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (!j.is_discarded() && j.is_object() && (j.contains("messageFilters") || j.contains("processFilters")))
        return j.get<FilterSet>();
    PtreeSax sax;
    nlohmann::json::sax_parse(content, &sax);
    return filterSetFromNode(sax.root);
}

FilterSet loadFilterFile(const std::string& utf8Path)
{
    auto content = readFile(utf8Path);
    auto first = content.find_first_not_of(" \t\r\n");
    bool xml = first != std::string::npos && content[first] == '<';
    return parseFilterFile(content, xml);
}

void saveFilterFile(const std::string& utf8Path, const FilterSet& filters)
{
    std::ofstream out(platform::utf8ToPath(utf8Path), std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot write '" + utf8Path + "'");
    out << nlohmann::json(filters).dump(2) << '\n';
}

Color ColorGenerator::next()
{
    constexpr double goldenRatioConjugate = 0.618033988749895;
    m_hue = std::fmod(m_hue + goldenRatioConjugate, 1.0);
    // HSV -> RGB
    auto h = m_hue * 6.0;
    auto i = static_cast<int>(h);
    auto f = h - i;
    auto p = m_value * (1 - m_saturation);
    auto q = m_value * (1 - m_saturation * f);
    auto t = m_value * (1 - m_saturation * (1 - f));
    double r = 0;
    double g = 0;
    double b = 0;
    switch (i % 6)
    {
    case 0: r = m_value; g = t; b = p; break;
    case 1: r = q; g = m_value; b = p; break;
    case 2: r = p; g = m_value; b = t; break;
    case 3: r = p; g = q; b = m_value; break;
    case 4: r = t; g = p; b = m_value; break;
    default: r = m_value; g = p; b = q; break;
    }
    auto to8 = [](double v) { return static_cast<uint8_t>(std::lround(v * 255.0)); };
    return Color::fromRgb(to8(r), to8(g), to8(b));
}

} // namespace dv
