/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include <pixelstats/MmMetricsReporter.h>
#include <sys/stat.h>
#include <unistd.h>

#include "MmMetricsGoldenAtomFieldTypes.h"
#include "MmMetricsGoldenResults.h"
#include "MockMmMetricsReporter.h"
#include "VendorAtomIntValueUtil.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using mm_metrics_atom_field_test_golden_results::MmMetricsGcmaPerDayHistogram_field_types;
using mm_metrics_atom_field_test_golden_results::MmMetricsGcmaPerDaySimple_field_types;
using mm_metrics_atom_field_test_golden_results::MmMetricsGcmaPerHour_field_types;
using mm_metrics_atom_field_test_golden_results::MmMetricsOomGroupMemUsage_field_types;
using mm_metrics_atom_field_test_golden_results::PixelMmMetricsPerDay_field_types;
using mm_metrics_atom_field_test_golden_results::PixelMmMetricsPerHour_field_types;

using mm_metrics_reporter_test_golden_result::MmMetricsGcmaPerDayHistogram_golden;
using mm_metrics_reporter_test_golden_result::MmMetricsGcmaPerDaySimple_golden;
using mm_metrics_reporter_test_golden_result::MmMetricsGcmaPerHour_golden;
using mm_metrics_reporter_test_golden_result::MmMetricsOomGroupMemUsage_golden;
using mm_metrics_reporter_test_golden_result::PixelMmMetricsPerDay_golden;
using mm_metrics_reporter_test_golden_result::PixelMmMetricsPerHour_golden;

const char *data_base_path = "/data/local/tmp/test/pixelstats_mm_test/data";

TEST(MmMetricsReporterTest, MmMetricsPerHourAtomFieldOffsetTypeTest) {
    int i = -1;
    uint64_t golden_result;
    int field_type;
    std::vector<VendorAtomValue> values;
    MockMmMetricsReporter mreport;
    const std::string data_path0 = std::string(data_base_path) + "/test_data_0";
    const std::string data_path1 = std::string(data_base_path) + "/test_data_1";

    // Assert failure means the test case itself has a bug.
    ASSERT_EQ(ARRAY_SIZE(PixelMmMetricsPerHour_golden),
              ARRAY_SIZE(PixelMmMetricsPerHour_field_types));

    /**
     * In test code we use setBasePath() to read different data sets for simulating
     * different timing reads of a sysfs node.
     */

    /**
     * aggregatePixelMmMetricsPer5Min() aggregates PSI into max, min, and avg.
     * For the regular code, it will be called 12 times per hour (i.e. once per 5min)
     * For test code we do 6 times: enough for testing.
     * e.g. here average  = (3 x data0 + 3 x data1) / 6 == avg of data 0, 1
     * The following sequence simulate regular code obtaining sysfs nodes into
     * values[] array (i.e. atom), ready to be sent to the server
     */
    mreport.setBasePath(data_path0);
    mreport.aggregatePixelMmMetricsPer5Min();
    mreport.aggregatePixelMmMetricsPer5Min();
    mreport.aggregatePixelMmMetricsPer5Min();
    mreport.setBasePath(data_path1);
    mreport.aggregatePixelMmMetricsPer5Min();
    mreport.aggregatePixelMmMetricsPer5Min();
    mreport.aggregatePixelMmMetricsPer5Min();

    // other fields from data set #0
    mreport.setBasePath(data_path0);
    values = mreport.genPixelMmMetricsPerHour();

    // Validate the atom: compare with golden results
    EXPECT_EQ(values.size(), ARRAY_SIZE(PixelMmMetricsPerHour_field_types));
    for (auto const &v : values) {
        i++;
        golden_result = PixelMmMetricsPerHour_golden[i];
        field_type = PixelMmMetricsPerHour_field_types[i];
        if (golden_result == -1)
            continue;  // no need to test (e.g. deprecated field)

        EXPECT_EQ(static_cast<int>(v.getTag()), field_type) << "type mismatch at offset " << i;
        EXPECT_EQ(getVendorAtomIntValue(v), golden_result) << "value mismatch at offset " << i;
    }
}

TEST(MmMetricsReporterTest, MmMetricsPerDayAtomFieldOffsetTypeTest) {
    int i = -1;
    uint64_t golden_result;
    int field_type;
    std::vector<VendorAtomValue> values;
    MockMmMetricsReporter mreport;
    const std::string data_path0 = std::string(data_base_path) + "/test_data_0";
    const std::string data_path1 = std::string(data_base_path) + "/test_data_1";

    // Assert failure means the test case itself has a bug.
    ASSERT_EQ(ARRAY_SIZE(PixelMmMetricsPerDay_golden),
              ARRAY_SIZE(PixelMmMetricsPerDay_field_types));

    mreport.setBasePath(data_path0);
    values = mreport.genPixelMmMetricsPerDay();

    // PixelMmMetricsPerDay calculatd the difference of consecutive readings.
    // So, it will not send values[] at the 1st read. (i.e. empty for the 1st read)
    EXPECT_EQ(values.size(), 0);
    values.clear();

    mreport.setBasePath(data_path1);
    values = mreport.genPixelMmMetricsPerDay();

    // Per Day metrics (diffs) should be calculated, values[] will be non-empty now.
    // number of data should be the same as the number of fields in the atom.
    EXPECT_EQ(values.size(), ARRAY_SIZE(PixelMmMetricsPerDay_field_types));
    for (auto const &v : values) {
        i++;
        EXPECT_LT(i, ARRAY_SIZE(PixelMmMetricsPerDay_field_types));

        golden_result = PixelMmMetricsPerDay_golden[i];
        field_type = PixelMmMetricsPerDay_field_types[i];
        if (golden_result == -1)
            continue;  // no need to test (e.g. deprecated field)

        EXPECT_EQ(static_cast<int>(v.getTag()), field_type) << "type mismatch at offset " << i;
        EXPECT_EQ(getVendorAtomIntValue(v), golden_result) << "value mismatch at offset " << i;
    }
}

TEST(MmMetricsReporterTest, MmMetricsOomGroupMemUsageSuccess) {
    constexpr int kNumTests = 2;
    MockMmMetricsReporter mreport;
    const std::string data_path[kNumTests] = {
            std::string(data_base_path) + "/test_data_0",
            std::string(data_base_path) + "/test_data_1",
    };
    std::vector<MmMetricsReporter::OomGroupMemUsage> ogusage;
    int32_t og_metric_uid[kNumTests];
    auto &golden = MmMetricsOomGroupMemUsage_golden;
    auto &gold_ftype = MmMetricsOomGroupMemUsage_field_types;

    constexpr int kNumFields = ARRAY_SIZE(MmMetricsOomGroupMemUsage_field_types);
    constexpr int kNumLines = ARRAY_SIZE(golden[0]);

    ASSERT_LT(kNumLines, 100);

    // Check testcase consistency (if fail, the test case itself has some bug)
    ASSERT_EQ(ARRAY_SIZE(golden), kNumTests);
    ASSERT_EQ(ARRAY_SIZE(golden[1]), kNumLines);
    ASSERT_EQ(ARRAY_SIZE(MmMetricsOomGroupMemUsage_field_types), kNumFields);

    for (int i = 0; i < kNumTests; i++) {
        for (int j = 0; j < kNumLines; j++) {
            // golden result does not have UID field, which is date/time based unique ID.
            ASSERT_EQ(ARRAY_SIZE(golden[i][j]), kNumFields - 1);
        }
    }

    for (int test_iteration = 0; test_iteration < kNumTests; ++test_iteration) {
        // setup
        mreport.setBasePath(data_path[test_iteration]);

        // --- start test ---
        ASSERT_TRUE(mreport.readMmProcessUsageByOomGroup(&ogusage));
        ASSERT_EQ(ogusage.size(), kNumLines);

        int line = 0;
        for (const auto &u : ogusage) {
            std::vector<VendorAtomValue> values =
                    mreport.genMmProcessUsageByOomGroupSnapshotAtom(u);
            int32_t &uid = og_metric_uid[test_iteration];

            // check size
            ASSERT_EQ(values.size(), kNumFields)
                    << "Size mismatch: test# " << test_iteration << " line " << line;

            if (line == 0) {
                uid = getVendorAtomIntValue(values[0]);
            } else {
                // check UID
                EXPECT_EQ(getVendorAtomIntValue(values[0]), uid)
                        << "value mismatch: test# " << test_iteration << " line " << line
                        << " field 0";
            }

            for (int field = 1; field < kNumFields; ++field) {
                // check types
                EXPECT_EQ(static_cast<int>(values[field].getTag()), gold_ftype[field])
                        << "type mismatch: test# " << test_iteration << " line " << line
                        << " field " << field;

                if (static_cast<int>(values[field].getTag()) != gold_ftype[field])
                    continue;  // no checking values when the type is already wrong.

                // check values
                EXPECT_EQ(getVendorAtomIntValue(values[field]),
                          golden[test_iteration][line][field - 1])
                        << "value mismatch: test# " << test_iteration << " line " << line
                        << " field " << field;
            }
            line++;
        }
        // --- end test ---
    }

    // metric_uid must be unique
    EXPECT_NE(og_metric_uid[0], og_metric_uid[1]);
}

TEST(MmMetricsReporterTest, MmMetricsOomGroupMemUsageFailFileNotFound) {
    constexpr int kNumTests = 2;
    MockMmMetricsReporter mreport;
    const std::string data_path = std::string(data_base_path) + "/nonexisting_dir";
    std::vector<MmMetricsReporter::OomGroupMemUsage> ogusage;
    int32_t uid;

    // setup
    mreport.setBasePath(data_path);

    // --- start test ---
    ASSERT_FALSE(mreport.readMmProcessUsageByOomGroup(&ogusage));
    ASSERT_EQ(ogusage.size(), 0);
}

static bool file_exists(const char *const path) {
    struct stat sbuf;

    return (stat(path, &sbuf) == 0);
}

TEST(MmMetricsReporterTest, MmMetricsOomGroupMemUsageMultipleFailCases) {
    constexpr int kNumTests = 8;
    MockMmMetricsReporter mreport;
    const std::string data_path[kNumTests] = {
            std::string(data_base_path) + "/test_data_oom_usage_fail/1",
            std::string(data_base_path) + "/test_data_oom_usage_fail/2",
            std::string(data_base_path) + "/test_data_oom_usage_fail/3",
            std::string(data_base_path) + "/test_data_oom_usage_fail/4",
            std::string(data_base_path) + "/test_data_oom_usage_fail/5",
            std::string(data_base_path) + "/test_data_oom_usage_fail/6",
            std::string(data_base_path) + "/test_data_oom_usage_fail/7",
            std::string(data_base_path) + "/test_data_oom_usage_fail/8",
    };
    const char *file = "oom_mm_usage";
    std::vector<MmMetricsReporter::OomGroupMemUsage> ogusage;

    for (int test_iteration = 0; test_iteration < kNumTests; ++test_iteration) {
        // setup
        mreport.setBasePath(data_path[test_iteration]);

        // check file exist, otherwise it is testing "file not found" rather than the desired test
        ASSERT_TRUE(file_exists((data_path[test_iteration] + "/" + file).c_str()));

        // --- start test ---
        ASSERT_FALSE(mreport.readMmProcessUsageByOomGroup(&ogusage))
                << "Iteration " << test_iteration << ": test fail.";
        ASSERT_EQ(ogusage.size(), 0) << "Iteration " << test_iteration << ": test fail.";
    }
}

TEST(MmMetricsReporterTest, MmMetricsGcmaPerHourSuccess) {
    MockMmMetricsReporter mreport;
    const std::string data_path = std::string(data_base_path) + "/test_data_0";
    auto &golden = MmMetricsGcmaPerHour_golden;
    auto &gold_ftype = MmMetricsGcmaPerHour_field_types;

    constexpr int kNumFields = ARRAY_SIZE(gold_ftype);
    constexpr int kNumLines = ARRAY_SIZE(golden);

    // Check testcase consistency (if fail, the test case itself has some bug)
    ASSERT_EQ(kNumFields, kNumLines);

    // setup
    mreport.setBasePath(data_path);

    // --- start test ---
    std::vector<VendorAtomValue> values = mreport.readAndGenGcmaPerHour();

    // check size
    ASSERT_EQ(values.size(), kNumLines);

    for (int field = 0; field < kNumFields; ++field) {
        // check type
        EXPECT_EQ(static_cast<int>(values[field].getTag()), gold_ftype[field])
                << "type mismatch @ field #" << field;

        if (static_cast<int>(values[field].getTag()) != gold_ftype[field])
            continue;  // no checking the value when the type is wrong.

        // check value
        EXPECT_EQ(getVendorAtomIntValue(values[field]), golden[field])
                << "value mismatch @ field #" << field;
    }
}

TEST(MmMetricsReporterTest, MmMetricsGcmaPerDaySuccess) {
    MockMmMetricsReporter mreport;
    const std::string data_path = std::string(data_base_path) + "/test_data_0";
    auto &golden_simple = MmMetricsGcmaPerDaySimple_golden;
    auto &golden_histogram = MmMetricsGcmaPerDayHistogram_golden;

    auto &gold_simple_ftype = MmMetricsGcmaPerDaySimple_field_types;
    auto &gold_histogram_ftype = MmMetricsGcmaPerDayHistogram_field_types;

    constexpr int kNumSimpleValues = 4;
    constexpr int kNumHistogramValues = 4;
    // total field num in atom values need to count the histogram array as one.
    constexpr int kNumAtomValues = kNumSimpleValues + 1;

    // Check testcase consistency (if fail, the test case itself has some bug)
    ASSERT_EQ(ARRAY_SIZE(golden_simple), kNumSimpleValues);
    ASSERT_EQ(ARRAY_SIZE(golden_histogram), kNumHistogramValues);
    ASSERT_EQ(ARRAY_SIZE(gold_simple_ftype), kNumSimpleValues + 1);  // count the last array type
    ASSERT_EQ(ARRAY_SIZE(gold_histogram_ftype), kNumHistogramValues);

    // setup
    mreport.setBasePath(data_path);

    // --- start test ---
    std::vector<VendorAtomValue> values = mreport.readAndGenGcmaPerDay();

    /*
     * check size +1:
     * Histogram in the form of a vector in the last element of 'Simple' value array.
     */
    ASSERT_EQ(values.size(), kNumAtomValues);

    // check 'simple' values
    for (int field = 0; field < kNumSimpleValues; ++field) {
        // check type
        EXPECT_EQ(static_cast<int>(values[field].getTag()), gold_simple_ftype[field])
                << "type mismatch @ field #" << field;

        if (static_cast<int>(values[field].getTag()) != gold_simple_ftype[field])
            continue;  // no checking the value when the type is wrong.

        if (field == kNumAtomValues - 1)
            continue;  // same as break.  The last one is an array, compare type only here.

        EXPECT_EQ(getVendorAtomIntValue(values[field]), golden_simple[field])
                << "value mismatch @ field #" << field;
    }

    // check array validity
    auto &arrAtomValue = values[kNumAtomValues - 1];
    const std::optional<std::vector<int64_t>> &repeatedLongValue =
            arrAtomValue.get<VendorAtomValue::repeatedLongValue>();
    ASSERT_TRUE(repeatedLongValue.has_value());

    // check array size
    ASSERT_EQ(repeatedLongValue.value().size(), kNumHistogramValues);

    // check array values
    for (int field = 0; field < kNumHistogramValues; ++field) {
        EXPECT_EQ(repeatedLongValue.value()[field], golden_histogram[field]);
    }
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
