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

#define LOG_TAG "pixelstats: BatteryEEPROM"

#include <log/log.h>
#include <time.h>
#include <utils/Timers.h>
#include <cinttypes>
#include <cmath>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <pixelstats/BatteryEEPROMReporter.h>
#include <pixelstats/StatsHelper.h>
#include <hardware/google/pixel/pixelstats/pixelatoms.pb.h>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::VendorAtom;
using aidl::android::frameworks::stats::VendorAtomValue;
using android::base::ReadFileToString;
using android::hardware::google::pixel::PixelAtoms::BatteryEEPROM;

#define LINESIZE 31
#define LINESIZE_MAX17201_HIST 80

BatteryEEPROMReporter::BatteryEEPROMReporter() {}

bool BatteryEEPROMReporter::ReadFileToInt(const std::string &path, int32_t *val) {
    std::string file_contents;

    if (!ReadFileToString(path.c_str(), &file_contents)) {
        ALOGI("Unable to read %s - %s", path.c_str(), strerror(errno));
        return false;
    }

    file_contents = android::base::Trim(file_contents);
    if (!android::base::ParseInt(file_contents, val)) {
        ALOGI("Unable to convert %s to int - %s", path.c_str(), strerror(errno));
        return false;
    }

    return true;
}

bool BatteryEEPROMReporter::checkCycleCountRollback() {
    int cycle_count;

    if (ReadFileToInt(kBatteryCycleCountPath.c_str(), &cycle_count) && cycle_count > 0) {
        if (last_cycle_count == 0) {
            last_cycle_count = cycle_count;
            return false;
        }

        if (cycle_count < last_cycle_count) {
            ALOGD("Cycle count rollback from %d to %d", last_cycle_count, cycle_count);
            last_cycle_count = cycle_count;
            return true;
        }
    }

    return false;
}

std::string BatteryEEPROMReporter::checkPaths(const std::vector<std::string>& paths) {
    if (paths.empty()) {
        return ""; // Or throw an exception if appropriate
    }

    for (const auto& path : paths) { // Use range-based for loop
        if (fileExists(path)) {
            return path;
        }
    }

    return ""; // No path found
}

void BatteryEEPROMReporter::checkAndReport(const std::shared_ptr<IStats> &stats_client,
                                           const std::string &path) {
    std::string file_contents;
    std::string history_each;
    std::string cycle_count;
    int sparse_index_count = 0;
    const int kSecondsPerMonth = 60 * 60 * 24 * 30;
    int64_t now = getTimeSecs();

    if (!checkCycleCountRollback() && (report_time_ != 0) &&
        (now - report_time_ < kSecondsPerMonth)) {
        ALOGD("Not upload time. now: %" PRId64 ", pre: %" PRId64, now, report_time_);
        return;
    }

    if (!ReadFileToString(path.c_str(), &file_contents)) {
        ALOGE("Unable to read %s - %s", path.c_str(), strerror(errno));
        return;
    }

    const int kHistTotalLen = file_contents.size();
    const int kHistTotalNum = kHistTotalLen / LINESIZE;
    ALOGD("kHistTotalLen=%d, kHistTotalNum=%d\n", kHistTotalLen, kHistTotalNum);

    /* TODO: wait for pa/2875004 merge
    if (ReadFileToString(kBatteryCycleCountPath.c_str(), &cycle_count)) {
        int cnt;

        cycle_count = android::base::Trim(cycle_count);
        if (android::base::ParseInt(cycle_count, &cnt)) {
            cnt /= 10;
            if (cnt > kHistTotalNum)
                sparse_index_count = cnt % kHistTotalNum;
        }

        ALOGD("sparse_index_count %d cnt: %d cycle_count %s\n", sparse_index_count, cnt,
              cycle_count.c_str());
    }
    */

    struct BatteryEEPROMPipelineRawFormat hist_raw;
    struct BatteryEEPROMPipeline hist;
    int16_t i;

    ReadFileToInt(kBatteryPairingPath, &hist.battery_pairing);

    for (i = 0; i < kHistTotalNum; i++) {
        size_t history_offset = i * LINESIZE;
        if (history_offset + LINESIZE > kHistTotalLen)
            break;
        history_each = file_contents.substr(history_offset, LINESIZE);
        unsigned int data[4];

        /* Format transfer: go/gsx01-eeprom */
        int16_t num = sscanf(history_each.c_str(), "%4" SCNx16 "%4" SCNx16 "%x %x %x %x",
                      &hist_raw.tempco, &hist_raw.rcomp0, &data[0], &data[1], &data[2], &data[3]);
        if (num <= 0)
            continue;

        if (hist_raw.tempco == 0xFFFF && hist_raw.rcomp0 == 0xFFFF)
            continue;

        /* Extract each data */
        uint64_t tmp = (int64_t)data[3] << 48 |
                       (int64_t)data[2] << 32 |
                       (int64_t)data[1] << 16 |
                       data[0];

        /* ignore this data if unreasonable */
        if (tmp <= 0)
            continue;

        /* data format/unit in go/gsx01-eeprom#heading=h.finy98ign34p */
        hist_raw.timer_h = tmp & 0xFF;
        hist_raw.fullcapnom = (tmp >>= 8) & 0x3FF;
        hist_raw.fullcaprep = (tmp >>= 10) & 0x3FF;
        hist_raw.mixsoc = (tmp >>= 10) & 0x3F;
        hist_raw.vfsoc = (tmp >>= 6) & 0x3F;
        hist_raw.maxvolt = (tmp >>= 6) & 0xF;
        hist_raw.minvolt = (tmp >>= 4) & 0xF;
        hist_raw.maxtemp = (tmp >>= 4) & 0xF;
        hist_raw.mintemp = (tmp >>= 4) & 0xF;
        hist_raw.maxchgcurr = (tmp >>= 4) & 0xF;
        hist_raw.maxdischgcurr = (tmp >>= 4) & 0xF;

        /* Mapping to original format to collect data */
        /* go/pixel-battery-eeprom-atom#heading=h.dcawdjiz2ls6 */
        hist.tempco = (int32_t)hist_raw.tempco;
        hist.rcomp0 = (int32_t)hist_raw.rcomp0;
        hist.timer_h = (int32_t)hist_raw.timer_h * 5;
        hist.max_temp = (int32_t)hist_raw.maxtemp * 3 + 22;
        hist.min_temp = (int32_t)hist_raw.mintemp * 3 - 20;
        hist.min_ibatt = (int32_t)hist_raw.maxchgcurr * 500 * (-1);
        hist.max_ibatt = (int32_t)hist_raw.maxdischgcurr * 500;
        hist.min_vbatt = (int32_t)hist_raw.minvolt * 10 + 2500;
        hist.max_vbatt = (int32_t)hist_raw.maxvolt * 20 + 4200;
        hist.batt_soc = (int32_t)hist_raw.vfsoc * 2;
        hist.msoc = (int32_t)hist_raw.mixsoc * 2;
        hist.full_cap = (int32_t)hist_raw.fullcaprep * 125 / 1000;
        hist.full_rep = (int32_t)hist_raw.fullcapnom * 125 / 1000;

        /* i < sparse_index_count: 20 40 60 80  */
        if (i < sparse_index_count)
            hist.cycle_cnt = (i + 1) * 20;
        else
            hist.cycle_cnt = (i + sparse_index_count + 1) * 10;

        reportEvent(stats_client, hist);
        report_time_ = getTimeSecs();
    }
    return;
}

int64_t BatteryEEPROMReporter::getTimeSecs(void) {
    return nanoseconds_to_seconds(systemTime(SYSTEM_TIME_BOOTTIME));
}

void BatteryEEPROMReporter::reportEvent(const std::shared_ptr<IStats> &stats_client,
                                        const struct BatteryEEPROMPipeline &hist) {
    std::vector<VendorAtomValue> values(kNumEEPROMPipelineFields);

    ALOGD("reportEvent: cycle_cnt:%d, full_cap:%d, esr:%d, rslow:%d, soh:%d, "
          "batt_temp:%d, cutoff_soc:%d, cc_soc:%d, sys_soc:%d, msoc:%d, "
          "batt_soc:%d, reserve:%d, max_temp:%d, min_temp:%d, max_vbatt:%d, "
          "min_vbatt:%d, max_ibatt:%d, min_ibatt:%d, checksum:%#x, full_rep:%d, "
          "tempco:%#x, rcomp0:%#x, timer_h:%d, batt_pair:%d",
          hist.cycle_cnt, hist.full_cap, hist.esr, hist.rslow, hist.soh, hist.batt_temp,
          hist.cutoff_soc, hist.cc_soc, hist.sys_soc, hist.msoc, hist.batt_soc, hist.reserve,
          hist.max_temp, hist.min_temp, hist.max_vbatt, hist.min_vbatt, hist.max_ibatt,
          hist.min_ibatt, hist.checksum, hist.full_rep, hist.tempco, hist.rcomp0, hist.timer_h,
          hist.battery_pairing);

    setAtomFieldValue(&values, BatteryEEPROM::kCycleCntFieldNumber, hist.cycle_cnt);
    setAtomFieldValue(&values, BatteryEEPROM::kFullCapFieldNumber, hist.full_cap);
    setAtomFieldValue(&values, BatteryEEPROM::kEsrFieldNumber, hist.esr);
    setAtomFieldValue(&values, BatteryEEPROM::kRslowFieldNumber, hist.rslow);
    setAtomFieldValue(&values, BatteryEEPROM::kSohFieldNumber, hist.soh);
    setAtomFieldValue(&values, BatteryEEPROM::kBattTempFieldNumber, hist.batt_temp);
    setAtomFieldValue(&values, BatteryEEPROM::kCutoffSocFieldNumber, hist.cutoff_soc);
    setAtomFieldValue(&values, BatteryEEPROM::kCcSocFieldNumber, hist.cc_soc);
    setAtomFieldValue(&values, BatteryEEPROM::kSysSocFieldNumber, hist.sys_soc);
    setAtomFieldValue(&values, BatteryEEPROM::kMsocFieldNumber, hist.msoc);
    setAtomFieldValue(&values, BatteryEEPROM::kBattSocFieldNumber, hist.batt_soc);
    setAtomFieldValue(&values, BatteryEEPROM::kReserveFieldNumber, hist.reserve);
    setAtomFieldValue(&values, BatteryEEPROM::kMaxTempFieldNumber, hist.max_temp);
    setAtomFieldValue(&values, BatteryEEPROM::kMinTempFieldNumber, hist.min_temp);
    setAtomFieldValue(&values, BatteryEEPROM::kMaxVbattFieldNumber, hist.max_vbatt);
    setAtomFieldValue(&values, BatteryEEPROM::kMinVbattFieldNumber, hist.min_vbatt);
    setAtomFieldValue(&values, BatteryEEPROM::kMaxIbattFieldNumber, hist.max_ibatt);
    setAtomFieldValue(&values, BatteryEEPROM::kMinIbattFieldNumber, hist.min_ibatt);
    setAtomFieldValue(&values, BatteryEEPROM::kChecksumFieldNumber, hist.checksum);
    setAtomFieldValue(&values, BatteryEEPROM::kTempcoFieldNumber, hist.tempco);
    setAtomFieldValue(&values, BatteryEEPROM::kRcomp0FieldNumber, hist.rcomp0);
    setAtomFieldValue(&values, BatteryEEPROM::kTimerHFieldNumber, hist.timer_h);
    setAtomFieldValue(&values, BatteryEEPROM::kFullRepFieldNumber, hist.full_rep);
    setAtomFieldValue(&values, BatteryEEPROM::kBatteryPairingFieldNumber, hist.battery_pairing);

    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kBatteryEeprom,
                        .values = std::move(values)};
    reportVendorAtom(stats_client, event);
}

void BatteryEEPROMReporter::checkAndReportGMSR(const std::shared_ptr<IStats> &stats_client,
                                               const std::vector<std::string> &paths) {
    struct BatteryEEPROMPipeline gmsr = {.checksum = EvtGMSR};
    std::string path = checkPaths(paths);
    std::string file_contents;
    int16_t num;

    if (path.empty())
        return;

    if (!ReadFileToString(path, &file_contents)) {
        ALOGE("Unable to read gmsr path: %s - %s", path.c_str(), strerror(errno));
        return;
    }

    num = sscanf(file_contents.c_str(), "rcomp0\t:%x\ntempco\t:%x\nfullcaprep\t:%x\ncycles\t:%x"
                 "\nfullcapnom\t:%x\nqresidual00\t:%x\nqresidual10\t:%x\nqresidual20\t:%x"
                 "\nqresidual30\t:%x\ncv_mixcap\t:%x\nhalftime\t:%x",
                 &gmsr.rcomp0, &gmsr.tempco, &gmsr.full_rep, &gmsr.cycle_cnt, &gmsr.full_cap,
                 &gmsr.max_vbatt, &gmsr.min_vbatt, &gmsr.max_ibatt, &gmsr.min_ibatt,
                 &gmsr.esr, &gmsr.rslow);
    if (num != kNum77759GMSRFields && num != kNum77779GMSRFields) {
        ALOGE("Couldn't process GMSR. num=%d\n", num);
        return;
    }

    if (num == kNum77759GMSRFields) {
        /* LSB: 5.0μVh/RSENSE ; Rsense LSB is 10μΩ ; multiply by 2 when task period = 351 ms */
        gmsr.cc_soc = gmsr.full_cap * 5 / 3 * 2;
        gmsr.sys_soc = gmsr.full_rep * 5 / 3 * 2;
    } else if (num == kNum77779GMSRFields) {
        /* LSB: 5.0μVh/RSENSE ; Rsense LSB is 10μΩ */
        gmsr.cc_soc = gmsr.full_cap * 5 / 2;
        gmsr.sys_soc = gmsr.full_rep * 5 / 2;
    }

    if (gmsr.tempco == 0xFFFF || gmsr.rcomp0 == 0xFFFF || gmsr.full_cap == 0xFFFF) {
	    ALOGD("Ignore invalid gmsr");
	    return;
    }

    if (!ReadFileToInt(kBatteryCycleCountPath, &gmsr.soh))
        ALOGE("Unable to read cycle count path: %s - %s", kBatteryCycleCountPath.c_str(),
              strerror(errno));

    reportEvent(stats_client, gmsr);
}

void BatteryEEPROMReporter::checkAndReportMaxfgHistory(const std::shared_ptr<IStats> &stats_client,
                                                       const std::string &path) {
    std::string file_contents;
    int16_t i;
    const int kSecondsPerMonth = 60 * 60 * 24 * 30;
    int64_t now = getTimeSecs();

    if (path.empty())
        return;

    if ((report_time_maxfg_ != 0) && (now - report_time_maxfg_ < kSecondsPerMonth)) {
        ALOGD("Not upload time for maxfg history. now: %" PRId64 ", pre: %" PRId64,
              now, report_time_maxfg_);
        return;
    }

    /* not support max17201 */
    if (!ReadFileToString(path, &file_contents))
        return;

    std::string hist_each;
    const int kHistTotalLen = file_contents.size();

    ALOGD("checkAndReportMaxfgHistory:size=%d\n%s", kHistTotalLen, file_contents.c_str());

    for (i = 0; i < kHistTotalLen; i++) {
        struct BatteryEEPROMPipeline maxfg_hist;
        uint16_t nQRTable00, nQRTable10, nQRTable20, nQRTable30, nCycles, nFullCapNom;
        uint16_t nRComp0, nTempCo, nIAvgEmpty, nFullCapRep, nVoltTemp, nMaxMinCurr, nMaxMinVolt;
        uint16_t nMaxMinTemp, nSOC, nTimerH;
        int16_t num;
        size_t hist_offset = i * LINESIZE_MAX17201_HIST;

        if (hist_offset >= file_contents.size())
            break;

        hist_each = file_contents.substr(hist_offset, LINESIZE_MAX17201_HIST);
        num = sscanf(hist_each.c_str(), "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16
                     "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16
                     "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16,
                     &nQRTable00, &nQRTable10, &nQRTable20, &nQRTable30, &nCycles, &nFullCapNom,
                     &nRComp0, &nTempCo, &nIAvgEmpty, &nFullCapRep, &nVoltTemp, &nMaxMinCurr,
                     &nMaxMinVolt, &nMaxMinTemp, &nSOC, &nTimerH);

        if (num != kNum17201HISTFields) {
            ALOGE("Couldn't process %s (num=%d)", hist_each.c_str(), num);
            continue;
        }

        /* not assign: nQRTable00, nQRTable10, nQRTable20, nQRTable30 */
        maxfg_hist.reserve = 0xFF;
        maxfg_hist.tempco = nTempCo;
        maxfg_hist.rcomp0 = nRComp0;
        maxfg_hist.full_rep = nFullCapNom;
        maxfg_hist.full_cap = nFullCapRep;
        maxfg_hist.cycle_cnt = nCycles * 16 / 100; // LSB: 16%;
        maxfg_hist.timer_h = (nTimerH * 32 / 10) / 24; // LSB: 3.2 hours
        maxfg_hist.batt_soc = (nSOC >> 8) & 0x00FF;
        maxfg_hist.msoc = nSOC & 0x00FF;
        maxfg_hist.max_ibatt = ((nMaxMinCurr >> 8) & 0x00FF) * 80;
        maxfg_hist.min_ibatt = (nMaxMinCurr & 0x00FF) * 80 * (-1);
        maxfg_hist.max_vbatt = ((nMaxMinVolt >> 8) & 0x00FF) * 20;
        maxfg_hist.min_vbatt = (nMaxMinVolt & 0x00FF) * 20;
        maxfg_hist.max_temp = (nMaxMinTemp >> 8) & 0x00FF;
        maxfg_hist.min_temp = nMaxMinTemp & 0x00FF;
        maxfg_hist.esr = nIAvgEmpty;
        maxfg_hist.rslow = nVoltTemp;

        reportEvent(stats_client, maxfg_hist);
        report_time_maxfg_ = now;
    }
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
