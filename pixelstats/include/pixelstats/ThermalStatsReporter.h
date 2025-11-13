/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef HARDWARE_GOOGLE_PIXEL_PIXELSTATS_THERMALSTATSREPORTER_H
#define HARDWARE_GOOGLE_PIXEL_PIXELSTATS_THERMALSTATSREPORTER_H

#include <aidl/android/frameworks/stats/IStats.h>
#include <hardware/google/pixel/pixelstats/pixelatoms.pb.h>
#include <json/reader.h>

#include <string>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using aidl::android::frameworks::stats::VendorAtomValue;

/**
 * A class to upload Pixel Thermal Stats metrics
 */
class ThermalStatsReporter {
  public:
    ThermalStatsReporter(const Json::Value &configData);
    void logThermalDfsStats(const std::shared_ptr<IStats> &stats_client,
                            const std::vector<std::string> &thermal_stats_paths);
    void logTjTripCountStats(const std::shared_ptr<IStats> &stats_client);

  private:
    struct ThermalDfsCounts {
        int64_t big_count;
        int64_t mid_count;
        int64_t little_count;
        int64_t gpu_count;
        int64_t tpu_count;
        int64_t aur_count;
    };

    struct TripCountConfig {
        std::vector<int> trip_numbers;
        std::vector<int64_t> prev_trip_counts;
        std::string read_path;
        std::string reset_path;
    };

    // Proto messages are 1-indexed and VendorAtom field numbers start at 2, so
    // store everything in the values array at the index of the field number
    // -2.
    const int kVendorAtomOffset = 2;
    const int kNumOfThermalDfsStats = 6;
    const int kMaxTripNumber = 8;
    const std::unordered_map<std::string, PixelAtoms::TjThermalZone> kThermalZoneStrToEnum{
            {"BIG", PixelAtoms::BIG}, {"BIG_MID", PixelAtoms::BIG_MID},
            {"MID", PixelAtoms::MID}, {"LITTLE", PixelAtoms::LITTLE},
            {"GPU", PixelAtoms::GPU}, {"TPU", PixelAtoms::TPU},
            {"AUR", PixelAtoms::AUR}, {"ISP", PixelAtoms::ISP},
            {"MEM", PixelAtoms::MEM}, {"AOC", PixelAtoms::AOC}};

    struct ThermalDfsCounts prev_data;
    // Map of Tj thermal zone to the trip count config.
    std::unordered_map<PixelAtoms::TjThermalZone, TripCountConfig> tz_trip_count_config_;

    bool captureThermalDfsStats(const std::vector<std::string> &thermal_stats_paths,
                                struct ThermalDfsCounts *cur_data);
    bool readDfsCount(const std::string &path, int64_t *val);
    bool readAllTripCount(const std::string &path, std::vector<int64_t> *vals);
    void parseThermalTjTripCounterConfig(const Json::Value &configData);
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

#endif  // HARDWARE_GOOGLE_PIXEL_PIXELSTATS_THERMALSTATSREPORTER_H
