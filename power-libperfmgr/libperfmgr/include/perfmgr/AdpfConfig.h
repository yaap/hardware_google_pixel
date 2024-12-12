/*
 * Copyright 2022 The Android Open Source Project
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

#pragma once

#include <optional>
#include <string>

namespace android {
namespace perfmgr {

struct AdpfConfig {
    std::string mName;
    // Pid control
    bool mPidOn;
    double mPidPo;
    double mPidPu;
    double mPidI;
    int64_t mPidIInit;
    int64_t mPidIHigh;
    int64_t mPidILow;
    double mPidDo;
    double mPidDu;
    // Uclamp boost control
    bool mUclampMinOn;
    uint32_t mUclampMinInit;
    uint32_t mUclampMinHigh;
    uint32_t mUclampMinLow;
    // Batch update control
    uint64_t mSamplingWindowP;
    uint64_t mSamplingWindowI;
    uint64_t mSamplingWindowD;
    int64_t mReportingRateLimitNs;
    double mTargetTimeFactor;
    // Stale control
    double mStaleTimeFactor;

    std::optional<bool> mGpuBoostOn;
    std::optional<uint64_t> mGpuBoostCapacityMax;
    uint64_t mGpuCapacityLoadUpHeadroom;

    // Heuristic boost control
    std::optional<bool> mHeuristicBoostOn;
    std::optional<uint32_t> mHBoostModerateJankThreshold;
    std::optional<double> mHBoostOffMaxAvgDurRatio;
    std::optional<double> mHBoostSevereJankPidPu;
    std::optional<uint32_t> mHBoostSevereJankThreshold;
    std::optional<std::pair<uint32_t, uint32_t>> mHBoostUclampMinCeilingRange;
    std::optional<std::pair<uint32_t, uint32_t>> mHBoostUclampMinFloorRange;
    std::optional<double> mJankCheckTimeFactor;
    std::optional<uint32_t> mLowFrameRateThreshold;
    std::optional<uint32_t> mMaxRecordsNum;

    uint32_t mUclampMinLoadUp;
    uint32_t mUclampMinLoadReset;

    // Power efficient sessions
    std::optional<int32_t> mUclampMaxEfficientBase;
    std::optional<int32_t> mUclampMaxEfficientOffset;

    int64_t getPidIInitDivI();
    int64_t getPidIHighDivI();
    int64_t getPidILowDivI();
    void dumpToFd(int fd);

    AdpfConfig(std::string name, bool pidOn, double pidPo, double pidPu, double pidI,
               int64_t pidIInit, int64_t pidIHigh, int64_t pidILow, double pidDo, double pidDu,
               bool uclampMinOn, uint32_t uclampMinInit, uint32_t uclampMinHigh,
               uint32_t uclampMinLow, uint64_t samplingWindowP, uint64_t samplingWindowI,
               uint64_t samplingWindowD, int64_t reportingRateLimitNs, double targetTimeFactor,
               double staleTimeFactor, std::optional<bool> gpuBoostOn,
               std::optional<uint64_t> gpuBoostCapacityMax, uint64_t gpuCapacityLoadUpHeadroom,
               std::optional<bool> heuristicBoostOn,
               std::optional<uint32_t> hBoostModerateJankThreshold,
               std::optional<double> hBoostOffMaxAvgDurRatio,
               std::optional<double> hBoostSevereJankPidPu,
               std::optional<uint32_t> hBoostSevereJankThreshold,
               std::optional<std::pair<uint32_t, uint32_t>> hBoostUclampMinCeilingRange,
               std::optional<std::pair<uint32_t, uint32_t>> hBoostUclampMinFloorRange,
               std::optional<double> jankCheckTimeFactor,
               std::optional<uint32_t> lowFrameRateThreshold, std::optional<uint32_t> maxRecordsNum,
               uint32_t uclampMinLoadUp, uint32_t uclampMinLoadReset,
               std::optional<int32_t> uclampMaxEfficientBase,
               std::optional<int32_t> uclampMaxEfficientOffset)
        : mName(std::move(name)),
          mPidOn(pidOn),
          mPidPo(pidPo),
          mPidPu(pidPu),
          mPidI(pidI),
          mPidIInit(pidIInit),
          mPidIHigh(pidIHigh),
          mPidILow(pidILow),
          mPidDo(pidDo),
          mPidDu(pidDu),
          mUclampMinOn(uclampMinOn),
          mUclampMinInit(uclampMinInit),
          mUclampMinHigh(uclampMinHigh),
          mUclampMinLow(uclampMinLow),
          mSamplingWindowP(samplingWindowP),
          mSamplingWindowI(samplingWindowI),
          mSamplingWindowD(samplingWindowD),
          mReportingRateLimitNs(reportingRateLimitNs),
          mTargetTimeFactor(targetTimeFactor),
          mStaleTimeFactor(staleTimeFactor),
          mGpuBoostOn(gpuBoostOn),
          mGpuBoostCapacityMax(gpuBoostCapacityMax),
          mGpuCapacityLoadUpHeadroom(gpuCapacityLoadUpHeadroom),
          mHeuristicBoostOn(heuristicBoostOn),
          mHBoostModerateJankThreshold(hBoostModerateJankThreshold),
          mHBoostOffMaxAvgDurRatio(hBoostOffMaxAvgDurRatio),
          mHBoostSevereJankPidPu(hBoostSevereJankPidPu),
          mHBoostSevereJankThreshold(hBoostSevereJankThreshold),
          mHBoostUclampMinCeilingRange(hBoostUclampMinCeilingRange),
          mHBoostUclampMinFloorRange(hBoostUclampMinFloorRange),
          mJankCheckTimeFactor(jankCheckTimeFactor),
          mLowFrameRateThreshold(lowFrameRateThreshold),
          mMaxRecordsNum(maxRecordsNum),
          mUclampMinLoadUp(uclampMinLoadUp),
          mUclampMinLoadReset(uclampMinLoadReset),
          mUclampMaxEfficientBase(uclampMaxEfficientBase),
          mUclampMaxEfficientOffset(uclampMaxEfficientOffset) {}
};

}  // namespace perfmgr
}  // namespace android
