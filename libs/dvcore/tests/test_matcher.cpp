#include "dvcore/Matcher.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

using namespace dv;

TEST_CASE("matcher: plain text and wildcards are case-insensitive")
{
    FilterState state;
    CHECK(Matcher("frame", MatchType::Simple).matches("Frame 1 start", state));
    CHECK(Matcher("a.b", MatchType::Simple).matches("xa.by", state));
    CHECK_FALSE(Matcher("a.b", MatchType::Simple).matches("axb", state));
    CHECK(Matcher("fr*start", MatchType::Wildcard).matches("FRAME 1 START", state));
}

TEST_CASE("matcher: RegexCase is case-sensitive")
{
    FilterState state;
    CHECK(Matcher("IRQ", MatchType::Regex).matches("irq 5", state));
    CHECK_FALSE(Matcher("IRQ", MatchType::RegexCase).matches("irq 5", state));
}

TEST_CASE("matcher: capture groups")
{
    FilterState state;
    std::string groups;
    Matcher frame("frame (\\d+) (start|done)", MatchType::Regex);
    REQUIRE(frame.matchGroups("Frame 12 done", state, groups));
    CHECK(groups == std::string("12\0done", 7));
    CHECK_FALSE(frame.matchGroups("Frame x done", state, groups));

    // without groups, or plain text: a match with an empty key
    REQUIRE(Matcher("frame \\d+", MatchType::Regex).matchGroups("frame 3", state, groups));
    CHECK(groups.empty());
    REQUIRE(Matcher("frame", MatchType::Simple).matchGroups("FRAME", state, groups));
    CHECK(groups.empty());
}

TEST_CASE("matcher: an invalid pattern throws")
{
    CHECK_THROWS_AS(Matcher("(unclosed", MatchType::Regex), std::runtime_error);
}
