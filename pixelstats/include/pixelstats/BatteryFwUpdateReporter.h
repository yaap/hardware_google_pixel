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

 #ifndef HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYFWUPDATEREPORTER_H
 #define HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYFWUPDATEREPORTER_H

#include <cstdint>
#include <string>

#include <aidl/android/frameworks/stats/IStats.h>
#include <pixelstats/StatsHelper.h>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using aidl::android::frameworks::stats::VendorAtomValue;

class BatteryFwUpdateReporter {
  public:
    BatteryFwUpdateReporter();

    void checkAndReportFwUpdate(const std::shared_ptr<IStats> &stats_client,
                                const std::vector<std::string> &paths,
                                const ReportEventType &event_type);

  private:

    struct BatteryFwUpdatePipeline {
      int32_t msg_type;
      int32_t msg_category;
      int32_t major_version_from;
      int32_t minor_version_from;
      int32_t major_version_to;
      int32_t minor_version_to;
      int32_t update_status;
      int32_t attempts;
      int32_t unix_time_sec;
      int32_t fw_data0;
      int32_t fw_data1;
      int32_t fw_data2;
      int32_t fw_data3;
    };

    static constexpr unsigned int kNumMaxFwUpdatePaths = 2;
    unsigned int last_check_[kNumMaxFwUpdatePaths] = {0};
    void reportEvent(const std::shared_ptr<IStats> &stats_client,
                     struct BatteryFwUpdatePipeline &data);

    const int kNumFwUpdatePipelineFields = sizeof(BatteryFwUpdatePipeline) / sizeof(int32_t);
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

 #endif  // HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYFWUPDATEREPORTER_H
