#include "pch.h"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    _putenv_s("UNITTEST_RUNNING", "1");
    // ::testing::GTEST_FLAG(filter) = "Test_Cases1*";
    return RUN_ALL_TESTS();
}