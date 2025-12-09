/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYEEPROMREPORTER_H
#define HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYEEPROMREPORTER_H

#include <cstdint>
#include <string>

#include <aidl/android/frameworks/stats/IStats.h>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using aidl::android::frameworks::stats::VendorAtomValue;

/**
 * A class to upload battery EEPROM metrics
 */
class BatteryEEPROMReporter {
  public:
    BatteryEEPROMReporter();
    void checkAndReport(const std::shared_ptr<IStats> &stats_client, const std::string &path);
    void checkAndReportGMSR(const std::shared_ptr<IStats> &stats_client, const std::vector<std::string> &paths);
    void checkAndReportMaxfgHistory(const std::shared_ptr<IStats> &stats_client,
                                    const std::string &path);

  private:
    int last_cycle_count = 0;

    /* P21+ history format */
    struct BatteryEEPROMPipelineRawFormat {
        uint16_t tempco;
        uint16_t rcomp0;
        uint8_t timer_h;
        unsigned fullcapnom:10;
        unsigned fullcaprep:10;
        unsigned mixsoc:6;
        unsigned vfsoc:6;
        unsigned maxvolt:4;
        unsigned minvolt:4;
        unsigned maxtemp:4;
        unsigned mintemp:4;
        unsigned maxchgcurr:4;
        unsigned maxdischgcurr:4;
    };

    struct BatteryEEPROMPipeline {
        int32_t cycle_cnt;
        int32_t full_cap;
        int32_t esr;
        int32_t rslow;
        int32_t soh;
        int32_t batt_temp;
        int32_t cutoff_soc;
        int32_t cc_soc;
        int32_t sys_soc;
        int32_t msoc;
        int32_t batt_soc;
        int32_t reserve;
        int32_t max_temp;
        int32_t min_temp;
        int32_t max_vbatt;
        int32_t min_vbatt;
        int32_t max_ibatt;
        int32_t min_ibatt;
        int32_t checksum;
        int32_t tempco;
        int32_t rcomp0;
        int32_t timer_h;
        int32_t full_rep;
        int32_t battery_pairing;
    };

    int64_t report_time_ = 0;
    int64_t report_time_maxfg_ = 0;
    int64_t getTimeSecs();

    void reportEvent(const std::shared_ptr<IStats> &stats_client,
                     const struct BatteryEEPROMPipeline &hist);
    bool ReadFileToInt(const std::string &path, int32_t *val);
    bool checkCycleCountRollback();
    std::string checkPaths(const std::vector<std::string> &paths);

    const int kNum77759GMSRFields = 11;
    const int kNum77779GMSRFields = 9;
    const int kNum17201HISTFields = 16;
    const int kNumEEPROMPipelineFields = sizeof(BatteryEEPROMPipeline) / sizeof(int32_t);

    const std::string kBatteryPairingPath = "/sys/class/power_supply/battery/pairing_state";
    const std::string kBatteryCycleCountPath = "/sys/class/power_supply/battery/cycle_count";
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

#endif  // HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYEEPROMREPORTER_H
