/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <BootControlClient.h>
#include <android-base/endian.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <dlfcn.h>
#include <misc_writer/misc_writer.h>
#include <recovery_ui/device.h>
#include <recovery_ui/wear_ui.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <string_view>
#include <vector>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

namespace {

// Provision Silent OTA(SOTA) flag while reason is "enable-sota"
bool ProvisionSilentOtaFlag(const std::string& reason) {
    if (android::base::StartsWith(reason, MiscWriter::kSotaFlag)) {
        MiscWriter misc_writer(MiscWriterActions::kSetSotaFlag);
        if (!misc_writer.PerformAction()) {
            LOG(ERROR) << "Failed to set the silent ota flag";
            return false;
        }
        LOG(INFO) << "Silent ota flag set successful";
    }
    return true;
}

/** Call device-specifc WipeEse function, if any. */
bool WipeEseHook(::RecoveryUI *const ui) {
    bool *(*WipeEseFunc)(::RecoveryUI *const);
    reinterpret_cast<void *&>(WipeEseFunc) = dlsym(RTLD_DEFAULT, "WipeEse");
    if (WipeEseFunc == nullptr) {
        LOG(INFO) << "No WipeEse implementation";
        return true;
    }

    return (*WipeEseFunc)(ui);
}

}  // namespace

class PixelWatchDevice : public ::Device {
  public:
    explicit PixelWatchDevice(::WearRecoveryUI* const ui) : ::Device(ui) {}

    bool PreWipeData() override {
        uint32_t currentSlot = 0;
        const auto module = android::hal::BootControlClient::WaitForService();
        if (module == nullptr) {
            LOG(ERROR) << "Error getting bootctrl module, slot attributes not reset";
        } else {
            // Reset current slot attributes
            currentSlot = module->GetCurrentSlot();
            LOG(INFO) << "Slot attributes reset for slot " << currentSlot;
            const auto result = module->SetActiveBootSlot(currentSlot);
            if (!result.IsOk()) {
                LOG(ERROR) << "Unable to call SetActiveBootSlot for slot " << currentSlot;
            }
        }

        // Loogging errors is sufficient, we don't want to block Wipe Data on this.
        return true;
    }
    /** Hook to wipe user data not stored in /data */
    bool PostWipeData() override {
        // Try to do everything but report a failure if anything wasn't successful
        bool totalSuccess = true;

        // Additional behavior along with wiping data
        auto reason = GetReason();
        CHECK(reason.has_value());
        if (!ProvisionSilentOtaFlag(reason.value())) {
            totalSuccess = false;
        }

        ::RecoveryUI *const ui = GetUI();

        if (!WipeEseHook(ui)) {
            totalSuccess = false;
        }

        return totalSuccess;
    }
};

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

Device *make_device() {
    return new ::android::hardware::google::pixel::PixelWatchDevice(new ::WearRecoveryUI);
}
