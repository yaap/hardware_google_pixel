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

#define LINESIZE 71
#define LINESIZE_V2 31
#define LINESIZE_MAX17201_HIST 80

BatteryEEPROMReporter::BatteryEEPROMReporter() {}

void BatteryEEPROMReporter::setAtomFieldValue(std::vector<VendorAtomValue> *values, int offset,
                                              int content) {
    std::vector<VendorAtomValue> &val = *values;

    if (offset - kVendorAtomOffset < val.size())
        val[offset - kVendorAtomOffset].set<VendorAtomValue::intValue>(content);
}

void BatteryEEPROMReporter::checkAndReport(const std::shared_ptr<IStats> &stats_client,
                                           const std::string &path) {
    std::string file_contents;
    std::string history_each;

    const int kSecondsPerMonth = 60 * 60 * 24 * 30;
    int64_t now = getTimeSecs();

    if ((report_time_ != 0) && (now - report_time_ < kSecondsPerMonth)) {
        ALOGD("Not upload time. now: %" PRId64 ", pre: %" PRId64, now, report_time_);
        return;
    }

    if (!ReadFileToString(path.c_str(), &file_contents)) {
        ALOGE("Unable to read %s - %s", path.c_str(), strerror(errno));
        return;
    }

    int16_t i, num;
    struct BatteryHistory hist;
    const int kHistTotalLen = file_contents.size();

    ALOGD("kHistTotalLen=%d\n", kHistTotalLen);

    if (kHistTotalLen >= (LINESIZE_V2 * BATT_HIST_NUM_MAX_V2)) {
        struct BatteryHistoryExtend histv2;
        for (i = 0; i < BATT_HIST_NUM_MAX_V2; i++) {
            size_t history_offset = i * LINESIZE_V2;
            if (history_offset > file_contents.size())
                break;
            history_each = file_contents.substr(history_offset, LINESIZE_V2);
            unsigned int data[4];

            /* Format transfer: go/gsx01-eeprom */
            num = sscanf(history_each.c_str(), "%4" SCNx16 "%4" SCNx16 "%x %x %x %x",
                        &histv2.tempco, &histv2.rcomp0, &data[0], &data[1], &data[2], &data[3]);

            if (histv2.tempco == 0xFFFF && histv2.rcomp0 == 0xFFFF)
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
            histv2.timer_h = tmp & 0xFF;
            histv2.fullcapnom = (tmp >>= 8) & 0x3FF;
            histv2.fullcaprep = (tmp >>= 10) & 0x3FF;
            histv2.mixsoc = (tmp >>= 10) & 0x3F;
            histv2.vfsoc = (tmp >>= 6) & 0x3F;
            histv2.maxvolt = (tmp >>= 6) & 0xF;
            histv2.minvolt = (tmp >>= 4) & 0xF;
            histv2.maxtemp = (tmp >>= 4) & 0xF;
            histv2.mintemp = (tmp >>= 4) & 0xF;
            histv2.maxchgcurr = (tmp >>= 4) & 0xF;
            histv2.maxdischgcurr = (tmp >>= 4) & 0xF;

            /* Mapping to original format to collect data */
            /* go/pixel-battery-eeprom-atom#heading=h.dcawdjiz2ls6 */
            hist.tempco = histv2.tempco;
            hist.rcomp0 = histv2.rcomp0;
            hist.timer_h = (uint8_t)histv2.timer_h * 5;
            hist.max_temp = (int8_t)histv2.maxtemp * 3 + 22;
            hist.min_temp = (int8_t)histv2.mintemp * 3 - 20;
            hist.min_ibatt = (int16_t)histv2.maxchgcurr * 500 * (-1);
            hist.max_ibatt = (int16_t)histv2.maxdischgcurr * 500;
            hist.min_vbatt = (uint16_t)histv2.minvolt * 10 + 2500;
            hist.max_vbatt = (uint16_t)histv2.maxvolt * 20 + 4200;
            hist.batt_soc = (uint8_t)histv2.vfsoc * 2;
            hist.msoc = (uint8_t)histv2.mixsoc * 2;
            hist.full_cap = (int16_t)histv2.fullcaprep * 125 / 1000;
            hist.full_rep = (int16_t)histv2.fullcapnom * 125 / 1000;
            hist.cycle_cnt = (i + 1) * 10;

            reportEvent(stats_client, hist);
            report_time_ = getTimeSecs();
        }
        return;
    }

    for (i = 0; i < (LINESIZE * BATT_HIST_NUM_MAX); i = i + LINESIZE) {
        if (i + LINESIZE > kHistTotalLen)
            break;
        history_each = file_contents.substr(i, LINESIZE);
        num = sscanf(history_each.c_str(),
                   "%4" SCNx16 "%4" SCNx16 "%4" SCNx16 "%4" SCNx16
                   "%2" SCNx8 "%2" SCNx8 " %2" SCNx8 "%2" SCNx8
                   "%2" SCNx8 "%2" SCNx8 " %2" SCNx8 "%2" SCNx8
                   "%2" SCNx8 "%2" SCNx8 " %4" SCNx16 "%4" SCNx16
                   "%4" SCNx16 "%4" SCNx16 "%4" SCNx16,
                   &hist.cycle_cnt, &hist.full_cap, &hist.esr,
                   &hist.rslow, &hist.batt_temp, &hist.soh,
                   &hist.cc_soc, &hist.cutoff_soc, &hist.msoc,
                   &hist.sys_soc, &hist.reserve, &hist.batt_soc,
                   &hist.min_temp, &hist.max_temp,  &hist.max_vbatt,
                   &hist.min_vbatt, &hist.max_ibatt, &hist.min_ibatt,
                   &hist.checksum);

        if (num != kNumBatteryHistoryFields) {
            ALOGE("Couldn't process %s", history_each.c_str());
            continue;
        }

        if (checkLogEvent(hist)) {
            reportEvent(stats_client, hist);
            report_time_ = getTimeSecs();
        }
    }
}

int64_t BatteryEEPROMReporter::getTimeSecs(void) {
    return nanoseconds_to_seconds(systemTime(SYSTEM_TIME_BOOTTIME));
}

/**
 * @return true if a log should be reported, else false.
 * Here we use checksum to confirm the data is usable or not.
 * The checksum mismatch when storage data overflow or corrupt.
 * We don't need data in such cases.
 */
bool BatteryEEPROMReporter::checkLogEvent(struct BatteryHistory hist) {
    int checksum = 0;

    checksum = hist.cycle_cnt + hist.full_cap + hist.esr + hist.rslow
                + hist.soh + hist.batt_temp + hist.cutoff_soc + hist.cc_soc
                + hist.sys_soc + hist.msoc + hist.batt_soc + hist.reserve
                + hist.max_temp + hist.min_temp + hist.max_vbatt
                + hist.min_vbatt + hist.max_ibatt + hist.min_ibatt;
    /* Compare with checksum data */
    if (checksum == hist.checksum) {
        return true;
    } else {
        return false;
    }
}

void BatteryEEPROMReporter::reportEvent(const std::shared_ptr<IStats> &stats_client,
                                        const struct BatteryHistory &hist) {
    // upload atom
    const std::vector<int> eeprom_history_fields = {
            BatteryEEPROM::kCycleCntFieldNumber,  BatteryEEPROM::kFullCapFieldNumber,
            BatteryEEPROM::kEsrFieldNumber,       BatteryEEPROM::kRslowFieldNumber,
            BatteryEEPROM::kSohFieldNumber,       BatteryEEPROM::kBattTempFieldNumber,
            BatteryEEPROM::kCutoffSocFieldNumber, BatteryEEPROM::kCcSocFieldNumber,
            BatteryEEPROM::kSysSocFieldNumber,    BatteryEEPROM::kMsocFieldNumber,
            BatteryEEPROM::kBattSocFieldNumber,   BatteryEEPROM::kReserveFieldNumber,
            BatteryEEPROM::kMaxTempFieldNumber,   BatteryEEPROM::kMinTempFieldNumber,
            BatteryEEPROM::kMaxVbattFieldNumber,  BatteryEEPROM::kMinVbattFieldNumber,
            BatteryEEPROM::kMaxIbattFieldNumber,  BatteryEEPROM::kMinIbattFieldNumber,
            BatteryEEPROM::kChecksumFieldNumber,  BatteryEEPROM::kTempcoFieldNumber,
            BatteryEEPROM::kRcomp0FieldNumber,    BatteryEEPROM::kTimerHFieldNumber,
            BatteryEEPROM::kFullRepFieldNumber};

    ALOGD("reportEvent: cycle_cnt:%d, full_cap:%d, esr:%d, rslow:%d, soh:%d, "
          "batt_temp:%d, cutoff_soc:%d, cc_soc:%d, sys_soc:%d, msoc:%d, "
          "batt_soc:%d, reserve:%d, max_temp:%d, min_temp:%d, max_vbatt:%d, "
          "min_vbatt:%d, max_ibatt:%d, min_ibatt:%d, checksum:%#x, full_rep:%d, "
          "tempco:%#x, rcomp0:%#x, timer_h:%d",
          hist.cycle_cnt, hist.full_cap, hist.esr, hist.rslow, hist.soh, hist.batt_temp,
          hist.cutoff_soc, hist.cc_soc, hist.sys_soc, hist.msoc, hist.batt_soc, hist.reserve,
          hist.max_temp, hist.min_temp, hist.max_vbatt, hist.min_vbatt, hist.max_ibatt,
          hist.min_ibatt, hist.checksum, hist.full_rep, hist.tempco, hist.rcomp0, hist.timer_h);

    std::vector<VendorAtomValue> values(eeprom_history_fields.size());
    VendorAtomValue val;

    val.set<VendorAtomValue::intValue>(hist.cycle_cnt);
    values[BatteryEEPROM::kCycleCntFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.full_cap);
    values[BatteryEEPROM::kFullCapFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.esr);
    values[BatteryEEPROM::kEsrFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.rslow);
    values[BatteryEEPROM::kRslowFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.soh);
    values[BatteryEEPROM::kSohFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.batt_temp);
    values[BatteryEEPROM::kBattTempFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.cutoff_soc);
    values[BatteryEEPROM::kCutoffSocFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.cc_soc);
    values[BatteryEEPROM::kCcSocFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.sys_soc);
    values[BatteryEEPROM::kSysSocFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.msoc);
    values[BatteryEEPROM::kMsocFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.batt_soc);
    values[BatteryEEPROM::kBattSocFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.reserve);
    values[BatteryEEPROM::kReserveFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.max_temp);
    values[BatteryEEPROM::kMaxTempFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.min_temp);
    values[BatteryEEPROM::kMinTempFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.max_vbatt);
    values[BatteryEEPROM::kMaxVbattFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.min_vbatt);
    values[BatteryEEPROM::kMinVbattFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.max_ibatt);
    values[BatteryEEPROM::kMaxIbattFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.min_ibatt);
    values[BatteryEEPROM::kMinIbattFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.checksum);
    values[BatteryEEPROM::kChecksumFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.tempco);
    values[BatteryEEPROM::kTempcoFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.rcomp0);
    values[BatteryEEPROM::kRcomp0FieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.timer_h);
    values[BatteryEEPROM::kTimerHFieldNumber - kVendorAtomOffset] = val;
    val.set<VendorAtomValue::intValue>(hist.full_rep);
    values[BatteryEEPROM::kFullRepFieldNumber - kVendorAtomOffset] = val;

    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kBatteryEeprom,
                        .values = std::move(values)};
    const ndk::ScopedAStatus ret = stats_client->reportVendorAtom(event);
    if (!ret.isOk())
        ALOGE("Unable to report BatteryEEPROM to Stats service");
}

void BatteryEEPROMReporter::reportEventInt32(const std::shared_ptr<IStats> &stats_client,
                                             const struct BatteryHistoryInt32 &hist) {
    std::vector<VendorAtomValue> values(23);

    ALOGD("reportEvent: cycle_cnt:%d, full_cap:%d, esr:%d, rslow:%d, soh:%d, "
          "batt_temp:%d, cutoff_soc:%d, cc_soc:%d, sys_soc:%d, msoc:%d, "
          "batt_soc:%d, reserve:%d, max_temp:%d, min_temp:%d, max_vbatt:%d, "
          "min_vbatt:%d, max_ibatt:%d, min_ibatt:%d, checksum:%#x, full_rep:%d, "
          "tempco:%#x, rcomp0:%#x, timer_h:%d",
          hist.cycle_cnt, hist.full_cap, hist.esr, hist.rslow, hist.soh, hist.batt_temp,
          hist.cutoff_soc, hist.cc_soc, hist.sys_soc, hist.msoc, hist.batt_soc, hist.reserve,
          hist.max_temp, hist.min_temp, hist.max_vbatt, hist.min_vbatt, hist.max_ibatt,
          hist.min_ibatt, hist.checksum, hist.full_rep, hist.tempco, hist.rcomp0, hist.timer_h);

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

    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kBatteryEeprom,
                        .values = std::move(values)};
    const ndk::ScopedAStatus ret = stats_client->reportVendorAtom(event);
    if (!ret.isOk())
        ALOGE("Unable to report BatteryEEPROM to Stats service");
}

void BatteryEEPROMReporter::checkAndReportGMSR(const std::shared_ptr<IStats> &stats_client,
                                               const std::vector<std::string> &paths) {
    struct BatteryHistory gmsr = {.checksum = EvtGMSR};
    std::string file_contents;
    std::string path;
    int16_t num;

    if (paths.empty())
        return;

    for (int i = 0; i < paths.size(); i++) {
        if (fileExists(paths[i])) {
            path = paths[i];
            break;
        }
    }

    if (!ReadFileToString(path, &file_contents)) {
        ALOGE("Unable to read gmsr path: %s - %s", path.c_str(), strerror(errno));
        return;
    }

    num = sscanf(file_contents.c_str(),  "rcomp0\t:%4" SCNx16 "\ntempco\t:%4" SCNx16
                 "\nfullcaprep\t:%4" SCNx16 "\ncycles\t:%4" SCNx16 "\nfullcapnom\t:%4" SCNx16
                 "\nqresidual00\t:%4" SCNx16 "\nqresidual10\t:%4" SCNx16
                 "\nqresidual20\t:%4" SCNx16 "\nqresidual30\t:%4" SCNx16
                 "\ncv_mixcap\t:%4" SCNx16 "\nhalftime\t:%4" SCNx16,
                 &gmsr.rcomp0, &gmsr.tempco, &gmsr.full_rep, &gmsr.cycle_cnt, &gmsr.full_cap,
                 &gmsr.max_vbatt, &gmsr.min_vbatt, &gmsr.max_ibatt, &gmsr.min_ibatt,
                 &gmsr.esr, &gmsr.rslow);
    if (num != kNum77759GMSRFields && num != kNum77779GMSRFields) {
        ALOGE("Couldn't process GMSR. num=%d\n", num);
        return;
    }

    if (gmsr.tempco == 0xFFFF || gmsr.rcomp0 == 0xFFFF || gmsr.full_cap == 0xFFFF) {
	    ALOGD("Ignore invalid gmsr");
	    return;
    }

    reportEvent(stats_client, gmsr);
}

void BatteryEEPROMReporter::checkAndReportMaxfgHistory(const std::shared_ptr<IStats> &stats_client,
                                                       const std::string &path) {
    std::string file_contents;
    int16_t i;

    if (path.empty())
        return;

    if (!ReadFileToString(path, &file_contents)) {
        ALOGD("Unable to read maxfg_hist path: %s - %s", path.c_str(), strerror(errno));
        return;
    }

    std::string hist_each;
    const int kHistTotalLen = file_contents.size();

    ALOGD("checkAndReportMaxfgHistory:size=%d\n%s", kHistTotalLen, file_contents.c_str());

    for (i = 0; i < kHistTotalLen; i++) {
        struct BatteryHistory maxfg_hist;
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
    }
}

void BatteryEEPROMReporter::checkAndReportFGModelLoading(const std::shared_ptr<IStats> &client,
                                                         const std::vector<std::string> &paths) {
    struct BatteryHistory params = {.full_cap = 0, .esr = 0, .rslow = 0,
                                    .checksum = EvtModelLoading, };
    std::string file_contents;
    std::string path;
    int num, pos = 0;
    const char *data;

    if (paths.empty())
        return;

    for (int i = 0; i < paths.size(); i++) {
        if (fileExists(paths[i])) {
            path = paths[i];
            break;
        }
    }

    /* not found */
    if (path.empty())
        return;

    if (!ReadFileToString(path, &file_contents)) {
        ALOGE("Unable to read ModelLoading History path: %s - %s", path.c_str(), strerror(errno));
        return;
    }

    data = file_contents.c_str();

    num = sscanf(&data[pos],  "ModelNextUpdate: %" SCNu16 "\n"
                 "%*x:%*x\n%*x:%*x\n%*x:%*x\n%*x:%*x\n%*x:%*x\n%n",
                 &params.rslow, &pos);
    if (num != 1) {
        ALOGE("Couldn't process ModelLoading History. num=%d\n", num);
        return;
    }

    sscanf(&data[pos],  "ATT: %" SCNu16 " FAIL: %" SCNu16, &params.full_cap, &params.esr);

    /* don't need to report when attempts counter is zero */
    if (params.full_cap == 0)
        return;

    reportEvent(client, params);
}

void BatteryEEPROMReporter::checkAndReportFGLearning(const std::shared_ptr<IStats> &stats_client,
                                                     const std::vector<std::string> &paths) {
    struct BatteryHistoryInt32 params = {.checksum = EvtFGLearningHistory};
    std::string path;
    struct timespec boot_time;
    auto format = FormatIgnoreAddr;
    std::vector<std::vector<uint32_t>> events;

    if (paths.empty())
        return;

    for (int i = 0; i < paths.size(); i++) {
        if (fileExists(paths[i])) {
            path = paths[i];
            break;
        }
    }

    /* not found */
    if (path.empty())
        return;

    clock_gettime(CLOCK_MONOTONIC, &boot_time);

    readLogbuffer(path, kNumFGLearningFieldsV3, params.checksum, format, last_lh_check_, events);
    if (events.size() == 0)
        readLogbuffer(path, kNumFGLearningFieldsV2, params.checksum, format, last_lh_check_, events);

    for (int event_idx = 0; event_idx < events.size(); event_idx++) {
        std::vector<uint32_t> &event = events[event_idx];
        if (event.size() == kNumFGLearningFieldsV2 ||
            event.size() == kNumFGLearningFieldsV3) {
            params.full_cap = event[0];                /* fcnom */
            params.esr = event[1];                     /* dpacc */
            params.rslow = event[2];                   /* dqacc */
            params.full_rep = event[3];                /* fcrep */
            params.msoc = (uint8_t)(event[4] >> 8);    /* repsoc */
            params.sys_soc = (uint8_t)(event[5] >> 8); /* mixsoc */
            params.batt_soc = (uint8_t)(event[6] >> 8);/* vfsoc */
            params.min_ibatt = event[7];               /* fstats */
            params.max_temp = (int8_t)(event[8] >> 8); /* avgtemp */
            params.min_temp = (int8_t)(event[9] >> 8); /* temp */
            params.max_ibatt = event[10];              /* qh */
            params.max_vbatt = event[11];              /* vcell */
            params.min_vbatt = event[12];              /* avgvcell */
            params.cycle_cnt = event[13];              /* vfocf */
            params.rcomp0 = event[14];                 /* rcomp0 */
            params.tempco = event[15];                 /* tempco */
            if (event.size() == kNumFGLearningFieldsV3)
                params.soh = event[16];                /* unix time */
        } else {
            ALOGE("Not support %zu fields for FG learning event", event.size());
            continue;
        }
        reportEventInt32(stats_client, params);
    }
    last_lh_check_ = (unsigned int)boot_time.tv_sec;
}

void BatteryEEPROMReporter::checkAndReportValidation(const std::shared_ptr<IStats> &stats_client,
                                                     const std::vector<std::string> &paths) {
    struct BatteryHistoryInt32 params = {.checksum = EvtHistoryValidation};
    std::string path;
    struct timespec boot_time;
    auto format = FormatIgnoreAddr;
    std::vector<std::vector<uint32_t>> events;

    if (paths.empty())
        return;

    for (int i = 0; i < paths.size(); i++) {
        if (fileExists(paths[i])) {
            path = paths[i];
            break;
        }
    }

    /* not found */
    if (path.empty())
        return;

    clock_gettime(CLOCK_MONOTONIC, &boot_time);

    readLogbuffer(path, kNumValidationFields, params.checksum, format, last_hv_check_, events);
    for (int event_idx = 0; event_idx < events.size(); event_idx++) {
        std::vector<uint32_t> &event = events[event_idx];
        if (event.size() == kNumValidationFields) {
            params.full_cap = event[0]; /* fcnom */
            params.esr = event[1];      /* dpacc */
            params.rslow = event[2];    /* dqacc */
            params.full_rep = event[3]; /* fcrep */
            reportEventInt32(stats_client, params);
        } else {
            ALOGE("Not support %zu fields for History Validation event", event.size());
        }
    }
    last_hv_check_ = (unsigned int)boot_time.tv_sec;
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
