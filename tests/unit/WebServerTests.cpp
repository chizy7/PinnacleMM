#include "../../visualization/WebServer.h"

#include <gtest/gtest.h>

namespace pinnacle::visualization {

class WebServerTests : public ::testing::Test {
protected:
  std::shared_ptr<PerformanceCollector> collector =
      std::make_shared<PerformanceCollector>();
  RestAPIServer server{collector};

  std::unordered_map<std::string, std::string>
  parseQuery(const std::string& query) {
    return server.parseQueryString(query);
  }

  http::status route(const std::string& target) {
    http::request<http::string_body> request{http::verb::get, target, 11};
    return server.handleRequest(std::move(request)).result();
  }
};

TEST_F(WebServerTests, ParsesStandardQueryParameters) {
  auto params = parseQuery("start=1234567890&end=9876543210&limit=100");

  EXPECT_EQ(params.at("start"), "1234567890");
  EXPECT_EQ(params.at("end"), "9876543210");
  EXPECT_EQ(params.at("limit"), "100");
}

TEST_F(WebServerTests, SupportsEncodedValuesAndOptionalQuestionMark) {
  auto params =
      parseQuery("?name=Oore%20Fasawe&search=C%2B%2B&note=hello+world");

  EXPECT_EQ(params.at("name"), "Oore Fasawe");
  EXPECT_EQ(params.at("search"), "C++");
  EXPECT_EQ(params.at("note"), "hello world");
}

TEST_F(WebServerTests, ReturnsEmptyMapForEmptyQuery) {
  EXPECT_TRUE(parseQuery("").empty());
}

TEST_F(WebServerTests, HandlesEmptyMissingAndRepeatedValues) {
  auto params = parseQuery("limit=&debug&metric=pnl&metric=sharpe");

  EXPECT_EQ(params.at("limit"), "");
  EXPECT_EQ(params.at("debug"), "");
  EXPECT_EQ(params.at("metric"), "sharpe");
}

TEST_F(WebServerTests, PreservesEqualsCharactersInValues) {
  auto params = parseQuery("token=abc=123");

  EXPECT_EQ(params.at("token"), "abc=123");
}

TEST_F(WebServerTests, SkipsMalformedParameters) {
  auto params = parseQuery("valid=value&bad=%ZZ&truncated=%A&empty-key=value");

  EXPECT_EQ(params.at("valid"), "value");
  EXPECT_EQ(params.count("bad"), 0);
  EXPECT_EQ(params.count("truncated"), 0);
  EXPECT_EQ(params.count("empty-key"), 1);
}

TEST_F(WebServerTests, RoutesPerformanceRequestsWithQueryStrings) {
  EXPECT_EQ(
      route(
          "/api/v1/strategies/primary_strategy/performance?start=100&end=500"),
      http::status::ok);
}

} // namespace pinnacle::visualization
