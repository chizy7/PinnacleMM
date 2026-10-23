#include "../../visualization/WebServer.h"

#include <gtest/gtest.h>

namespace pinnacle::visualization {

class RestHandler : public ::testing::Test {
protected:
  std::shared_ptr<PerformanceCollector> collector =
      std::make_shared<PerformanceCollector>();
  RestAPIServer server{collector};

  http::response<http::string_body> get(const std::string& target) {
    http::request<http::string_body> request{http::verb::get, target, 11};
    return server.handleRequest(std::move(request));
  }
};

class PerformanceHistory : public ::testing::Test {
protected:
  PerformanceCollector collector;
};

TEST_F(PerformanceHistory, FiltersSnapshotsInclusivelyByTimeRange) {
  PerformanceData first;
  first.timestamp = 100;
  first.pnl = 1.0;
  PerformanceData second;
  second.timestamp = 200;
  second.pnl = 2.0;
  PerformanceData third;
  third.timestamp = 300;
  third.pnl = 3.0;

  collector.recordPerformance("strategy", first);
  collector.recordPerformance("strategy", second);
  collector.recordPerformance("strategy", third);

  auto history = collector.getPerformanceHistory("strategy", 200, 300);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 200);
  EXPECT_EQ(history[1].timestamp, 300);
}

TEST_F(PerformanceHistory, AppliesMaximumHistorySize) {
  collector.setMaxHistorySize(2);

  for (uint64_t timestamp = 1; timestamp <= 3; ++timestamp) {
    PerformanceData data;
    data.timestamp = timestamp;
    collector.recordPerformance("strategy", data);
  }

  auto history = collector.getPerformanceHistory("strategy", 0, 3);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 2);
  EXPECT_EQ(history[1].timestamp, 3);
}

TEST_F(PerformanceHistory, LimitKeepsNewestSnapshotsInChronologicalOrder) {
  for (uint64_t timestamp = 1; timestamp <= 3; ++timestamp) {
    PerformanceData data;
    data.timestamp = timestamp;
    collector.recordPerformance("strategy", data);
  }

  auto history = collector.getPerformanceHistory("strategy", 0, 3, 2);

  ASSERT_EQ(history.size(), 2);
  EXPECT_EQ(history[0].timestamp, 2);
  EXPECT_EQ(history[1].timestamp, 3);
}

TEST_F(PerformanceHistory, StartEqualToFirstTimestampIsInclusive) {
  PerformanceData data;
  data.timestamp = 100;
  collector.recordPerformance("strategy", data);

  auto history = collector.getPerformanceHistory("strategy", 100, 100);

  ASSERT_EQ(history.size(), 1);
  EXPECT_EQ(history[0].timestamp, 100);
}

TEST(ChartHistory, DerivesPointsFromPerformanceHistory) {
  PerformanceCollector collector;
  for (uint64_t timestamp = 100; timestamp <= 300; timestamp += 100) {
    PerformanceData data;
    data.timestamp = timestamp;
    data.pnl = static_cast<double>(timestamp) / 100.0;
    data.totalTrades = static_cast<int>(timestamp);
    collector.recordPerformance("strategy", data);
  }

  auto pnl = collector.getChartDataInRange("strategy", "pnl", 200, 300);
  ASSERT_EQ(pnl.size(), 2);
  EXPECT_EQ(pnl[0].timestamp, 200);
  EXPECT_DOUBLE_EQ(pnl[0].value, 2.0);
  EXPECT_EQ(pnl[1].timestamp, 300);
  EXPECT_DOUBLE_EQ(pnl[1].value, 3.0);

  auto trades =
      collector.getChartDataInRange("strategy", "total_trades", 0, 100);
  ASSERT_EQ(trades.size(), 1);
  EXPECT_DOUBLE_EQ(trades[0].value, 100.0);
}

TEST(ChartHistory, ReportsSupportedMetricsAndRejectsUnknown) {
  for (const auto* metric :
       {"pnl", "position", "sharpe_ratio", "max_drawdown", "win_rate",
        "total_trades", "ml_accuracy", "prediction_time"}) {
    EXPECT_TRUE(PerformanceCollector::isChartMetricSupported(metric)) << metric;
  }
  EXPECT_FALSE(PerformanceCollector::isChartMetricSupported("volume"));
  EXPECT_FALSE(PerformanceCollector::isChartMetricSupported(""));

  PerformanceCollector collector;
  PerformanceData data;
  data.timestamp = 100;
  collector.recordPerformance("strategy", data);
  EXPECT_TRUE(
      collector.getChartDataInRange("strategy", "volume", 0, 1000).empty());
}

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

TEST(ChartRouting, ExtractsStrategyAndMetricPath) {
  EXPECT_EQ(extractPath("/api/v1/strategies/strategy/charts/pnl?range=1h"),
            "/api/v1/strategies/strategy/charts/pnl");
}

TEST_F(RestHandler, ReturnsFilteredPerformanceHistoryAndPreservesLatestShape) {
  PerformanceData first;
  first.timestamp = 100;
  first.pnl = 1.0;
  PerformanceData second;
  second.timestamp = 200;
  second.pnl = 2.0;
  collector->recordPerformance("strategy", first);
  collector->recordPerformance("strategy", second);

  auto ranged =
      get("/api/v1/strategies/strategy/performance?start=100&end=200");
  auto latest = get("/api/v1/strategies/strategy/performance");

  ASSERT_EQ(ranged.result(), http::status::ok);
  EXPECT_TRUE(nlohmann::json::parse(ranged.body())["data"].is_array());
  EXPECT_TRUE(nlohmann::json::parse(latest.body())["data"].is_object());
}

TEST_F(RestHandler, RejectsInvalidAndReversedPerformanceRanges) {
  EXPECT_EQ(get("/api/v1/strategies/strategy/performance?start=abc").result(),
            http::status::bad_request);
  EXPECT_EQ(
      get("/api/v1/strategies/strategy/performance?start=200&end=100").result(),
      http::status::bad_request);
}

TEST_F(RestHandler, ReturnsEmptyArrayForPerformanceRangeWithNoMatches) {
  PerformanceData data;
  data.timestamp = 100;
  collector->recordPerformance("strategy", data);

  auto response =
      get("/api/v1/strategies/strategy/performance?start=200&end=300");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  EXPECT_TRUE(body["data"].is_array());
  EXPECT_TRUE(body["data"].empty());
}

TEST_F(RestHandler, RejectsInvalidPerformanceLimitWithLimitMessage) {
  for (const auto* query : {"limit=abc", "limit=0", "limit=-1", "limit="}) {
    auto response =
        get(std::string("/api/v1/strategies/strategy/performance?") + query);
    auto body = nlohmann::json::parse(response.body());

    EXPECT_EQ(response.result(), http::status::bad_request) << query;
    EXPECT_EQ(body["error"], "Invalid performance limit") << query;
  }
}

TEST_F(RestHandler, LimitReturnsNewestSnapshotsInOrder) {
  for (uint64_t timestamp = 1; timestamp <= 3; ++timestamp) {
    PerformanceData data;
    data.timestamp = timestamp;
    collector->recordPerformance("strategy", data);
  }

  auto response = get("/api/v1/strategies/strategy/performance?limit=2");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  ASSERT_EQ(body["data"].size(), 2);
  EXPECT_EQ(body["data"][0]["timestamp"], 2);
  EXPECT_EQ(body["data"][1]["timestamp"], 3);
}

TEST_F(RestHandler, ReturnsRecordedPerformanceThroughRangeQuery) {
  PerformanceData data;
  data.timestamp = 100;
  data.pnl = 4.5;
  collector->recordPerformance("strategy", data);

  auto response =
      get("/api/v1/strategies/strategy/performance?start=100&end=100");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  ASSERT_EQ(body["data"].size(), 1);
  EXPECT_EQ(body["data"][0]["timestamp"], 100);
  EXPECT_DOUBLE_EQ(body["data"][0]["pnl"], 4.5);
}

TEST_F(PerformanceHistory, UnregisterClearsHistory) {
  PerformanceData data;
  data.timestamp = 100;
  collector.recordPerformance("strategy", data);
  collector.unregisterStrategy("strategy");

  EXPECT_TRUE(collector.getPerformanceHistory("strategy", 0, 200).empty());
  EXPECT_TRUE(collector.getChartDataInRange("strategy", "pnl", 0, 200).empty());
}

TEST_F(RestHandler, ReturnsChartPointsForRangeQuery) {
  PerformanceData data;
  data.timestamp = wallClockNanos();
  data.pnl = 4.5;
  data.position = -0.25;
  collector->recordPerformance("strategy", data);

  auto pnl = get("/api/v1/strategies/strategy/charts/pnl?range=30m");
  auto pnlBody = nlohmann::json::parse(pnl.body());
  ASSERT_EQ(pnl.result(), http::status::ok);
  ASSERT_EQ(pnlBody["data"].size(), 1);
  EXPECT_EQ(pnlBody["data"][0]["timestamp"], data.timestamp);
  EXPECT_DOUBLE_EQ(pnlBody["data"][0]["value"], 4.5);
  EXPECT_FALSE(pnlBody["data"][0].contains("label"));

  auto position = get("/api/v1/strategies/strategy/charts/position?range=30m");
  auto positionBody = nlohmann::json::parse(position.body());
  ASSERT_EQ(position.result(), http::status::ok);
  ASSERT_EQ(positionBody["data"].size(), 1);
  EXPECT_DOUBLE_EQ(positionBody["data"][0]["value"], -0.25);
}

TEST_F(RestHandler, DefaultsChartWindowToOneHour) {
  constexpr uint64_t nanosPerSecond = 1000000000ULL;
  const auto now = wallClockNanos();

  PerformanceData stale;
  stale.timestamp = now - 2 * 3600 * nanosPerSecond;
  PerformanceData recent;
  recent.timestamp = now - 60 * nanosPerSecond;
  collector->recordPerformance("strategy", stale);
  collector->recordPerformance("strategy", recent);

  auto response = get("/api/v1/strategies/strategy/charts/pnl");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  ASSERT_EQ(body["data"].size(), 1);
  EXPECT_EQ(body["data"][0]["timestamp"], recent.timestamp);
}

TEST_F(RestHandler, RejectsMalformedChartRanges) {
  for (const auto* query :
       {"range=1x", "range=m", "range=1.5h", "range=", "range=-1h"}) {
    auto response =
        get(std::string("/api/v1/strategies/strategy/charts/pnl?") + query);
    auto body = nlohmann::json::parse(response.body());

    EXPECT_EQ(response.result(), http::status::bad_request) << query;
    EXPECT_EQ(body["error"], "Invalid chart range") << query;
  }
}

TEST_F(RestHandler, RejectsInvalidAndReversedChartBounds) {
  EXPECT_EQ(get("/api/v1/strategies/strategy/charts/pnl?start=abc").result(),
            http::status::bad_request);
  EXPECT_EQ(
      get("/api/v1/strategies/strategy/charts/pnl?start=200&end=100").result(),
      http::status::bad_request);
}

TEST_F(RestHandler, ExplicitChartBoundsTakePrecedenceOverRange) {
  PerformanceData old;
  old.timestamp = 100;
  old.pnl = 1.0;
  PerformanceData recent;
  recent.timestamp = wallClockNanos();
  recent.pnl = 2.0;
  collector->recordPerformance("strategy", old);
  collector->recordPerformance("strategy", recent);

  auto response =
      get("/api/v1/strategies/strategy/charts/pnl?range=1h&start=100&end=100");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  ASSERT_EQ(body["data"].size(), 1);
  EXPECT_EQ(body["data"][0]["timestamp"], 100);
  EXPECT_DOUBLE_EQ(body["data"][0]["value"], 1.0);
}

TEST_F(RestHandler, RejectsUnsupportedChartMetric) {
  auto response = get("/api/v1/strategies/strategy/charts/volume?range=1h");
  auto body = nlohmann::json::parse(response.body());

  EXPECT_EQ(response.result(), http::status::bad_request);
  EXPECT_EQ(body["error"], "Unsupported chart metric: volume");
}

TEST_F(RestHandler, ReturnsEmptyArrayForChartRangeWithNoMatches) {
  PerformanceData data;
  data.timestamp = 100;
  collector->recordPerformance("strategy", data);

  auto response =
      get("/api/v1/strategies/strategy/charts/pnl?start=200&end=300");
  auto body = nlohmann::json::parse(response.body());

  ASSERT_EQ(response.result(), http::status::ok);
  EXPECT_TRUE(body["data"].is_array());
  EXPECT_TRUE(body["data"].empty());
}

} // namespace pinnacle::visualization
