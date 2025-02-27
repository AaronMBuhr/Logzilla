#include "pch.h"

// This file contains the main entry point for the Google Test framework
// The actual tests are in separate files organized by component

// Simple sanity check test to verify the test framework is working
TEST(SanityCheck, BasicAssertions) {
  EXPECT_EQ(1, 1);
  EXPECT_TRUE(true);
  EXPECT_FALSE(false);
  EXPECT_STREQ("hello", "hello");
}

// Main entry point for the tests
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}