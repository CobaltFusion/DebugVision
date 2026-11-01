#include "dvcore/Settings.h"
#include "dvcore/SourceConfig.h"

#include <doctest/doctest.h>

using namespace dv;

TEST_CASE("OutputDebugString is only available on Windows")
{
    auto& registry = SourceRegistry::instance();
#ifdef _WIN32
    CHECK(registry.isAvailable("outputdebugstring"));
#else
    CHECK_FALSE(registry.isAvailable("outputdebugstring"));
    CHECK_THROWS(registry.parseSpec("outputdebugstring"));
#endif
    CHECK(registry.isAvailable("udp"));
    CHECK(registry.isAvailable("file"));
    CHECK(registry.isAvailable("process"));
}

TEST_CASE("parse source specs")
{
    auto& registry = SourceRegistry::instance();
    auto udp = registry.parseSpec("udp:port=3000,bind=127.0.0.1,group=239.1.2.3,allowedSenders=10.0.0.0/8;192.168.1.5,name=my udp");
    CHECK(udp.type == "udp");
    CHECK(udp.name == "my udp");
    auto p = udp.getParams<UdpParams>();
    CHECK(p.port == 3000);
    CHECK(p.bindAddress == "127.0.0.1");
    CHECK(p.multicastGroup == "239.1.2.3");
    CHECK(p.allowedSenders == std::vector<std::string>{"10.0.0.0/8", "192.168.1.5"});

    auto file = registry.parseSpec("file:path=C:/logs/app.log,tail=0,encoding=utf-16le");
    CHECK(file.getParams<FileParams>().path == "C:/logs/app.log");
    CHECK_FALSE(file.getParams<FileParams>().tail);
    CHECK(file.encodingValue() == Encoding::Utf16LE);

    CHECK_THROWS(registry.parseSpec("udp:port=0"));
    CHECK_THROWS(registry.parseSpec("udp:nonsense=1"));
    CHECK_THROWS(registry.parseSpec("carrier-pigeon"));
}

TEST_CASE("source configs round trip and keep unknown types and fields")
{
    auto json = nlohmann::json::parse(R"({
        "sources": [
            { "id": "a", "type": "outputdebugstring", "name": "ODS", "params": { "scope": "both" } },
            { "id": "b", "type": "udp", "params": { "port": 2020, "futureOption": 7 }, "futureField": "x" },
            { "id": "c", "type": "quantum-entangler", "params": { "qubits": 3 } }
        ],
        "someFutureSetting": true
    })");
    auto settings = json.get<AppSettings>();
    REQUIRE(settings.sources.size() == 3);
    CHECK(settings.sources[1].getParams<UdpParams>().port == 2020);
    nlohmann::json back = settings;
    CHECK(back["someFutureSetting"] == true);
    CHECK(back["sources"][0]["type"] == "outputdebugstring");
    CHECK(back["sources"][0]["params"]["scope"] == "both");
    CHECK(back["sources"][1]["params"]["futureOption"] == 7);
    CHECK(back["sources"][1]["futureField"] == "x");
    CHECK(back["sources"][2]["params"]["qubits"] == 3);
}

TEST_CASE("temporary sources are not saved")
{
    AppSettings settings;
    auto c = SourceRegistry::instance().parseSpec("file:path=x.log");
    c.temporary = true;
    settings.sources.push_back(c);
    nlohmann::json j = settings;
    CHECK(j["sources"].empty());
}

TEST_CASE("view settings round trip")
{
    AppSettings s;
    ViewSettings v;
    v.name = "errors";
    v.filters.messageFilters.push_back(Filter{"error", MatchType::Simple, FilterType::Include});
    v.columns[static_cast<int>(Column::Date)].visible = true;
    v.clockTime = false;
    s.views = {v};
    s.historyLimit = 1000000;
    nlohmann::json j = s;
    auto back = j.get<AppSettings>();
    REQUIRE(back.views.size() == 1);
    CHECK(back.views[0].name == "errors");
    CHECK(back.views[0].filters == v.filters);
    CHECK(back.views[0].columns[static_cast<int>(Column::Date)].visible);
    CHECK_FALSE(back.views[0].clockTime);
    CHECK(back.historyLimit == 1000000);
}
