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

#include <pixelstats/JsonConfigUtils.h>

#include <fstream>
#include <iostream>

namespace android {
namespace hardware {
namespace google {
namespace pixel {

// Helper function to read int vectors from JSON
std::vector<int> readIntVectorFromJson(const Json::Value &jsonArr) {
    std::vector<int> vec;
    if (jsonArr.isArray()) {  // Check if jsonArr is an array
        for (Json::Value::ArrayIndex i = 0; i < jsonArr.size(); ++i) {
            vec.push_back(jsonArr[i].asInt());
        }
    }
    return vec;
}

// Helper function to read string vectors from JSON
std::vector<std::string> readStringVectorFromJson(const Json::Value &jsonArr) {
    std::vector<std::string> vec;
    if (jsonArr.isArray()) { // Check if jsonArr is an array
        for (Json::Value::ArrayIndex i = 0; i < jsonArr.size(); ++i) {
            vec.push_back(jsonArr[i].asString());
        }
    }
    return vec;
}

// Helper function to read pairs of strings from JSON
std::vector<std::pair<std::string, std::string>>
readStringPairVectorFromJson(const Json::Value &jsonArr) {
    std::vector<std::pair<std::string, std::string>> vec;
    if (jsonArr.isArray()) { // Check if jsonArr is an array
        for (Json::Value::ArrayIndex i = 0; i < jsonArr.size(); ++i) {
            const Json::Value& innerArr = jsonArr[i];
            if (innerArr.isArray() && innerArr.size() == 2) { // Check if inner array is valid
                vec.push_back({innerArr[0].asString(), innerArr[1].asString()});
            }
        }
    }
    return vec;
}

std::string getCStringOrDefault(const Json::Value configData, const std::string& key) {
    if (configData.isMember(key)) {
        return configData[key].asString();
    } else {
        return "";
    }
}

int getIntOrDefault(const Json::Value configData, const std::string& key) {
    if (configData.isMember(key) && configData[key].isInt()) {
        return configData[key].asInt();
    } else {
        return 0;
    }
}

}  // namespace pixel
}  // namespace google
}  // namespace hardware
}  // namespace android
