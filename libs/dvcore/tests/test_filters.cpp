#include "dvcore/FilterEngine.h"

#include <doctest/doctest.h>

using namespace dv;

namespace {

Filter make(std::string text, FilterType type, MatchType match = MatchType::Simple)
{
    Filter f;
    f.text = std::move(text);
    f.filterType = type;
    f.matchType = match;
    return f;
}

} // namespace

TEST_CASE("case-insensitive literal search")
{
    CHECK(containsIgnoreCase("Hello World", "world"));
    CHECK(containsIgnoreCase("x.y", "."));
    CHECK_FALSE(containsIgnoreCase("Hello", "world"));
    CHECK(findIgnoreCase("aXbxc", "x", 2) == 3);
}

TEST_CASE("include and exclude semantics")
{
    FilterSet set;
    set.messageFilters = {make("error", FilterType::Include), make("ignore", FilterType::Exclude)};
    FilterEngine engine(set);
    auto state = engine.makeState();
    CHECK(engine.evaluate("an Error occurred", "p", state).included);
    CHECK_FALSE(engine.evaluate("all fine", "p", state).included);
    CHECK_FALSE(engine.evaluate("error, but ignore it", "p", state).included);
}

TEST_CASE("without include filters everything that is not excluded passes")
{
    FilterSet set;
    set.processFilters = {make("noisy.exe", FilterType::Exclude)};
    FilterEngine engine(set);
    auto state = engine.makeState();
    CHECK(engine.evaluate("x", "app.exe", state).included);
    CHECK_FALSE(engine.evaluate("x", "NOISY.EXE", state).included);
}

TEST_CASE("wildcard and regex match types")
{
    FilterSet set;
    set.messageFilters = {make("start*end", FilterType::Include, MatchType::Wildcard), make("^id=\\d+$", FilterType::Include, MatchType::Regex),
        make("CaseSensitive", FilterType::Include, MatchType::RegexCase)};
    FilterEngine engine(set);
    auto state = engine.makeState();
    CHECK(engine.evaluate("the start and the END", "p", state).included);
    CHECK(engine.evaluate("ID=42", "p", state).included);
    CHECK_FALSE(engine.evaluate("id=x", "p", state).included);
    CHECK(engine.evaluate("CaseSensitive", "p", state).included);
    CHECK_FALSE(engine.evaluate("casesensitive", "p", state).included);
}

TEST_CASE("non-ASCII text is matched case-insensitively")
{
    FilterSet set;
    set.messageFilters = {make("\xC3\x89T\xC3\x89", FilterType::Include)}; // ÉTÉ
    FilterEngine engine(set);
    auto state = engine.makeState();
    CHECK(engine.evaluate("\xC3\xA9t\xC3\xA9 2024", "p", state).included); // été
}

TEST_CASE("Once includes the first match only")
{
    FilterSet set;
    set.messageFilters = {make("keep", FilterType::Include), make("first", FilterType::Once)};
    FilterEngine engine(set);
    auto state = engine.makeState();
    CHECK(engine.isStateful());
    CHECK(engine.evaluate("first time", "p", state).included);
    CHECK_FALSE(engine.evaluate("first again", "p", state).included);
    CHECK(engine.evaluate("keep", "p", state).included);
}

TEST_CASE("row colors: highlight filters take precedence")
{
    FilterSet set;
    auto include = make("msg", FilterType::Include);
    include.background = Color::fromRgb(1, 2, 3);
    auto highlight = make("important", FilterType::Highlight);
    highlight.background = Color::fromRgb(255, 0, 0);
    set.messageFilters = {include, highlight};
    FilterEngine engine(set);
    auto state = engine.makeState();
    auto r1 = engine.evaluate("msg important", "p", state);
    CHECK(state.styles[r1.style].background == Color::fromRgb(255, 0, 0));
    auto r2 = engine.evaluate("msg", "p", state);
    CHECK(state.styles[r2.style].background == Color::fromRgb(1, 2, 3));
}

TEST_CASE("auto colors are assigned per distinct match")
{
    FilterSet set;
    auto f = make("thread (\\d+)", FilterType::Highlight, MatchType::RegexGroups);
    f.background = Color::autoColor();
    set.messageFilters = {f};
    FilterEngine engine(set);
    auto state = engine.makeState();
    auto a = engine.evaluate("thread 1 started", "p", state).style;
    auto b = engine.evaluate("thread 2 started", "p", state).style;
    auto c = engine.evaluate("THREAD 1 stopped", "p", state).style;
    CHECK(a != 0);
    CHECK(a != b);
    CHECK(a == c);
}

TEST_CASE("actions")
{
    FilterSet set;
    set.messageFilters = {make("stop here", FilterType::Stop), make("beep", FilterType::Beep), make("clear now", FilterType::Clear), make("mark", FilterType::Bookmark)};
    set.processFilters = {make("watched.exe", FilterType::Beep)};
    FilterEngine engine(set);
    auto state = engine.makeState();
    CHECK(engine.evaluate("please stop here", "p", state).actions == ActionStop);
    CHECK(engine.evaluate("beep", "p", state).actions == ActionBeep);
    CHECK(engine.evaluate("clear now", "p", state).actions == ActionClear);
    CHECK(engine.evaluate("mark", "p", state).actions == ActionBookmark);
    // process Beep filters test the process name (DebugView++ tested the message)
    CHECK(engine.evaluate("x", "watched.exe", state).actions == ActionBeep);
}

TEST_CASE("token highlights")
{
    FilterSet set;
    auto token = make("(\\d+)ms", FilterType::Token, MatchType::RegexGroups);
    token.background = Color::fromRgb(0, 255, 0);
    set.messageFilters = {token, make("WARN", FilterType::Token)};
    FilterEngine engine(set);
    auto state = engine.makeState();
    auto spans = engine.highlights("took 12ms and 7ms, warn", state);
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].begin == 5);
    CHECK(spans[0].end == 7);
    CHECK(spans[1].begin == 14);
    CHECK(spans[2].begin == 19);
}

TEST_CASE("compile errors identify the filter")
{
    FilterSet set;
    set.processFilters = {make("ok", FilterType::Include), make("(unclosed", FilterType::Include, MatchType::Regex)};
    try
    {
        FilterEngine engine(set);
        FAIL("expected FilterError");
    }
    catch (const FilterError& e)
    {
        CHECK(e.processFilter);
        CHECK(e.index == 1);
    }
}

TEST_CASE("DebugView++ XML filter files")
{
    const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<Filter>
  <Name>My View</Name>
  <MessageFilters>
    <MessageFilter>
      <Enable>true</Enable>
      <Text>a &amp; b</Text>
      <MatchType>Simple</MatchType>
      <FilterType>Exclude</FilterType>
      <BackColor><Red>255</Red><Green>128</Green><Blue>0</Blue></BackColor>
      <TextColor><Red>0</Red><Green>0</Green><Blue>0</Blue></TextColor>
    </MessageFilter>
    <Filter>
      <Enable>false</Enable>
      <Text>id=(\d+)</Text>
      <MatchType>RegexGroups</MatchType>
      <FilterType>MatchColor</FilterType>
      <BackColor Auto="true"/>
      <TextColor><Red>0</Red><Green>0</Green><Blue>0</Blue></TextColor>
    </Filter>
  </MessageFilters>
  <ProcessFilters>
    <ProcessFilter>
      <Enable>true</Enable><Text>svchost.exe</Text><MatchType>Simple</MatchType><FilterType>Exclude</FilterType>
      <BackColor><Red>255</Red><Green>255</Green><Blue>255</Blue></BackColor><TextColor><Red>0</Red><Green>0</Green><Blue>0</Blue></TextColor>
    </ProcessFilter>
  </ProcessFilters>
</Filter>)";
    auto set = parseFilterFile(xml, true);
    CHECK(set.name == "My View");
    REQUIRE(set.messageFilters.size() == 2);
    CHECK(set.messageFilters[0].text == "a & b");
    CHECK(set.messageFilters[0].filterType == FilterType::Exclude);
    CHECK(set.messageFilters[0].background == Color::fromRgb(255, 128, 0));
    CHECK_FALSE(set.messageFilters[1].enabled);
    CHECK(set.messageFilters[1].filterType == FilterType::Token);
    CHECK(set.messageFilters[1].background.isAuto);
    REQUIRE(set.processFilters.size() == 1);
    CHECK(set.processFilters[0].text == "svchost.exe");
}

TEST_CASE("DebugView++ property-tree JSON filter files with duplicate keys")
{
    const char* json = R"({
    "Filter": {
        "Name": "json view",
        "MessageFilters": {
            "Filter": { "Enable": "true", "Text": "one", "MatchType": "Simple", "FilterType": "Include",
                        "BackColor": { "Red": "1", "Green": "2", "Blue": "3" }, "TextColor": { "Red": "0", "Green": "0", "Blue": "0" } },
            "Filter": { "Enable": "true", "Text": "two", "MatchType": "Regex", "FilterType": "Highlight",
                        "BackColor": { "<xmlattr>": { "Auto": "true" } }, "TextColor": { "Red": "0", "Green": "0", "Blue": "0" } }
        },
        "ProcessFilters": ""
    }
})";
    auto set = parseFilterFile(json, false);
    CHECK(set.name == "json view");
    REQUIRE(set.messageFilters.size() == 2);
    CHECK(set.messageFilters[0].text == "one");
    CHECK(set.messageFilters[0].background == Color::fromRgb(1, 2, 3));
    CHECK(set.messageFilters[1].matchType == MatchType::Regex);
    CHECK(set.messageFilters[1].background.isAuto);
}

TEST_CASE("own JSON filter format round trip")
{
    FilterSet set;
    set.name = "round trip";
    auto f = make("x", FilterType::Track, MatchType::Wildcard);
    f.background = Color::autoColor();
    f.foreground = Color::fromRgb(10, 20, 30);
    set.messageFilters = {f};
    set.processFilters = {make("p.exe", FilterType::Exclude)};
    nlohmann::json j = set;
    auto back = parseFilterFile(j.dump(), false);
    CHECK(back == set);
}
