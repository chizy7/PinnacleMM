#include "../../strategies/basic/BasicMarketMaker.h"
#include "../../strategies/config/StrategyConfig.h"
#include <gtest/gtest.h>

// A simple placeholder test to ensure the file compiles
TEST(StrategyTest, PlaceholderTest) {
    // Simple assertion to make the test pass
    ASSERT_TRUE(true);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}