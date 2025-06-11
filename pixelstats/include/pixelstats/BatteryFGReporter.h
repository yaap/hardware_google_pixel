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

 #ifndef HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYFGREPORTER_H
 #define HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYFGREPORTER_H

#include <cstdint>
#include <string>

#include <aidl/android/frameworks/stats/IStats.h>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

using aidl::android::frameworks::stats::IStats;
using aidl::android::frameworks::stats::VendorAtomValue;

class BatteryFGReporter {
  public:
    BatteryFGReporter();

    void checkAndReportFwUpdate(const std::shared_ptr<IStats> &stats_client, const std::string &path);
    void checkAndReportFGAbnormality(const std::shared_ptr<IStats> &stats_client, const std::vector<std::string> &paths);

  private:
    const int kVendorAtomOffset = 2;

    struct BatteryFGPipeline {
      int32_t event;
      int32_t state;
      int32_t duration;
      int32_t addr01;
      int32_t data01;
      int32_t addr02;
      int32_t data02;
      int32_t addr03;
      int32_t data03;
      int32_t addr04;
      int32_t data04;
      int32_t addr05;
      int32_t data05;
      int32_t addr06;
      int32_t data06;
      int32_t addr07;
      int32_t data07;
      int32_t addr08;
      int32_t data08;
      int32_t addr09;
      int32_t data09;
      int32_t addr10;
      int32_t data10;
      int32_t addr11;
      int32_t data11;
      int32_t addr12;
      int32_t data12;
      int32_t addr13;
      int32_t data13;
      int32_t addr14;
      int32_t data14;
      int32_t addr15;
      int32_t data15;
      int32_t addr16;
      int32_t data16;
    };

    int64_t getTimeSecs();

    unsigned int last_ab_check_ = 0;
    static constexpr unsigned int kNumMaxEvents = 8;
    unsigned int ab_trigger_time_[kNumMaxEvents] = {0};
    void reportFGEvent(const std::shared_ptr<IStats> &stats_client, struct BatteryFGPipeline &data);

    const int kNumFGPipelineFields = sizeof(BatteryFGPipeline) / sizeof(int32_t);
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

 #endif  // HARDWARE_GOOGLE_PIXEL_PIXELSTATS_BATTERYFGREPORTER_H
