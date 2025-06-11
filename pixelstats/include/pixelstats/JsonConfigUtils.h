/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef HARDWARE_GOOGLE_PIXEL_PIXELSTATS_JSON_CONFIG_UTILS_H
#define HARDWARE_GOOGLE_PIXEL_PIXELSTATS_JSON_CONFIG_UTILS_H

#include <json/reader.h>
#include <string>
#include <vector>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

std::vector<std::string> readStringVectorFromJson(const Json::Value &jsonArr);
std::vector<std::pair<std::string, std::string>> readStringPairVectorFromJson(const Json::Value &jsonArr);
std::string getCStringOrDefault(const Json::Value configData, const std::string& key);
int getIntOrDefault(const Json::Value configData, const std::string& key);

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android

#endif // HARDWARE_GOOGLE_PIXEL_PIXELSTATS_JSON_CONFIG_UTILS_H