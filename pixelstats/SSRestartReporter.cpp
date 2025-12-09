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

#define LOG_TAG "pixelstats: SSRestartReporter"

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <hardware/google/pixel/pixelstats/pixelatoms.pb.h>
#include <log/log.h>
#include <pixelstats/SSRestartReporter.h>
#include <pixelstats/StatsHelper.h>

#include <chrono>
#include <cinttypes>
#include <filesystem>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using aidl::android::frameworks::stats::VendorAtom;
using android::base::ReadFileToString;
using android::hardware::google::pixel::PixelAtoms::SubsystemRestartCrashReported;

const std::string btime_prefix = "btime ";
const std::string crash_reason_prefix = "crash_reason: ";
const std::string crash_count_prefix = "crash_count: ";
const std::string proc_stat = "/proc/stat";
const int BOOT_TIME_MARGIN = 60;

SSRestartReporter::SSRestartReporter() {}

int64_t SSRestartReporter::getBootTime() {
    std::string proc_stat_contents;
    if (!ReadFileToString(proc_stat.c_str(), &proc_stat_contents)) {
        ALOGE("Failed to read %s", proc_stat.c_str());
        return 0;
    }

    std::istringstream stream(proc_stat_contents);
    std::string line;
    while (std::getline(stream, line)) {
        if (android::base::StartsWith(line, btime_prefix)) {
            std::string btime_string = line.substr(btime_prefix.size());
            btime_string = android::base::Trim(btime_string);
            int64_t btime;
            if (!android::base::ParseInt(btime_string, &btime)) {
                ALOGE("Unable to parse btime: %s", btime_string.c_str());
                return 0;
            }
            return btime;
        }
    }
    return 0;
}

void SSRestartReporter::reportFile(const std::shared_ptr<IStats> &stats_client,
                                   const std::string &path) {
    std::string file_contents;
    if (!ReadFileToString(path, &file_contents)) {
        ALOGE("Unable to read %s - %s", path.c_str(), strerror(errno));
        return;
    }

    std::string crash_reason;
    /* crash_count may be missing from the file, defaults to 1 */
    int crash_count = 1;
    std::istringstream stream(file_contents);
    std::string line;
    while (std::getline(stream, line)) {
        if (android::base::StartsWith(line, crash_reason_prefix)) {
            crash_reason = line.substr(crash_reason_prefix.size());
            crash_reason = android::base::Trim(crash_reason);
        } else if (android::base::StartsWith(line, crash_count_prefix)) {
            std::string count_str = line.substr(crash_count_prefix.size());
            count_str = android::base::Trim(count_str);
            if (!android::base::ParseInt(count_str, &crash_count)) {
                ALOGE("Unable to convert %s (from %s) to int - %s", count_str.c_str(), path.c_str(),
                      strerror(errno));
                return;
            }
        }
    }

    reportSSRestartStatsEvent(stats_client, crash_reason, crash_count);
}

void SSRestartReporter::logSSRestartStats(const std::shared_ptr<IStats> &stats_client,
                                          const std::string &ssrdump_dir) {

    if (last_scan_time_ == 0) {
        last_scan_time_ = getBootTime() - BOOT_TIME_MARGIN;
    }

    std::error_code ec;
    if (!std::filesystem::exists(ssrdump_dir, ec) ||
        !std::filesystem::is_directory(ssrdump_dir, ec)) {
        ALOGE("Error accessing %s: %s", ssrdump_dir.c_str(), ec.message().c_str());
        return;
    }

    for (const auto &entry : std::filesystem::directory_iterator(ssrdump_dir, ec)) {
        if (ec) {
            ALOGE("Error iterating directory %s: %s", ssrdump_dir.c_str(), ec.message().c_str());
            break;
        }
        if (entry.is_regular_file(ec) && entry.path().extension() == ".txt") {
            struct stat file_stat;
            /* Report only new crashes. */
            if (stat(entry.path().c_str(), &file_stat) == 0 &&
                file_stat.st_mtime > last_scan_time_) {
                reportFile(stats_client, entry.path());
            }
        }
    }

    last_scan_time_ = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
}

void SSRestartReporter::reportSSRestartStatsEvent(const std::shared_ptr<IStats> &stats_client,
                                                  const std::string &crash_reason,
                                                  int crash_count) {
    auto crash_reason_enum = SubsystemRestartCrashReported::UNKNOWN;

    if (android::base::StartsWith(crash_reason, "u100 power on err:")) {
        crash_reason_enum = SubsystemRestartCrashReported::U100_POWER_ON_ERR;
    } else if (crash_reason == "u100 coredump") {
        crash_reason_enum = SubsystemRestartCrashReported::U100_COREDUMP;
    } else if (crash_reason == "vpu crash") {
        crash_reason_enum = SubsystemRestartCrashReported::VPU_CRASH;
    } else {
        /* Do not report other crashes */
        return;
    }

    std::vector<VendorAtomValue> values(2);
    values[0].set<VendorAtomValue::intValue>(crash_reason_enum);
    values[1].set<VendorAtomValue::intValue>(crash_count);

    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kSubsystemRestartCrashReported,
                        .values = std::move(values)};
    reportVendorAtom(stats_client, event);
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
