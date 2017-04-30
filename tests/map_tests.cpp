#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "core/Map.h"

#include "interfaces_mocks.h"

using ::testing::ReturnRef;
using ::testing::Return;

TEST(Map, Constructor)
{
    Map map;
    EXPECT_EQ(map.GetWidth(), 0);
    EXPECT_EQ(map.GetHeight(), 0);
    EXPECT_EQ(map.GetDepth(), 0);
}

