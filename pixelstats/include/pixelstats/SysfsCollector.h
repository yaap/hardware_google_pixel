/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef HARDWARE_GOOGLE_PIXEL_PIXELSTATS_SYSFSCOLLECTOR_H
#define HARDWARE_GOOGLE_PIXEL_PIXELSTATS_SYSFSCOLLECTOR_H

#include <aidl/android/frameworks/stats/IStats.h>
#include <hardware/google/pixel/pixelstats/pixelatoms.pb.h>
#include <json/reader.h>

#include "BatteryEEPROMReporter.h"
#include "BatteryFGReporter.h"
#include "BatteryHealthReporter.h"
#include "BatteryTTFReporter.h"
#include "DisplayStatsReporter.h"
#include "MitigationDurationReporter.h"
#include "MitigationStatsReporter.h"
#include "MmMetricsReporter.h"
#include "SSRestartReporter.h"
#include "TempResidencyReporter.h"
#include "ThermalStatsReporter.h"
#include "WaterEventReporter.h"

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using android::hardware::google::pixel::PixelAtoms::VendorSlowIo;

class SysfsCollector {
  public:
    SysfsCollector(const Json::Value& configData);
    void collect();

  private:
    const Json::Value configData;
    bool ReadFileToInt(const std::string &path, int *val);
    bool ReadFileToInt(const char *path, int *val);
    void aggregatePer5Min();
    void logOnce();
    void logWater();
    void logPerDay();
    void logPerHour();

    void logBatteryChargeCycles(const std::shared_ptr<IStats> &stats_client);
    void logBatteryHealth(const std::shared_ptr<IStats> &stats_client);
    void logBatteryTTF(const std::shared_ptr<IStats> &stats_client);
    void logBlockStatsReported(const std::shared_ptr<IStats> &stats_client);
    void logCodecFailed(const std::shared_ptr<IStats> &stats_client);
    void logCodec1Failed(const std::shared_ptr<IStats> &stats_client);
    void logSlowIO(const std::shared_ptr<IStats> &stats_client);
    void logSpeakerImpedance(const std::shared_ptr<IStats> &stats_client);
    void logSpeechDspStat(const std::shared_ptr<IStats> &stats_client);
    void logBatteryCapacity(const std::shared_ptr<IStats> &stats_client);
    void logUFSLifetime(const std::shared_ptr<IStats> &stats_client);
    void logUFSErrorsCount(const std::shared_ptr<IStats> &stats_client);
    void logF2fsStats(const std::shared_ptr<IStats> &stats_client);
    void logF2fsAtomicWriteInfo(const std::shared_ptr<IStats> &stats_client);
    void logF2fsCompressionInfo(const std::shared_ptr<IStats> &stats_client);
    void logF2fsGcSegmentInfo(const std::shared_ptr<IStats> &stats_client);
    void logZramStats(const std::shared_ptr<IStats> &stats_client);
    void logBootStats(const std::shared_ptr<IStats> &stats_client);
    void logBatteryEEPROM(const std::shared_ptr<IStats> &stats_client);
    void logSpeakerHealthStats(const std::shared_ptr<IStats> &stats_client);
    void logF2fsSmartIdleMaintEnabled(const std::shared_ptr<IStats> &stats_client);
    void logThermalStats(const std::shared_ptr<IStats> &stats_client);
    void logMitigationDurationCounts(const std::shared_ptr<IStats> &stats_client);
    void logDisplayStats(const std::shared_ptr<IStats> &stats_client);
    void logDisplayPortStats(const std::shared_ptr<IStats> &stats_client);
    void logDisplayPortDSCStats(const std::shared_ptr<IStats> &stats_client);
    void logDisplayPortMaxResolutionStats(const std::shared_ptr<IStats> &stats_client);
    void logHDCPStats(const std::shared_ptr<IStats> &stats_client);
    void logVendorAudioPdmStatsReported(const std::shared_ptr<IStats> &stats_client);

    void reportSlowIoFromFile(const std::shared_ptr<IStats> &stats_client, const std::string& path,
                              const VendorSlowIo::IoOperation &operation_s);
    void logTempResidencyStats(const std::shared_ptr<IStats> &stats_client);
    void reportZramMmStat(const std::shared_ptr<IStats> &stats_client);
    void reportZramBdStat(const std::shared_ptr<IStats> &stats_client);
    int getReclaimedSegments(const std::string &mode);
    void logVendorAudioHardwareStats(const std::shared_ptr<IStats> &stats_client);
    void logVendorLongIRQStatsReported(const std::shared_ptr<IStats> &stats_client);
    void logVendorResumeLatencyStats(const std::shared_ptr<IStats> &stats_client);
    void logPartitionUsedSpace(const std::shared_ptr<IStats> &stats_client);
    void logPcieLinkStats(const std::shared_ptr<IStats> &stats_client);
    void logWavesStats(const std::shared_ptr<IStats> &stats_client);
    void logAdaptedInfoStats(const std::shared_ptr<IStats> &stats_client);
    void logPcmUsageStats(const std::shared_ptr<IStats> &stats_client);
    void logOffloadEffectsStats(const std::shared_ptr<IStats> &stats_client);
    void logBluetoothAudioUsage(const std::shared_ptr<IStats> &stats_client);
    void logBatteryGMSR(const std::shared_ptr<IStats> &stats_client);
    void logDmVerityPartitionReadAmount(const std::shared_ptr<IStats> &stats_client);
    void logSSRestartStats(const std::shared_ptr<IStats> &stats_client);
    void logBatteryHistoryValidation();
    void logUfsStorageType();

    BatteryEEPROMReporter battery_EEPROM_reporter_;
    MmMetricsReporter mm_metrics_reporter_;
    MitigationStatsReporter mitigation_stats_reporter_;
    MitigationDurationReporter mitigation_duration_reporter_;
    ThermalStatsReporter thermal_stats_reporter_;
    DisplayStatsReporter display_stats_reporter_;
    BatteryHealthReporter battery_health_reporter_;
    BatteryTTFReporter battery_time_to_full_reporter_;
    TempResidencyReporter temp_residency_reporter_;
    WaterEventReporter water_event_reporter_;
    BatteryFGReporter battery_fg_reporter_;
    SSRestartReporter ss_restart_reporter_;
    // Proto messages are 1-indexed and VendorAtom field numbers start at 2, so
    // store everything in the values array at the index of the field number    // -2.
    const int kVendorAtomOffset = 2;

    bool log_once_reported = false;
    int64_t prev_huge_pages_since_boot_ = -1;

    struct perf_metrics_data {
        uint64_t resume_latency_sum_ms;
        int64_t resume_count;
        std::vector<int64_t> resume_latency_buckets;
        int bucket_cnt;
    };
    struct perf_metrics_data prev_data;
    const int kMaxResumeLatencyBuckets = 36;
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

#endif  // HARDWARE_GOOGLE_PIXEL_PIXELSTATS_SYSFSCOLLECTOR_H
