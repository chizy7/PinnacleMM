#include "../../visualization/WebServer.h"

#include <gtest/gtest.h>

namespace pinnacle::visualization {

TEST(QueryString, ParsesStandardQueryParameters) {
  auto params = parseQueryString("start=1234567890&end=9876543210&limit=100");

  EXPECT_EQ(params.at("start"), "1234567890");
  EXPECT_EQ(params.at("end"), "9876543210");
  EXPECT_EQ(params.at("limit"), "100");
}

TEST(QueryString, SupportsEncodedValuesAndOptionalQuestionMark) {
  auto params =
      parseQueryString("?name=Oore%20Fasawe&search=C%2B%2B&note=hello+world");

  EXPECT_EQ(params.at("name"), "Oore Fasawe");
  EXPECT_EQ(params.at("search"), "C++");
  EXPECT_EQ(params.at("note"), "hello world");
}

TEST(QueryString, ReturnsEmptyMapForEmptyQuery) {
  EXPECT_TRUE(parseQueryString("").empty());
}

TEST(QueryString, HandlesEmptyMissingAndRepeatedValues) {
  auto params = parseQueryString("limit=&debug&metric=pnl&metric=sharpe");

  EXPECT_EQ(params.at("limit"), "");
  EXPECT_EQ(params.at("debug"), "");
  EXPECT_EQ(params.at("metric"), "sharpe");
}

TEST(QueryString, PreservesEqualsCharactersInValues) {
  auto params = parseQueryString("token=abc=123");

  EXPECT_EQ(params.at("token"), "abc=123");
}

TEST(QueryString, SkipsMalformedParameters) {
  auto params =
      parseQueryString("valid=value&bad=%ZZ&truncated=%A&empty-key=value");

  EXPECT_EQ(params.at("valid"), "value");
  EXPECT_EQ(params.count("bad"), 0);
  EXPECT_EQ(params.count("truncated"), 0);
  EXPECT_EQ(params.count("empty-key"), 1);
}

} // namespace pinnacle::visualization
