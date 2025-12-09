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

#define LOG_TAG "pixelstats: BatteryFwUpdateReporter"

#include <log/log.h>
#include <time.h>
#include <utils/Timers.h>
#include <cinttypes>
#include <cmath>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <pixelstats/BatteryFwUpdateReporter.h>
#include <hardware/google/pixel/pixelstats/pixelatoms.pb.h>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::VendorAtom;
using aidl::android::frameworks::stats::VendorAtomValue;
using android::base::ReadFileToString;
using android::hardware::google::pixel::PixelAtoms::BatteryFirmwareUpdateReported;

BatteryFwUpdateReporter::BatteryFwUpdateReporter() {}

void BatteryFwUpdateReporter::reportEvent(const std::shared_ptr<IStats> &stats_client,
                                          struct BatteryFwUpdatePipeline &data) {
    std::vector<VendorAtomValue> values(kNumFwUpdatePipelineFields);

    ALOGD("reportEvent: msg_type=%d, msg_category=%d, major_ver_from=%d, minor_ver_from=%d, "
          "major_ver_to=%d, minor_ver_to=%d, update_status=%d, attempts=%d, unix_time_sec=%d "
          "fw_data0=%d, fw_data1=%d, fw_data2=%d, fw_data3=%d",
          data.msg_type, data.msg_category, data.major_version_from, data.minor_version_from,
          data.major_version_to, data.minor_version_to, data.update_status, data.attempts,
          data.unix_time_sec, data.fw_data0, data.fw_data1, data.fw_data2, data.fw_data3);

    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kMsgTypeFieldNumber, data.msg_type);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kMsgCategoryFieldNumber,
                      data.msg_category);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kMajorVersionFromFieldNumber,
                      data.major_version_from);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kMinorVersionFromFieldNumber,
                      data.minor_version_from);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kMajorVersionToFieldNumber,
                      data.major_version_to);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kMinorVersionToFieldNumber,
                      data.minor_version_to);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kUpdateStatusFieldNumber,
                      data.update_status);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kAttemptsFieldNumber, data.attempts);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kUnixTimeSecFieldNumber,
                      data.unix_time_sec);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kFwData0FieldNumber, data.fw_data0);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kFwData1FieldNumber, data.fw_data1);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kFwData2FieldNumber, data.fw_data2);
    setAtomFieldValue(&values, BatteryFirmwareUpdateReported::kFwData3FieldNumber, data.fw_data3);

    VendorAtom event = {.reverseDomainName = "",
                        .atomId = PixelAtoms::Atom::kBatteryFirmwareUpdateReported,
                        .values = std::move(values)};
    reportVendorAtom(stats_client, event);
}

void BatteryFwUpdateReporter::checkAndReportFwUpdate(const std::shared_ptr<IStats> &stats_client,
                                                     const std::vector<std::string> &paths,
                                                     const ReportEventType &event_type) {
    struct BatteryFwUpdatePipeline params;
    struct timespec boot_time;
    std::vector<int32_t> fields = {kNumFwUpdatePipelineFields};

    if (paths.empty())
        return;

    if (paths.size() > kNumMaxFwUpdatePaths) {
        ALOGE("Exceed max number of FwUpdatePath, expected=%d, paths=%zu",
               kNumMaxFwUpdatePaths, paths.size());
        return;
    }

    for (int i = 0; i < paths.size(); i++) {
        if (!fileExists(paths[i]))
            continue;

        std::vector<std::vector<uint32_t>> events;

        clock_gettime(CLOCK_MONOTONIC, &boot_time);
        readLogbuffer(paths[i], fields, event_type, FormatOnlyVal,
                      last_check_[i], events);
        for (int event_idx = 0; event_idx < events.size(); event_idx++) {
            std::vector<uint32_t> &event = events[event_idx];
            if (event.size() == kNumFwUpdatePipelineFields) {
                params.msg_type = event[0];
                params.msg_category = event[1];
                params.major_version_from = event[2];
                params.minor_version_from = event[3];
                params.major_version_to = event[4];
                params.minor_version_to = event[5];
                params.update_status = event[6];
                params.attempts = event[7];
                params.unix_time_sec = event[8];
                params.fw_data0 = event[9];
                params.fw_data1 = event[10];
                params.fw_data2 = event[11];
                params.fw_data3 = event[12];
                reportEvent(stats_client, params);
            } else {
                ALOGE("Not support %zu fields for Firmware Update event", event.size());
            }
        }
        last_check_[i] = (unsigned int)boot_time.tv_sec;
    }
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
