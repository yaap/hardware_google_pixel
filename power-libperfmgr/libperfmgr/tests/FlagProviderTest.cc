/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <perfmgr/FlagProvider.h>

namespace android::perfmgr {

class FlagProviderTest : public ::testing::Test {
  public:
    FlagProvider *mProvider = &FlagProvider::GetInstance();
    virtual void SetUp() {
        FlagProvider::TearDown();
        FlagProvider::SetUp();
    }
};

TEST_F(FlagProviderTest, initializeProvider) {}

TEST_F(FlagProviderTest, overrideValue) {
    bool baseValue = powerhal::flags::test_flag();
    mProvider->OverrideValue(powerhal::flags::test_flag, !baseValue);
    EXPECT_EQ(powerhal::flags::test_flag(), !baseValue);
}

TEST_F(FlagProviderTest, overrideValueDroppable) {
    bool baseValue = powerhal::flags::test_flag();
    mProvider->OverrideValue(powerhal::flags::test_flag, !baseValue);
    EXPECT_EQ(powerhal::flags::test_flag(), !baseValue);
    mProvider->DropOverride(powerhal::flags::test_flag);
    EXPECT_EQ(powerhal::flags::test_flag(), baseValue);
    mProvider->OverrideValue(powerhal::flags::test_flag, !baseValue);
    mProvider->ClearOverrides();
    EXPECT_EQ(powerhal::flags::test_flag(), baseValue);
}

TEST_F(FlagProviderTest, acquireGetterFromString) {
    std::string flagName = "test_flag";
    auto getter = mProvider->GetterFromString(flagName);
    EXPECT_EQ(getter, powerhal::flags::test_flag);
}

TEST_F(FlagProviderTest, setUpTearDown) {
    bool baseValue = powerhal::flags::test_flag();
    mProvider->OverrideValue(powerhal::flags::test_flag, !baseValue);
    FlagProvider::TearDown();
    EXPECT_EQ(powerhal::flags::test_flag(), baseValue);
    mProvider = &FlagProvider::GetInstance();
    mProvider->OverrideValue(powerhal::flags::test_flag, !baseValue);
    EXPECT_EQ(powerhal::flags::test_flag(), !baseValue);
}

}  // namespace android::perfmgr
