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

#define LOG_TAG "pixelstats: ThermalStats"

#include <aidl/android/frameworks/stats/IStats.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android/binder_manager.h>
#include <hardware/google/pixel/pixelstats/pixelatoms.pb.h>
#include <pixelstats/JsonConfigUtils.h>
#include <pixelstats/ThermalStatsReporter.h>
#include <utils/Log.h>

#include <cinttypes>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using aidl::android::frameworks::stats::VendorAtom;
using aidl::android::frameworks::stats::VendorAtomValue;
using android::base::ReadFileToString;
using android::base::WriteStringToFile;
using android::hardware::google::pixel::PixelAtoms::ThermalDfsStats;

namespace {

enum class ThermalStatsErrorCode : int32_t {
    ERR_OK = 0,
    ERR_READ_FAIL = -1,
    ERR_RESET_FAIL = -2,
    ERR_INVALID_DATA = -3
};

/**
 * Calculates the stat value to report and the next previous value.
 *
 * Handles logic based on read/reset success, current vs previous values as:
 * 1) Read = ERR
 *  - Value to report = ERR_READ_FAIL
 *  - Updated prev value = Prev (continue with previous value)
 * 2) Read = OK AND Reset = ERR
 *  a) Current = 0
 *    - Value to report = ERR_RESET_FAIL (for better debuggability)
 *    - Updated prev value = Current
 *  b) Current >= Prev
 *    - Value to report = Current - Prev
 *    - Updated prev value = Current
 *  c) Current < Prev
 *    - Value to report = ERR_INVALID_DATA
 *    - Updated prev value = 0
 * 3) Read = OK AND Reset = OK
 *  a) Current >= Prev
 *    - Value to report = Current - Prev
 *    - Updated prev value = 0
 *  b) Current < Prev
 *    - Value to report = ERR_INVALID_DATA
 *    - Updated prev value = 0
 *
 * @param current_value: The value just read from the source.
 * @param previous_value: The value stored from the previous reporting cycle.
 * @param read_status: Status of the read operation.
 * @param reset_status: Status of the reset operation.
 * @return std::pair<int64_t, int64_t> Pair containing:
 *    first:  The value to report (can be a count or an error code).
 *    second: The updated previous value to store for the next cycle.
 */
std::pair<int64_t, int64_t> calculateReportValueAndNewPrev(int64_t current_value,
                                                           int64_t previous_value,
                                                           ThermalStatsErrorCode read_status,
                                                           ThermalStatsErrorCode reset_status) {
    int64_t value_to_report;
    int64_t updated_prev_value;
    if (read_status != ThermalStatsErrorCode::ERR_OK) {
        value_to_report = static_cast<int64_t>(read_status);
        updated_prev_value = previous_value;
    } else if (reset_status != ThermalStatsErrorCode::ERR_OK) {
        if (current_value == 0) {
            value_to_report = static_cast<int64_t>(ThermalStatsErrorCode::ERR_RESET_FAIL);
            updated_prev_value = current_value;
        } else {
            if (current_value >= previous_value) {
                value_to_report = current_value - previous_value;
                updated_prev_value = current_value;
            } else {
                value_to_report = static_cast<int64_t>(ThermalStatsErrorCode::ERR_INVALID_DATA);
                updated_prev_value = 0;
            }
        }
    } else {
        if (current_value >= previous_value) {
            value_to_report = current_value - previous_value;
        } else {
            value_to_report = static_cast<int64_t>(ThermalStatsErrorCode::ERR_INVALID_DATA);
        }
        updated_prev_value = 0;
    }
    return {value_to_report, updated_prev_value};
}

}  // namespace

ThermalStatsReporter::ThermalStatsReporter(const Json::Value &configData) {
    parseThermalTjTripCounterConfig(configData);
}

void ThermalStatsReporter::parseThermalTjTripCounterConfig(const Json::Value &configData) {
    if (!configData.isMember("ThermalTjTripCounterConfig")) {
        ALOGI("No thermal Tj trip counter config found.");
        return;
    }

    Json::Value tjTripCountConfig = configData["ThermalTjTripCounterConfig"];
    for (Json::Value::ArrayIndex i = 0; i < tjTripCountConfig.size(); i++) {
        std::string name = tjTripCountConfig[i]["Name"].asString();
        if (name.empty() || !kThermalZoneStrToEnum.count(name)) {
            ALOGE("Thermal Tj trip counter config [%d] with invalid sensor %s", i, name.c_str());
        }
        std::vector<int> trip_numbers = readIntVectorFromJson(tjTripCountConfig[i]["TripNumbers"]);
        for (size_t trip_idx = 0; trip_idx < trip_numbers.size(); trip_idx++) {
            if (trip_numbers[trip_idx] < 0 || trip_numbers[trip_idx] >= kMaxTripNumber) {
                ALOGE("Thermal Tj trip counter config [%d], trip at idx %zu has invalid trip "
                      "number "
                      "%d",
                      i, trip_idx, trip_numbers[trip_idx]);
                continue;
            }
        }
        std::string read_path = getCStringOrDefault(tjTripCountConfig[i], "ReadPath");
        std::string reset_path = getCStringOrDefault(tjTripCountConfig[i], "ResetPath");
        if (read_path.empty() || reset_path.empty()) {
            ALOGE("Thermal Tj trip counter config [%d] for sensor %s has invalid read: %s or "
                  "reset: %s "
                  "path",
                  i, name.c_str(), read_path.c_str(), reset_path.c_str());
            continue;
        }
        tz_trip_count_config_[kThermalZoneStrToEnum.at(name)] = {
                .trip_numbers = trip_numbers,
                .prev_trip_counts = std::vector<int64_t>(kMaxTripNumber, 0),
                .read_path = read_path,
                .reset_path = reset_path,
        };
    }
}

bool ThermalStatsReporter::readAllTripCount(const std::string &path, std::vector<int64_t> *trips) {
    std::string file_contents;

    if (path.empty()) {
        ALOGE("Empty path");
        return false;
    }

    if (!ReadFileToString(path.c_str(), &file_contents)) {
        ALOGE("Unable to read %s - %s", path.c_str(), strerror(errno));
        return false;
    }

    trips->resize(kMaxTripNumber, 0);
    if (sscanf(file_contents.c_str(),
               "%" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64 " %" SCNd64
               " %" SCNd64,
               &(*trips)[0], &(*trips)[1], &(*trips)[2], &(*trips)[3], &(*trips)[4], &(*trips)[5],
               &(*trips)[6], &(*trips)[7]) < kMaxTripNumber) {
        ALOGE("Unable to parse trip_counters %s from file %s", file_contents.c_str(), path.c_str());
        return false;
    }

    return true;
}

bool ThermalStatsReporter::readDfsCount(const std::string &path, int64_t *val) {
    std::vector<int64_t> trips;
    if (!readAllTripCount(path, &trips)) {
        return false;
    }
    *val = trips[6];
    return true;
}

bool ThermalStatsReporter::captureThermalDfsStats(
        const std::vector<std::string> &thermal_stats_paths, struct ThermalDfsCounts *pcur_data) {
    bool report_stats = false;
    std::string path;

    if (thermal_stats_paths.size() < kNumOfThermalDfsStats) {
        ALOGE("Number of thermal stats paths (%zu) is less than expected (%d)",
              thermal_stats_paths.size(), kNumOfThermalDfsStats);
        return false;
    }

    path = thermal_stats_paths[ThermalDfsStats::kBigDfsCountFieldNumber - kVendorAtomOffset];
    if (!readDfsCount(path, &(pcur_data->big_count))) {
        pcur_data->big_count = prev_data.big_count;
    } else {
        report_stats |= (pcur_data->big_count > prev_data.big_count);
    }

    path = thermal_stats_paths[ThermalDfsStats::kMidDfsCountFieldNumber - kVendorAtomOffset];
    if (!readDfsCount(path, &(pcur_data->mid_count))) {
        pcur_data->mid_count = prev_data.mid_count;
    } else {
        report_stats |= (pcur_data->mid_count > prev_data.mid_count);
    }

    path = thermal_stats_paths[ThermalDfsStats::kLittleDfsCountFieldNumber - kVendorAtomOffset];
    if (!readDfsCount(path, &(pcur_data->little_count))) {
        pcur_data->little_count = prev_data.little_count;
    } else {
        report_stats |= (pcur_data->little_count > prev_data.little_count);
    }

    path = thermal_stats_paths[ThermalDfsStats::kGpuDfsCountFieldNumber - kVendorAtomOffset];
    if (!readDfsCount(path, &(pcur_data->gpu_count))) {
        pcur_data->gpu_count = prev_data.gpu_count;
    } else {
        report_stats |= (pcur_data->gpu_count > prev_data.gpu_count);
    }

    path = thermal_stats_paths[ThermalDfsStats::kTpuDfsCountFieldNumber - kVendorAtomOffset];
    if (!readDfsCount(path, &(pcur_data->tpu_count))) {
        pcur_data->tpu_count = prev_data.tpu_count;
    } else {
        report_stats |= (pcur_data->tpu_count > prev_data.tpu_count);
    }

    path = thermal_stats_paths[ThermalDfsStats::kAurDfsCountFieldNumber - kVendorAtomOffset];
    if (!readDfsCount(path, &(pcur_data->aur_count))) {
        pcur_data->aur_count = prev_data.aur_count;
    } else {
        report_stats |= (pcur_data->aur_count > prev_data.aur_count);
    }

    return report_stats;
}

void ThermalStatsReporter::logThermalDfsStats(const std::shared_ptr<IStats> &stats_client,
                                              const std::vector<std::string> &thermal_stats_paths) {
    struct ThermalDfsCounts cur_data = prev_data;

    if (!captureThermalDfsStats(thermal_stats_paths, &cur_data)) {
        prev_data = cur_data;
        ALOGI("No update found for thermal stats");
        return;
    }

    VendorAtomValue tmp;
    int64_t max_dfs_count = static_cast<int64_t>(INT32_MAX);
    int dfs_count;
    std::vector<VendorAtomValue> values(kNumOfThermalDfsStats);

    dfs_count = std::min<int64_t>(cur_data.big_count - prev_data.big_count, max_dfs_count);
    tmp.set<VendorAtomValue::intValue>(dfs_count);
    values[ThermalDfsStats::kBigDfsCountFieldNumber - kVendorAtomOffset] = tmp;

    dfs_count = std::min<int64_t>(cur_data.mid_count - prev_data.mid_count, max_dfs_count);
    tmp.set<VendorAtomValue::intValue>(dfs_count);
    values[ThermalDfsStats::kMidDfsCountFieldNumber - kVendorAtomOffset] = tmp;

    dfs_count = std::min<int64_t>(cur_data.little_count - prev_data.little_count, max_dfs_count);
    tmp.set<VendorAtomValue::intValue>(dfs_count);
    values[ThermalDfsStats::kLittleDfsCountFieldNumber - kVendorAtomOffset] = tmp;

    dfs_count = std::min<int64_t>(cur_data.gpu_count - prev_data.gpu_count, max_dfs_count);
    tmp.set<VendorAtomValue::intValue>(dfs_count);
    values[ThermalDfsStats::kGpuDfsCountFieldNumber - kVendorAtomOffset] = tmp;

    dfs_count = std::min<int64_t>(cur_data.tpu_count - prev_data.tpu_count, max_dfs_count);
    tmp.set<VendorAtomValue::intValue>(dfs_count);
    values[ThermalDfsStats::kTpuDfsCountFieldNumber - kVendorAtomOffset] = tmp;

    dfs_count = std::min<int64_t>(cur_data.aur_count - prev_data.aur_count, max_dfs_count);
    tmp.set<VendorAtomValue::intValue>(dfs_count);
    values[ThermalDfsStats::kAurDfsCountFieldNumber - kVendorAtomOffset] = tmp;

    prev_data = cur_data;

    ALOGD("Report updated thermal metrics to stats service");
    // Send vendor atom to IStats HAL
    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kThermalDfsStats,
                        .values = std::move(values)};
    const ndk::ScopedAStatus ret = stats_client->reportVendorAtom(event);
    if (!ret.isOk())
        ALOGE("Unable to report thermal DFS stats to Stats service");
}

void ThermalStatsReporter::logTjTripCountStats(const std::shared_ptr<IStats> &stats_client) {
    if (tz_trip_count_config_.empty())
        return;

    for (auto &[tz, trip_count_config] : tz_trip_count_config_) {
        ThermalStatsErrorCode read_status, reset_status;
        std::vector<int64_t> trips;

        if (readAllTripCount(trip_count_config.read_path, &trips)) {
            read_status = ThermalStatsErrorCode::ERR_OK;
            if (WriteStringToFile(std::to_string(0), trip_count_config.reset_path)) {
                reset_status = ThermalStatsErrorCode::ERR_OK;
            } else {
                ALOGE("Failed to write to file %s", trip_count_config.reset_path.c_str());
                reset_status = ThermalStatsErrorCode::ERR_RESET_FAIL;
            }
        } else {
            ALOGE("Unable to read trip count from %s", trip_count_config.read_path.c_str());
            // Resize needed before assigning error codes. Value is meaningless.
            trips.resize(kMaxTripNumber, 0);
            read_status = ThermalStatsErrorCode::ERR_READ_FAIL;
            // Reset fails if read fails
            reset_status = ThermalStatsErrorCode::ERR_READ_FAIL;
        }

        for (const auto &trip_number : trip_count_config.trip_numbers) {
            int64_t &prev_trip_count_ref = trip_count_config.prev_trip_counts[trip_number];

            auto [trip_count_to_report, updated_prev_value] = calculateReportValueAndNewPrev(
                    trips[trip_number], prev_trip_count_ref, read_status, reset_status);

            // Update the stored previous value
            prev_trip_count_ref = updated_prev_value;

            // Skip reporting if the calculated count is 0 (and not an error code)
            if (trip_count_to_report == 0) {
                ALOGD("Skipping logging Tj trip count for tz: %d, trip: %d with count: 0", tz,
                      trip_number);
                continue;
            }

            std::vector<VendorAtomValue> values(3);
            values[0].set<VendorAtomValue::intValue>(tz);
            values[1].set<VendorAtomValue::intValue>(trip_number);
            // Clamp the value to INT32_MAX before reporting
            values[2].set<VendorAtomValue::intValue>(
                    static_cast<int32_t>(std::min<int64_t>(trip_count_to_report, INT32_MAX)));

            VendorAtom event = {.reverseDomainName = "",
                                .atomId = PixelAtoms::Atom::kThermalTjTripCountReported,
                                .values = std::move(values)};
            ALOGI("Reported thermal Tj trip count metrics for tz: %d, trip: %d, count: %" PRId64,
                  tz, trip_number, trip_count_to_report);

            const ndk::ScopedAStatus ret = stats_client->reportVendorAtom(event);
            if (!ret.isOk()) {
                ALOGE("Unable to report thermal Tj trip count stats to Stats service");
            }
        }
    }
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
