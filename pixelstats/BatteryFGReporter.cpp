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

#define LOG_TAG "pixelstats: BatteryFGReporter"

#include <log/log.h>
#include <time.h>
#include <utils/Timers.h>
#include <cinttypes>
#include <cmath>

#include <android-base/file.h>
#include <pixelstats/BatteryFGReporter.h>
#include <pixelstats/StatsHelper.h>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::VendorAtom;
using aidl::android::frameworks::stats::VendorAtomValue;
using android::base::ReadFileToString;


BatteryFGReporter::BatteryFGReporter() {}

int64_t BatteryFGReporter::getTimeSecs() {
    return nanoseconds_to_seconds(systemTime(SYSTEM_TIME_BOOTTIME));
}

void BatteryFGReporter::convertAndReportFuelGaugeAtom(const std::shared_ptr<IStats> &stats_client,
                                                      const BatteryFuelGaugeReported &report_msg) {
    std::vector<VendorAtomValue> values(5);
    std::vector<int32_t> fg_data_vec;

    values[0].set<VendorAtomValue::longValue>(report_msg.unix_time_sec());
    values[1].set<VendorAtomValue::intValue>(report_msg.data_type());
    values[2].set<VendorAtomValue::intValue>(report_msg.data_event());
    values[3].set<VendorAtomValue::intValue>(report_msg.fg_index());

    for (int32_t val : report_msg.fg_data()) {
        fg_data_vec.push_back(val);
    }
    values[4].set<VendorAtomValue::repeatedIntValue>(fg_data_vec);

    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kBatteryFuelGaugeReported,
                        .values = std::move(values)};
    reportVendorAtom(stats_client, event);
}

std::string BatteryFGReporter::getValidPath(const std::vector<std::string> &paths) {
    if (paths.empty()) {
        return ""; // Return empty string if no paths provided
    }

    for (const auto& path : paths) { // Using range-based for loop for cleaner iteration
        if (fileExists(path)) {
            return path; // Return the first existing path
        }
    }
    return ""; // Return empty string if no existing path is found
}

std::vector<std::string> BatteryFGReporter::getValidPaths(const std::vector<std::string> &paths) {
    std::vector<std::string> valid_paths;

    for (const auto& path : paths) {
        if (!path.empty() && fileExists(path)) {
            valid_paths.push_back(path);
        }
    }

    return valid_paths;
}

void BatteryFGReporter::reportFGAbEvent(const std::shared_ptr<IStats> &stats_client,
                                        struct BatteryFGPipeline &data) {
    BatteryFuelGaugeReported report_msg;

    if (data.event >= kNumMaxEvents) {
        ALOGE("Exceed max number of events, expected=%d, event=%d",
               kNumMaxEvents, data.event);
        return;
    }

    /* save time when trigger, calculate duration when clear */
    if (data.state == 1 && ab_trigger_time_[data.event] == 0) {
        ab_trigger_time_[data.event] = getTimeSecs();
    } else {
        data.duration = getTimeSecs() - ab_trigger_time_[data.event];
        ab_trigger_time_[data.event] = 0;
    }

    ALOGD("reportEvent: event=%d, state=%d, duration=%d, addr01=%04X, data01=%04X, "
          "addr02=%04X, data02=%04X, addr03=%04X, data03=%04X, addr04=%04X, data04=%04X, "
          "addr05=%04X, data05=%04X, addr06=%04X, data06=%04X, addr07=%04X, data07=%04X, "
          "addr08=%04X, data08=%04X, addr09=%04X, data09=%04X, addr10=%04X, data10=%04X, "
          "addr11=%04X, data11=%04X, addr12=%04X, data12=%04X, addr13=%04X, data13=%04X, "
          "addr14=%04X, data14=%04X, addr15=%04X, data15=%04X, addr16=%04X, data16=%04X",
          data.event, data.state, data.duration, data.addr01, data.data01,
          data.addr02, data.data02, data.addr03, data.data03, data.addr04, data.data04,
          data.addr05, data.data05, data.addr06, data.data06, data.addr07, data.data07,
          data.addr08, data.data08, data.addr09, data.data09, data.addr10, data.data10,
          data.addr11, data.data11, data.addr12, data.data12, data.addr13, data.data13,
          data.addr14, data.data14, data.addr15, data.data15, data.addr16, data.data16);


    /*
     * state=0 -> untrigger, state=1 -> trigger
     * Since atom enum reserves unknown value at 0, offset by 1 here
     * state=1-> untrigger, state=2 -> trigger
     */
    data.state += 1;

    report_msg.set_unix_time_sec(data.duration);
    report_msg.set_data_type(EvtFGAbnormalEvent);
    report_msg.set_data_event(data.event);
    report_msg.set_fg_index(BatteryFuelGaugeReported::PRIMARY);
    report_msg.add_fg_data(data.state);
    report_msg.add_fg_data(data.addr01);
    report_msg.add_fg_data(data.data01);
    report_msg.add_fg_data(data.addr02);
    report_msg.add_fg_data(data.data02);
    report_msg.add_fg_data(data.addr03);
    report_msg.add_fg_data(data.data03);
    report_msg.add_fg_data(data.addr04);
    report_msg.add_fg_data(data.data04);
    report_msg.add_fg_data(data.addr05);
    report_msg.add_fg_data(data.data05);
    report_msg.add_fg_data(data.addr06);
    report_msg.add_fg_data(data.data06);
    report_msg.add_fg_data(data.addr07);
    report_msg.add_fg_data(data.data07);
    report_msg.add_fg_data(data.addr08);
    report_msg.add_fg_data(data.data08);
    report_msg.add_fg_data(data.addr09);
    report_msg.add_fg_data(data.data09);
    report_msg.add_fg_data(data.addr10);
    report_msg.add_fg_data(data.data10);
    report_msg.add_fg_data(data.addr11);
    report_msg.add_fg_data(data.data11);
    report_msg.add_fg_data(data.addr12);
    report_msg.add_fg_data(data.data12);
    report_msg.add_fg_data(data.addr13);
    report_msg.add_fg_data(data.data13);
    report_msg.add_fg_data(data.addr14);
    report_msg.add_fg_data(data.data14);
    report_msg.add_fg_data(data.addr15);
    report_msg.add_fg_data(data.data15);
    report_msg.add_fg_data(data.addr16);
    report_msg.add_fg_data(data.data16);

    convertAndReportFuelGaugeAtom(stats_client, report_msg);
}

void BatteryFGReporter::checkAndReportFGAbnormality(const std::shared_ptr<IStats> &stats_client,
                                                    const std::vector<std::string> &paths) {
    std::string path = getValidPath(paths);
    struct timespec boot_time;
    std::vector<std::vector<uint32_t>> events;
    std::vector<int32_t> fields = {kNumFGPipelineFields};

    if (paths.empty())
        return;

    clock_gettime(CLOCK_MONOTONIC, &boot_time);
    readLogbuffer(path, fields, EvtFGAbnormalEvent, FormatOnlyVal, last_ab_check_, events);
    for (int seq = 0; seq < events.size(); seq++) {
        if (events[seq].size() == kNumFGPipelineFields) {
            struct BatteryFGPipeline data;
            std::copy(events[seq].begin(), events[seq].end(), (int32_t *)&data);
            reportFGAbEvent(stats_client, data);
        } else {
            ALOGE("Not support %zu fields for FG abnormal event", events[seq].size());
        }
    }

    last_ab_check_ = (unsigned int)boot_time.tv_sec;
}

void BatteryFGReporter::checkAndReportHistValid(const std::shared_ptr<IStats> &stats_client,
                                                const std::vector<std::string> &paths) {
    std::string path = getValidPath(paths);;
    struct timespec boot_time;
    std::vector<std::vector<uint32_t>> events;
    std::vector<int32_t> fields = {kNumValidationFields};
    BatteryFuelGaugeReported report_msg;

    if (path.empty())
        return;

    clock_gettime(CLOCK_MONOTONIC, &boot_time);

    readLogbuffer(path, fields, EvtHistoryValidation, FormatOnlyVal, last_hv_check_, events);

    for (int event_idx = 0; event_idx < events.size(); event_idx++) {
        std::vector<uint32_t> &event = events[event_idx];
        if (event.size() == kNumValidationFields) {
            report_msg.set_data_type(EvtHistoryValidation);
            report_msg.set_fg_index(BatteryFuelGaugeReported::PRIMARY);
            report_msg.set_data_event(event[0]);    /* log type */
            report_msg.add_fg_data(event[1]);       /* first empty entry */
            report_msg.add_fg_data(event[2]);       /* first misplaced entry */
            report_msg.add_fg_data(event[3]);       /* first migrated entry */
            report_msg.add_fg_data(event[4]);       /* last migrated entry */
            report_msg.add_fg_data(event[5]);       /* last cycle count */
            report_msg.add_fg_data(event[6]);       /* current cycle count */
            report_msg.add_fg_data(event[7]);       /* eeprom cycle count */
            report_msg.add_fg_data(event[8]);       /* result */
            report_msg.set_unix_time_sec(event[9]); /* unix time */

            convertAndReportFuelGaugeAtom(stats_client, report_msg);
            report_msg.Clear();
        } else {
            ALOGE("Not support %zu fields for History Validation event", event.size());
        }
    }
    last_hv_check_ = (unsigned int)boot_time.tv_sec;
}

void BatteryFGReporter::checkAndReportFGLearning(const std::shared_ptr<IStats> &stats_client,
                                                 const std::vector<std::string> &paths) {
    struct timespec boot_time;
    auto format = FormatIgnoreAddr;
    const int data_type = EvtFGLearningHistory;
    BatteryFuelGaugeReported report_msg;
    std::vector<int32_t> fields = {kNumFGLearningFields, kNumFGLearningFieldsV2,
                                         kNumFGLearningFieldsV3, kNumFGLearningFieldsV4};

    clock_gettime(CLOCK_MONOTONIC, &boot_time);
    std::vector<std::string> valid_paths = getValidPaths(paths);
    for (const auto& path : valid_paths) {
        std::vector<std::vector<uint32_t>> events;

        if (path.empty() || !fileExists(path))
            continue;

        auto fg_idx = BatteryFuelGaugeReported::PRIMARY;
        if (path.find("secondary") != std::string::npos)
            fg_idx = BatteryFuelGaugeReported::SECONDARY;

        readLogbuffer(path, fields, data_type, format, last_lh_check_, events);

        for (int event_idx = 0; event_idx < events.size(); event_idx++) {
            std::vector<uint32_t> &event = events[event_idx];
            report_msg.set_data_type(data_type);
            report_msg.set_unix_time_sec(event[16]);       /* unix time */
            report_msg.set_fg_index(fg_idx);               /* fg index */
            report_msg.add_fg_data(event[0]);              /* fcnom */
            report_msg.add_fg_data(event[1]);              /* dpacc */
            report_msg.add_fg_data(event[2]);              /* dqacc */
            report_msg.add_fg_data(event[3]);              /* fcrep */
            report_msg.add_fg_data(event[4]);              /* repsoc */
            report_msg.add_fg_data(event[5]);              /* mixsoc */
            report_msg.add_fg_data(event[6]);              /* vfsoc */
            report_msg.add_fg_data(event[7]);              /* fstats */
            report_msg.add_fg_data(event[8]);              /* avgtemp */
            report_msg.add_fg_data(event[9]);              /* temp */
            report_msg.add_fg_data(event[10]);             /* qh */
            report_msg.add_fg_data(event[11]);             /* vcell */
            report_msg.add_fg_data(event[12]);             /* avgvcell */
            report_msg.add_fg_data(event[13]);             /* vfocv */
            report_msg.add_fg_data(event[14]);             /* rcomp0 */
            report_msg.add_fg_data(event[15]);             /* tempco */
            if (event.size() == kNumFGLearningFieldsV2 || event.size() == kNumFGLearningFieldsV4) {
                report_msg.add_fg_data(event[17]);         /* cotrim */
                report_msg.add_fg_data(event[18]);         /* coff */
                report_msg.add_fg_data(event[19]);         /* lock_1 */
                report_msg.add_fg_data(event[20]);         /* lock_2 */
                if (event.size() == kNumFGLearningFieldsV4)
                    report_msg.set_data_event(event[21]);      /* event */
            } else if (event.size() == kNumFGLearningFieldsV3) {
                report_msg.set_data_event(event[17]);      /* event */
            }
            convertAndReportFuelGaugeAtom(stats_client, report_msg);
            report_msg.Clear();
        }
    }
    last_lh_check_ = (unsigned int)boot_time.tv_sec;
}

void BatteryFGReporter::checkAndReportFGModelLoading(const std::shared_ptr<IStats> &stats_client,
                                                     const std::vector<std::string> &paths) {
    std::string path = getValidPath(paths);
    std::string file_contents;
    int num, model_next_update, att_cnt, fail_cnt;
    const char *data;
    BatteryFuelGaugeReported report_msg;

    /* not found */
    if (path.empty())
        return;

    if (!ReadFileToString(path, &file_contents)) {
        ALOGE("Unable to read ModelLoading History path: %s - %s", path.c_str(), strerror(errno));
        return;
    }

    data = file_contents.c_str();

    num = sscanf(data, "ModelNextUpdate: %d%*[0-9a-f: \n]ATT: %x FAIL: %x",
                 &model_next_update, &att_cnt, &fail_cnt);
    if (num != 3) {
        ALOGE("Couldn't process ModelLoading History. num=%d\n", num);
        return;
     }

    /* don't need to report when attempts counter is zero */
    if (att_cnt == 0)
        return;

    report_msg.set_data_type(EvtModelLoading);
    report_msg.set_fg_index(BatteryFuelGaugeReported::PRIMARY);
    report_msg.add_fg_data(model_next_update);
    report_msg.add_fg_data(att_cnt);
    report_msg.add_fg_data(fail_cnt);

    convertAndReportFuelGaugeAtom(stats_client, report_msg);
    report_msg.Clear();
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
