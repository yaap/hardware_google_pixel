/*
 * Copyright (C) 2022 The Android Open Source Project
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
#define ATRACE_TAG (ATRACE_TAG_THERMAL | ATRACE_TAG_HAL)

#include "thermal-helper.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <utils/Trace.h>

#include <iterator>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace aidl {
namespace android {
namespace hardware {
namespace thermal {
namespace implementation {

constexpr std::string_view kThermalSensorsRoot("/sys/devices/virtual/thermal");
constexpr std::string_view kSensorPrefix("thermal_zone");
constexpr std::string_view kCoolingDevicePrefix("cooling_device");
constexpr std::string_view kThermalNameFile("type");
constexpr std::string_view kSensorPolicyFile("policy");
constexpr std::string_view kSensorTempSuffix("temp");
constexpr std::string_view kSensorTripPointTempZeroFile("trip_point_0_temp");
constexpr std::string_view kSensorTripPointHystZeroFile("trip_point_0_hyst");
constexpr std::string_view kUserSpaceSuffix("user_space");
constexpr std::string_view kCoolingDeviceCurStateSuffix("cur_state");
constexpr std::string_view kCoolingDeviceMaxStateSuffix("max_state");
constexpr std::string_view kCoolingDeviceState2powerSuffix("state2power_table");
constexpr std::string_view kConfigProperty("vendor.thermal.config");
constexpr std::string_view kConfigDefaultFileName("thermal_info_config.json");
constexpr std::string_view kThermalGenlProperty("persist.vendor.enable.thermal.genl");
constexpr std::string_view kThermalDisabledProperty("vendor.disable.thermalhal.control");

namespace {
using ::android::base::StringPrintf;

std::unordered_map<std::string, std::string> parseThermalPathMap(std::string_view prefix) {
    std::unordered_map<std::string, std::string> path_map;
    std::unique_ptr<DIR, int (*)(DIR *)> dir(opendir(kThermalSensorsRoot.data()), closedir);
    if (!dir) {
        return path_map;
    }

    // std::filesystem is not available for vendor yet
    // see discussion: aosp/894015
    while (struct dirent *dp = readdir(dir.get())) {
        if (dp->d_type != DT_DIR) {
            continue;
        }

        if (!::android::base::StartsWith(dp->d_name, prefix.data())) {
            continue;
        }

        std::string path = ::android::base::StringPrintf("%s/%s/%s", kThermalSensorsRoot.data(),
                                                         dp->d_name, kThermalNameFile.data());
        std::string name;
        if (!::android::base::ReadFileToString(path, &name)) {
            PLOG(ERROR) << "Failed to read from " << path;
            continue;
        }

        path_map.emplace(
                ::android::base::Trim(name),
                ::android::base::StringPrintf("%s/%s", kThermalSensorsRoot.data(), dp->d_name));
    }

    return path_map;
}

}  // namespace

// dump additional traces for a given sensor
void ThermalHelperImpl::dumpTraces(std::string_view sensor_name) {
    if (!(sensor_info_map_.count(sensor_name.data()) &&
          sensor_status_map_.count(sensor_name.data()))) {
        LOG(ERROR) << sensor_name << " not part of sensor_info_map_ or sensor_status_map_";
        return;
    }

    // add trace for current sensor
    const auto &sensor_status = sensor_status_map_.at(sensor_name.data());
    ATRACE_INT((sensor_name.data() + std::string("-cached")).c_str(),
               static_cast<int>(sensor_status.thermal_cached.temp));

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    if (!sensor_info.virtual_sensor_info) {
        return;
    }

    if (sensor_info.virtual_sensor_info->vt_estimator) {
        sensor_info.virtual_sensor_info->vt_estimator->DumpTraces();
    }

    // dump traces for all dependent/linked sensors
    for (const auto &linked_sensor : sensor_info.virtual_sensor_info->linked_sensors) {
        dumpTraces(linked_sensor);
    }
}

// If the cdev_ceiling is higher than CDEV max_state, cap the cdev_ceiling to max_state.
void ThermalHelperImpl::maxCoolingRequestCheck(
        std::unordered_map<std::string, BindedCdevInfo> *binded_cdev_info_map) {
    for (auto &binded_cdev_info_pair : *binded_cdev_info_map) {
        const auto &cdev_info = cooling_device_info_map_.at(binded_cdev_info_pair.first);
        for (auto &cdev_ceiling : binded_cdev_info_pair.second.cdev_ceiling) {
            if (cdev_ceiling > cdev_info.max_state) {
                if (cdev_ceiling != std::numeric_limits<int>::max()) {
                    LOG(ERROR) << binded_cdev_info_pair.first << " cdev_ceiling:" << cdev_ceiling
                               << " is higher than max state:" << cdev_info.max_state;
                }
                cdev_ceiling = cdev_info.max_state;
            }
        }
    }
}

/*
 * Populate the sensor_name_to_file_map_ map by walking through the file tree,
 * reading the type file and assigning the temp file path to the map.  If we do
 * not succeed, abort.
 */
ThermalHelperImpl::ThermalHelperImpl(const NotificationCallback &cb)
    : thermal_watcher_(new ThermalWatcher(std::bind(&ThermalHelperImpl::thermalWatcherCallbackFunc,
                                                    this, std::placeholders::_1))),
      cb_(cb) {
    const std::string config_path =
            "/vendor/etc/" +
            ::android::base::GetProperty(kConfigProperty.data(), kConfigDefaultFileName.data());
    bool thermal_throttling_disabled =
            ::android::base::GetBoolProperty(kThermalDisabledProperty.data(), false);
    bool ret = true;
    Json::Value config;
    std::unordered_set<std::string> loaded_config_paths;
    if (!ParseThermalConfig(config_path, &config, &loaded_config_paths)) {
        LOG(ERROR) << "Failed to read JSON config";
        ret = false;
    }

    const std::string &comment = config["Comment"].asString();
    LOG(INFO) << "Comment: " << comment;

    if (!ParseCoolingDevice(config, &cooling_device_info_map_)) {
        LOG(ERROR) << "Failed to parse cooling device info config";
        ret = false;
    }

    if (!ParseSensorInfo(config, &sensor_info_map_)) {
        LOG(ERROR) << "Failed to parse sensor info config";
        ret = false;
    }

    auto tz_map = parseThermalPathMap(kSensorPrefix.data());
    if (!initializeSensorMap(tz_map)) {
        LOG(ERROR) << "Failed to initialize sensor map";
        ret = false;
    }

    auto cdev_map = parseThermalPathMap(kCoolingDevicePrefix.data());
    if (!initializeCoolingDevices(cdev_map)) {
        LOG(ERROR) << "Failed to initialize cooling device map";
        ret = false;
    }

    if (!power_files_.registerPowerRailsToWatch(config)) {
        LOG(ERROR) << "Failed to register power rails";
        ret = false;
    }

    if (ret) {
        if (!thermal_stats_helper_.initializeStats(config, sensor_info_map_,
                                                   cooling_device_info_map_, this)) {
            LOG(FATAL) << "Failed to initialize thermal stats";
        }
    }

    for (auto &name_status_pair : sensor_info_map_) {
        sensor_status_map_[name_status_pair.first] = {
                .severity = ThrottlingSeverity::NONE,
                .prev_hot_severity = ThrottlingSeverity::NONE,
                .prev_cold_severity = ThrottlingSeverity::NONE,
                .last_update_time = boot_clock::time_point::min(),
                .thermal_cached = {NAN, boot_clock::time_point::min()},
                .pending_notification = false,
                .override_status = {nullptr, false, false},
        };

        if (name_status_pair.second.throttling_info != nullptr) {
            if (!thermal_throttling_.registerThermalThrottling(
                        name_status_pair.first, name_status_pair.second.throttling_info,
                        cooling_device_info_map_)) {
                LOG(ERROR) << name_status_pair.first << " failed to register thermal throttling";
                ret = false;
                break;
            }

            // Update cooling device max state for default mode
            maxCoolingRequestCheck(&name_status_pair.second.throttling_info->binded_cdev_info_map);

            // Update cooling device max state for each profile mode
            for (auto &cdev_throttling_profile_pair :
                 name_status_pair.second.throttling_info->profile_map) {
                maxCoolingRequestCheck(&cdev_throttling_profile_pair.second);
            }
        }
        // Check the virtual sensor settings are valid
        if (name_status_pair.second.virtual_sensor_info != nullptr) {
            // Check if sub sensor setting is valid
            for (size_t i = 0;
                 i < name_status_pair.second.virtual_sensor_info->linked_sensors.size(); i++) {
                if (!isSubSensorValid(
                            name_status_pair.second.virtual_sensor_info->linked_sensors[i],
                            name_status_pair.second.virtual_sensor_info->linked_sensors_type[i])) {
                    LOG(ERROR) << name_status_pair.first << "'s link sensor "
                               << name_status_pair.second.virtual_sensor_info->linked_sensors[i]
                               << " is invalid";
                    ret = false;
                    break;
                }
            }

            // Check if the backup sensor is valid
            if (!name_status_pair.second.virtual_sensor_info->backup_sensor.empty()) {
                if (!isSubSensorValid(name_status_pair.second.virtual_sensor_info->backup_sensor,
                                      SensorFusionType::SENSOR)) {
                    LOG(ERROR) << name_status_pair.first << "'s backup sensor "
                               << name_status_pair.second.virtual_sensor_info->backup_sensor
                               << " is invalid";
                    ret = false;
                    break;
                }
            }

            // Check if the trigger sensor is valid
            if (!name_status_pair.second.virtual_sensor_info->trigger_sensors.empty() &&
                name_status_pair.second.is_watch) {
                for (size_t i = 0;
                     i < name_status_pair.second.virtual_sensor_info->trigger_sensors.size(); i++) {
                    if (sensor_info_map_.count(
                                name_status_pair.second.virtual_sensor_info->trigger_sensors[i])) {
                        sensor_info_map_[name_status_pair.second.virtual_sensor_info
                                                 ->trigger_sensors[i]]
                                .is_watch = true;
                    } else {
                        LOG(ERROR)
                                << name_status_pair.first << "'s trigger sensor: "
                                << name_status_pair.second.virtual_sensor_info->trigger_sensors[i]
                                << " is invalid";
                        ret = false;
                        break;
                    }
                }
            }

            // Check if the severity reference sensor is valid
            if (name_status_pair.second.severity_reference != "") {
                if (sensor_info_map_.contains(name_status_pair.second.severity_reference)) {
                    sensor_info_map_[name_status_pair.second.severity_reference].is_watch = true;
                    LOG(INFO) << "Enable is_watch for " << name_status_pair.first
                              << "'s severity reference sensor: "
                              << name_status_pair.second.severity_reference;
                } else {
                    LOG(ERROR) << name_status_pair.first << "'s severity reference sensor: "
                               << name_status_pair.second.severity_reference << " is invalid";
                    ret = false;
                }
            }
        }
        // Check predictor info config
        if (name_status_pair.second.predictor_info != nullptr) {
            std::string predict_sensor_name = name_status_pair.second.predictor_info->sensor;
            if (!(sensor_info_map_.count(predict_sensor_name))) {
                LOG(ERROR) << name_status_pair.first << "'s predictor " << predict_sensor_name
                           << " is not part of sensor_info_map_";
                ret = false;
                break;
            }

            const auto &predictor_sensor_info = sensor_info_map_.at(predict_sensor_name);
            if (predictor_sensor_info.virtual_sensor_info == nullptr ||
                predictor_sensor_info.virtual_sensor_info->vt_estimator == nullptr) {
                LOG(ERROR) << name_status_pair.first << "'s predictor " << predict_sensor_name
                           << " does not support prediction";
                ret = false;
                break;
            }

            if (name_status_pair.second.predictor_info->support_pid_compensation) {
                std::vector<float> output_template;
                size_t prediction_weight_count =
                        name_status_pair.second.predictor_info->prediction_weights.size();
                // read predictor out to get the size of output vector
                ::thermal::vtestimator::VtEstimatorStatus predict_check =
                        predictor_sensor_info.virtual_sensor_info->vt_estimator->GetAllPredictions(
                                &output_template);

                if (predict_check != ::thermal::vtestimator::kVtEstimatorOk) {
                    LOG(ERROR) << "Failed to get output size of " << name_status_pair.first
                               << "'s predictor " << predict_sensor_name
                               << " GetAllPredictions ret: " << ret << ")";
                    ret = false;
                    break;
                }

                if (prediction_weight_count != output_template.size()) {
                    LOG(ERROR) << "Sensor [" << name_status_pair.first << "]: "
                               << "prediction weights size (" << prediction_weight_count
                               << ") doesn't match predictor [" << predict_sensor_name
                               << "]'s output size (" << output_template.size() << ")";
                    ret = false;
                    break;
                }
            }
        }
    }

    if (!power_hal_service_.connect()) {
        LOG(ERROR) << "Fail to connect to Power Hal";
    } else {
        power_hal_service_.updateSupportedPowerHints(sensor_info_map_);
    }

    if (thermal_throttling_disabled) {
        if (ret) {
            clearAllThrottling();
            is_initialized_ = ret;
            return;
        } else {
            sensor_info_map_.clear();
            cooling_device_info_map_.clear();
            return;
        }
    } else if (!ret) {
        LOG(FATAL) << "ThermalHAL could not be initialized properly.";
    }
    is_initialized_ = ret;

    const bool thermal_genl_enabled =
            ::android::base::GetBoolProperty(kThermalGenlProperty.data(), false);

    std::set<std::string> monitored_sensors;
    initializeTrip(tz_map, &monitored_sensors, thermal_genl_enabled);

    if (thermal_genl_enabled) {
        thermal_watcher_->registerFilesToWatchNl(monitored_sensors);
    } else {
        thermal_watcher_->registerFilesToWatch(monitored_sensors);
    }

    // Need start watching after status map initialized
    is_initialized_ = thermal_watcher_->startWatchingDeviceFiles();
    if (!is_initialized_) {
        LOG(FATAL) << "ThermalHAL could not start watching thread properly.";
    }
}

bool getThermalZoneTypeById(int tz_id, std::string *type) {
    std::string tz_type;
    std::string path =
            ::android::base::StringPrintf("%s/%s%d/%s", kThermalSensorsRoot.data(),
                                          kSensorPrefix.data(), tz_id, kThermalNameFile.data());
    if (!::android::base::ReadFileToString(path, &tz_type)) {
        LOG(ERROR) << "Failed to read sensor from: " << path;
        return false;
    }

    // Strip the newline.
    *type = ::android::base::Trim(tz_type);
    LOG(INFO) << "TZ path: " << path << " type: " << *type;
    return true;
}

void ThermalHelperImpl::checkUpdateSensorForEmul(std::string_view target_sensor,
                                                 const bool max_throttling) {
    // Force update all the sensors which are related to the target emul sensor
    for (auto &[sensor_name, sensor_info] : sensor_info_map_) {
        if (sensor_info.virtual_sensor_info == nullptr || !sensor_info.is_watch) {
            continue;
        }

        const auto &linked_sensors = sensor_info.virtual_sensor_info->linked_sensors;
        if (std::find(linked_sensors.begin(), linked_sensors.end(), target_sensor) ==
            linked_sensors.end()) {
            continue;
        }

        auto &sensor_status = sensor_status_map_.at(sensor_name.data());
        sensor_status.override_status.max_throttling = max_throttling;
        sensor_status.override_status.pending_update = true;

        checkUpdateSensorForEmul(sensor_name, max_throttling);
    }
}

bool ThermalHelperImpl::emulTemp(std::string_view target_sensor, const float temp,
                                 const bool max_throttling) {
    LOG(INFO) << "Set " << target_sensor.data() << " emul_temp: " << temp
              << " max_throttling: " << max_throttling;

    std::lock_guard<std::shared_mutex> _lock(sensor_status_map_mutex_);

    // Check the target sensor is valid
    if (!sensor_status_map_.count(target_sensor.data())) {
        LOG(ERROR) << "Cannot find target emul sensor: " << target_sensor.data();
        return false;
    }

    auto &sensor_status = sensor_status_map_.at(target_sensor.data());

    sensor_status.override_status.emul_temp.reset(new EmulTemp{temp, -1});
    sensor_status.override_status.max_throttling = max_throttling;
    sensor_status.override_status.pending_update = true;

    checkUpdateSensorForEmul(target_sensor.data(), max_throttling);

    thermal_watcher_->wake();
    return true;
}

bool ThermalHelperImpl::emulSeverity(std::string_view target_sensor, const int severity,
                                     const bool max_throttling) {
    LOG(INFO) << "Set " << target_sensor.data() << " emul_severity: " << severity
              << " max_throttling: " << max_throttling;

    std::lock_guard<std::shared_mutex> _lock(sensor_status_map_mutex_);
    // Check the target sensor is valid
    if (!sensor_status_map_.count(target_sensor.data()) ||
        !sensor_info_map_.count(target_sensor.data())) {
        LOG(ERROR) << "Cannot find target emul sensor: " << target_sensor.data();
        return false;
    }
    const auto &sensor_info = sensor_info_map_.at(target_sensor.data());

    // Check the emul severity is valid
    if (severity > static_cast<int>(kThrottlingSeverityCount)) {
        LOG(ERROR) << "Invalid emul severity value " << severity;
        return false;
    }

    const auto temp = sensor_info.hot_thresholds[severity] / sensor_info.multiplier;

    auto &sensor_status = sensor_status_map_.at(target_sensor.data());

    sensor_status.override_status.emul_temp.reset(new EmulTemp{temp, severity});
    sensor_status.override_status.max_throttling = max_throttling;
    sensor_status.override_status.pending_update = true;

    checkUpdateSensorForEmul(target_sensor.data(), max_throttling);

    thermal_watcher_->wake();
    return true;
}

bool ThermalHelperImpl::emulClear(std::string_view target_sensor) {
    LOG(INFO) << "Clear " << target_sensor.data() << " emulation settings";

    std::lock_guard<std::shared_mutex> _lock(sensor_status_map_mutex_);
    if (target_sensor == "all") {
        for (auto &[sensor_name, sensor_status] : sensor_status_map_) {
            sensor_status.override_status = {
                    .emul_temp = nullptr, .max_throttling = false, .pending_update = true};
            checkUpdateSensorForEmul(sensor_name, false);
        }
    } else if (sensor_status_map_.count(target_sensor.data())) {
        auto &sensor_status = sensor_status_map_.at(target_sensor.data());
        sensor_status.override_status = {
                .emul_temp = nullptr, .max_throttling = false, .pending_update = true};
        checkUpdateSensorForEmul(target_sensor.data(), false);
    } else {
        LOG(ERROR) << "Cannot find target emul sensor: " << target_sensor.data();
        return false;
    }

    thermal_watcher_->wake();
    return true;
}

bool ThermalHelperImpl::readCoolingDevice(std::string_view cooling_device,
                                          CoolingDevice *out) const {
    // Read the file.  If the file can't be read temp will be empty string.
    std::string data;

    if (!cooling_devices_.readThermalFile(cooling_device, &data)) {
        LOG(ERROR) << "readCoolingDevice: failed to read cooling_device: " << cooling_device;
        return false;
    }

    const CdevInfo &cdev_info = cooling_device_info_map_.at(cooling_device.data());
    const CoolingType &type = cdev_info.type;

    out->type = type;
    out->name = cooling_device.data();
    out->value = std::stoi(data);

    return true;
}

bool ThermalHelperImpl::readTemperature(std::string_view sensor_name, Temperature *out,
                                        const bool force_no_cache) {
    // Return fail if the thermal sensor cannot be read.
    float temp = NAN;
    std::map<std::string, float> sensor_log_map;
    auto &sensor_status = sensor_status_map_.at(sensor_name.data());

    if (!readThermalSensor(sensor_name, &temp, force_no_cache, &sensor_log_map)) {
        LOG(ERROR) << "Failed to read thermal sensor " << sensor_name.data();
        thermal_stats_helper_.reportThermalAbnormality(
                ThermalSensorAbnormalityDetected::TEMP_READ_FAIL, sensor_name, std::nullopt);
        return false;
    }

    if (std::isnan(temp)) {
        LOG(INFO) << "Sensor " << sensor_name.data() << " temperature is nan.";
        return false;
    }
    const auto severity_reference = getSeverityReference(sensor_name.data());

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    out->type = sensor_info.type;
    out->name = sensor_name.data();
    out->value = temp * sensor_info.multiplier;

    std::pair<ThrottlingSeverity, ThrottlingSeverity> status =
            std::make_pair(ThrottlingSeverity::NONE, ThrottlingSeverity::NONE);

    // Only update status if the thermal sensor is being monitored
    if (!sensor_info.is_watch) {
        return true;
    }
    ThrottlingSeverity prev_hot_severity, prev_cold_severity;
    {
        std::unique_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
        prev_hot_severity = sensor_status.prev_hot_severity;
        prev_cold_severity = sensor_status.prev_cold_severity;
        status = getSeverityFromThresholds(sensor_info.hot_thresholds, sensor_info.cold_thresholds,
                                           sensor_info.hot_hysteresis, sensor_info.cold_hysteresis,
                                           prev_hot_severity, prev_cold_severity, out->value);

        out->throttlingStatus =
                static_cast<size_t>(status.first) > static_cast<size_t>(status.second)
                        ? status.first
                        : status.second;

        if (status.first != sensor_status.prev_hot_severity) {
            sensor_status.prev_hot_severity = status.first;
        }
        if (status.second != sensor_status.prev_cold_severity) {
            sensor_status.prev_cold_severity = status.second;
        }

        out->throttlingStatus = std::max(out->throttlingStatus, severity_reference);

        if (sensor_status.override_status.emul_temp != nullptr &&
            sensor_status.override_status.emul_temp->severity >= 0) {
            out->throttlingStatus = static_cast<ThrottlingSeverity>(
                    sensor_status.override_status.emul_temp->severity);
        }

        if (sensor_status.severity != out->throttlingStatus) {
            sensor_status.severity = out->throttlingStatus;
            sensor_status.pending_notification = true;
        }
    }

    std::ostringstream sensor_log;
    for (const auto &sensor_log_pair : sensor_log_map) {
        sensor_log << sensor_log_pair.first << ":" << sensor_log_pair.second << " ";
    }
    // Update sensor temperature time in state
    thermal_stats_helper_.updateSensorTempStatsBySeverity(sensor_name, out->throttlingStatus);
    if (out->throttlingStatus >= sensor_info.log_level) {
        LOG(INFO) << sensor_name.data() << ":" << out->value << " raw data: " << sensor_log.str();
    } else {
        LOG(VERBOSE) << sensor_name.data() << ":" << out->value
                     << " raw data: " << sensor_log.str();
    }
    ATRACE_INT((sensor_name.data() + std::string("-severity")).c_str(),
               static_cast<int>(out->throttlingStatus));

    return true;
}

bool ThermalHelperImpl::readTemperatureThreshold(std::string_view sensor_name,
                                                 TemperatureThreshold *out) const {
    // Read the file.  If the file can't be read temp will be empty string.
    std::string temp;
    std::string path;

    if (!sensor_info_map_.count(sensor_name.data())) {
        LOG(ERROR) << __func__ << ": sensor not found: " << sensor_name;
        return false;
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());

    out->type = sensor_info.type;
    out->name = sensor_name.data();
    out->hotThrottlingThresholds =
            std::vector(sensor_info.hot_thresholds.begin(), sensor_info.hot_thresholds.end());
    out->coldThrottlingThresholds =
            std::vector(sensor_info.cold_thresholds.begin(), sensor_info.cold_thresholds.end());
    return true;
}

void ThermalHelperImpl::updateCoolingDevices(const std::vector<std::string> &updated_cdev) {
    int max_state;

    for (const auto &target_cdev : updated_cdev) {
        if (thermal_throttling_.getCdevMaxRequest(target_cdev, &max_state)) {
            if (cooling_devices_.writeCdevFile(target_cdev, std::to_string(max_state))) {
                ATRACE_INT(target_cdev.c_str(), max_state);
                LOG(INFO) << "Successfully update cdev " << target_cdev << " sysfs to "
                          << max_state;
            } else {
                LOG(ERROR) << "Failed to update cdev " << target_cdev << " sysfs to " << max_state;
            }
        }
    }
}

std::pair<ThrottlingSeverity, ThrottlingSeverity> ThermalHelperImpl::getSeverityFromThresholds(
        const ThrottlingArray &hot_thresholds, const ThrottlingArray &cold_thresholds,
        const ThrottlingArray &hot_hysteresis, const ThrottlingArray &cold_hysteresis,
        ThrottlingSeverity prev_hot_severity, ThrottlingSeverity prev_cold_severity,
        float value) const {
    ThrottlingSeverity ret_hot = ThrottlingSeverity::NONE;
    ThrottlingSeverity ret_hot_hysteresis = ThrottlingSeverity::NONE;
    ThrottlingSeverity ret_cold = ThrottlingSeverity::NONE;
    ThrottlingSeverity ret_cold_hysteresis = ThrottlingSeverity::NONE;

    // Here we want to control the iteration from high to low, and ::ndk::enum_range doesn't support
    // a reverse iterator yet.
    for (size_t i = static_cast<size_t>(ThrottlingSeverity::SHUTDOWN);
         i > static_cast<size_t>(ThrottlingSeverity::NONE); --i) {
        if (!std::isnan(hot_thresholds[i]) && hot_thresholds[i] <= value &&
            ret_hot == ThrottlingSeverity::NONE) {
            ret_hot = static_cast<ThrottlingSeverity>(i);
        }
        if (!std::isnan(hot_thresholds[i]) && (hot_thresholds[i] - hot_hysteresis[i]) < value &&
            ret_hot_hysteresis == ThrottlingSeverity::NONE) {
            ret_hot_hysteresis = static_cast<ThrottlingSeverity>(i);
        }
        if (!std::isnan(cold_thresholds[i]) && cold_thresholds[i] >= value &&
            ret_cold == ThrottlingSeverity::NONE) {
            ret_cold = static_cast<ThrottlingSeverity>(i);
        }
        if (!std::isnan(cold_thresholds[i]) && (cold_thresholds[i] + cold_hysteresis[i]) > value &&
            ret_cold_hysteresis == ThrottlingSeverity::NONE) {
            ret_cold_hysteresis = static_cast<ThrottlingSeverity>(i);
        }
    }
    if (static_cast<size_t>(ret_hot) < static_cast<size_t>(prev_hot_severity)) {
        ret_hot = ret_hot_hysteresis;
    }
    if (static_cast<size_t>(ret_cold) < static_cast<size_t>(prev_cold_severity)) {
        ret_cold = ret_cold_hysteresis;
    }

    return std::make_pair(ret_hot, ret_cold);
}

bool ThermalHelperImpl::isSubSensorValid(std::string_view sensor_data,
                                         const SensorFusionType sensor_fusion_type) {
    switch (sensor_fusion_type) {
        case SensorFusionType::SENSOR:
            if (!sensor_info_map_.count(sensor_data.data())) {
                LOG(ERROR) << "Cannot find " << sensor_data.data() << " from sensor info map";
                return false;
            }
            break;
        case SensorFusionType::ODPM:
            if (!GetPowerStatusMap().count(sensor_data.data())) {
                LOG(ERROR) << "Cannot find " << sensor_data.data() << " from power status map";
                return false;
            }
            break;
        default:
            break;
    }
    return true;
}

void ThermalHelperImpl::clearAllThrottling(void) {
    // Clear the CDEV request
    for (const auto &cdev_info_pair : cooling_device_info_map_) {
        cooling_devices_.writeCdevFile(cdev_info_pair.first, "0");
    }

    for (auto &sensor_info_pair : sensor_info_map_) {
        sensor_info_pair.second.is_watch = false;
        sensor_info_pair.second.throttling_info.reset();
        sensor_info_pair.second.hot_thresholds.fill(NAN);
        sensor_info_pair.second.cold_thresholds.fill(NAN);
        Temperature temp = {
                .type = sensor_info_pair.second.type,
                .name = sensor_info_pair.first,
                .value = NAN,
                .throttlingStatus = ThrottlingSeverity::NONE,
        };
        // Send callbacks with NONE severity
        if (sensor_info_pair.second.send_cb && cb_) {
            cb_(temp);
        }
        // Disable thermal power hints
        if (sensor_info_pair.second.send_powerhint) {
            for (const auto &severity : ::ndk::enum_range<ThrottlingSeverity>()) {
                power_hal_service_.setMode(sensor_info_pair.first, severity, false);
            }
        }
    }
}

bool ThermalHelperImpl::initializeSensorMap(
        const std::unordered_map<std::string, std::string> &path_map) {
    for (const auto &sensor_info_pair : sensor_info_map_) {
        std::string_view sensor_name = sensor_info_pair.first;
        if (sensor_info_pair.second.virtual_sensor_info != nullptr) {
            continue;
        }
        if (!path_map.count(sensor_name.data())) {
            LOG(ERROR) << "Could not find " << sensor_name << " in sysfs";
            return false;
        }

        std::string path;
        if (sensor_info_pair.second.temp_path.empty()) {
            path = ::android::base::StringPrintf("%s/%s", path_map.at(sensor_name.data()).c_str(),
                                                 kSensorTempSuffix.data());
        } else {
            path = sensor_info_pair.second.temp_path;
        }

        if (!thermal_sensors_.addThermalFile(sensor_name, path)) {
            LOG(ERROR) << "Could not add " << sensor_name << "to sensors map";
            return false;
        }
    }
    return true;
}

bool ThermalHelperImpl::initializeCoolingDevices(
        const std::unordered_map<std::string, std::string> &path_map) {
    for (auto &cooling_device_info_pair : cooling_device_info_map_) {
        std::string cooling_device_name = cooling_device_info_pair.first;
        if (!path_map.count(cooling_device_name)) {
            LOG(ERROR) << "Could not find " << cooling_device_name << " in sysfs";
            return false;
        }
        // Add cooling device path for thermalHAL to get current state
        std::string_view path = path_map.at(cooling_device_name);
        std::string read_path;
        if (!cooling_device_info_pair.second.read_path.empty()) {
            read_path = cooling_device_info_pair.second.read_path.data();
        } else {
            read_path = ::android::base::StringPrintf("%s/%s", path.data(),
                                                      kCoolingDeviceCurStateSuffix.data());
        }
        if (!cooling_devices_.addThermalFile(cooling_device_name, read_path)) {
            LOG(ERROR) << "Could not add " << cooling_device_name
                       << " read path to cooling device map";
            return false;
        }

        // Get cooling device state2power table from sysfs if not defined in config
        if (!cooling_device_info_pair.second.state2power.size()) {
            std::string state2power_path = ::android::base::StringPrintf(
                    "%s/%s", path.data(), kCoolingDeviceState2powerSuffix.data());
            std::string state2power_str;
            if (::android::base::ReadFileToString(state2power_path, &state2power_str)) {
                LOG(INFO) << "Cooling device " << cooling_device_info_pair.first
                          << " use State2power read from sysfs";
                std::stringstream power(state2power_str);
                unsigned int power_number;
                while (power >> power_number) {
                    cooling_device_info_pair.second.state2power.push_back(
                            static_cast<float>(power_number));
                }
            }
        }

        // Check if there's any wrong ordered state2power value to avoid cdev stuck issue
        for (size_t i = 0; i < cooling_device_info_pair.second.state2power.size(); ++i) {
            LOG(INFO) << "Cooling device " << cooling_device_info_pair.first << " state:" << i
                      << " power: " << cooling_device_info_pair.second.state2power[i];
            if (i > 0 && cooling_device_info_pair.second.state2power[i] >
                                 cooling_device_info_pair.second.state2power[i - 1]) {
                LOG(ERROR) << "Higher power with higher state on cooling device "
                           << cooling_device_info_pair.first << "'s state" << i;
            }
        }

        // Get max cooling device request state
        std::string max_state;
        std::string max_state_path = ::android::base::StringPrintf(
                "%s/%s", path.data(), kCoolingDeviceMaxStateSuffix.data());
        if (!::android::base::ReadFileToString(max_state_path, &max_state)) {
            LOG(ERROR) << cooling_device_info_pair.first
                       << " could not open max state file:" << max_state_path;
            cooling_device_info_pair.second.max_state = std::numeric_limits<int>::max();
        } else {
            cooling_device_info_pair.second.max_state = std::stoi(::android::base::Trim(max_state));
            LOG(INFO) << "Cooling device " << cooling_device_info_pair.first
                      << " max state: " << cooling_device_info_pair.second.max_state
                      << " state2power number: "
                      << cooling_device_info_pair.second.state2power.size();
            if (cooling_device_info_pair.second.state2power.size() > 0 &&
                static_cast<int>(cooling_device_info_pair.second.state2power.size()) !=
                        (cooling_device_info_pair.second.max_state + 1)) {
                LOG(ERROR) << "Invalid state2power number: "
                           << cooling_device_info_pair.second.state2power.size()
                           << ", number should be " << cooling_device_info_pair.second.max_state + 1
                           << " (max_state + 1)";
                return false;
            }
        }

        // Add cooling device path for thermalHAL to request state
        cooling_device_name =
                ::android::base::StringPrintf("%s_%s", cooling_device_name.c_str(), "w");
        std::string write_path;
        if (!cooling_device_info_pair.second.write_path.empty()) {
            write_path = cooling_device_info_pair.second.write_path.data();
        } else {
            write_path = ::android::base::StringPrintf("%s/%s", path.data(),
                                                       kCoolingDeviceCurStateSuffix.data());
        }

        if (!cooling_devices_.addThermalFile(cooling_device_name, write_path)) {
            LOG(ERROR) << "Could not add " << cooling_device_name
                       << " write path to cooling device map";
            return false;
        }
    }
    return true;
}

void ThermalHelperImpl::setMinTimeout(SensorInfo *sensor_info) {
    sensor_info->polling_delay = kMinPollIntervalMs;
    sensor_info->passive_delay = kMinPollIntervalMs;
}

void ThermalHelperImpl::initializeTrip(const std::unordered_map<std::string, std::string> &path_map,
                                       std::set<std::string> *monitored_sensors,
                                       bool thermal_genl_enabled) {
    for (auto &sensor_info : sensor_info_map_) {
        if (!sensor_info.second.is_watch || (sensor_info.second.virtual_sensor_info != nullptr)) {
            continue;
        }

        bool trip_update = false;
        std::string_view sensor_name = sensor_info.first;
        std::string_view tz_path = path_map.at(sensor_name.data());
        std::string tz_policy;
        std::string path =
                ::android::base::StringPrintf("%s/%s", (tz_path.data()), kSensorPolicyFile.data());

        if (thermal_genl_enabled) {
            trip_update = true;
        } else {
            // Check if thermal zone support uevent notify
            if (!::android::base::ReadFileToString(path, &tz_policy)) {
                LOG(ERROR) << sensor_name << " could not open tz policy file:" << path;
            } else {
                tz_policy = ::android::base::Trim(tz_policy);
                if (tz_policy != kUserSpaceSuffix) {
                    LOG(ERROR) << sensor_name << " does not support uevent notify";
                } else {
                    trip_update = true;
                }
            }
        }
        if (trip_update) {
            // Update thermal zone trip point
            for (size_t i = 0; i < kThrottlingSeverityCount; ++i) {
                if (!std::isnan(sensor_info.second.hot_thresholds[i]) &&
                    !std::isnan(sensor_info.second.hot_hysteresis[i])) {
                    // Update trip_point_0_temp threshold
                    std::string threshold = std::to_string(std::lround(
                            sensor_info.second.hot_thresholds[i] / sensor_info.second.multiplier));
                    path = ::android::base::StringPrintf("%s/%s", (tz_path.data()),
                                                         kSensorTripPointTempZeroFile.data());
                    if (!::android::base::WriteStringToFile(threshold, path)) {
                        LOG(ERROR) << "fail to update " << sensor_name << " trip point: " << path
                                   << " to " << threshold;
                        trip_update = false;
                        break;
                    }
                    // Update trip_point_0_hyst threshold
                    threshold = std::to_string(std::lround(sensor_info.second.hot_hysteresis[i] /
                                                           sensor_info.second.multiplier));
                    path = ::android::base::StringPrintf("%s/%s", (tz_path.data()),
                                                         kSensorTripPointHystZeroFile.data());
                    if (!::android::base::WriteStringToFile(threshold, path)) {
                        LOG(ERROR) << "fail to update " << sensor_name << "trip hyst" << threshold
                                   << path;
                        trip_update = false;
                        break;
                    }
                    break;
                } else if (i == kThrottlingSeverityCount - 1) {
                    LOG(ERROR) << sensor_name << ":all thresholds are NAN";
                    trip_update = false;
                    break;
                }
            }
            monitored_sensors->insert(sensor_info.first);
        }

        if (!trip_update) {
            LOG(INFO) << "config Sensor: " << sensor_info.first
                      << " to default polling interval: " << kMinPollIntervalMs.count();
            setMinTimeout(&sensor_info.second);
        }
    }
}

bool ThermalHelperImpl::fillCurrentTemperatures(bool filterType, bool filterCallback,
                                                TemperatureType type,
                                                std::vector<Temperature> *temperatures) {
    std::vector<Temperature> ret;
    for (const auto &name_info_pair : sensor_info_map_) {
        Temperature temp;
        if (name_info_pair.second.is_hidden) {
            continue;
        }
        if (filterType && name_info_pair.second.type != type) {
            continue;
        }
        if (filterCallback && !name_info_pair.second.send_cb) {
            continue;
        }
        if (readTemperature(name_info_pair.first, &temp, false)) {
            ret.emplace_back(std::move(temp));
        } else {
            LOG(ERROR) << __func__
                       << ": error reading temperature for sensor: " << name_info_pair.first;
        }
    }
    *temperatures = ret;
    return ret.size() > 0;
}

bool ThermalHelperImpl::fillTemperatureThresholds(
        bool filterType, TemperatureType type,
        std::vector<TemperatureThreshold> *thresholds) const {
    std::vector<TemperatureThreshold> ret;
    for (const auto &name_info_pair : sensor_info_map_) {
        TemperatureThreshold temp;
        if (name_info_pair.second.is_hidden) {
            continue;
        }
        if (filterType && name_info_pair.second.type != type) {
            continue;
        }
        if (readTemperatureThreshold(name_info_pair.first, &temp)) {
            ret.emplace_back(std::move(temp));
        } else {
            LOG(ERROR) << __func__ << ": error reading temperature threshold for sensor: "
                       << name_info_pair.first;
            return false;
        }
    }
    *thresholds = ret;
    return ret.size() > 0;
}

bool ThermalHelperImpl::fillCurrentCoolingDevices(
        bool filterType, CoolingType type, std::vector<CoolingDevice> *cooling_devices) const {
    std::vector<CoolingDevice> ret;
    for (const auto &name_info_pair : cooling_device_info_map_) {
        CoolingDevice value;
        if (filterType && name_info_pair.second.type != type) {
            continue;
        }
        if (readCoolingDevice(name_info_pair.first, &value)) {
            ret.emplace_back(std::move(value));
        } else {
            LOG(ERROR) << __func__ << ": error reading cooling device: " << name_info_pair.first;
            return false;
        }
    }
    *cooling_devices = ret;
    return ret.size() > 0;
}

ThrottlingSeverity ThermalHelperImpl::getSeverityReference(std::string_view sensor_name) {
    if (!sensor_info_map_.contains(sensor_name.data())) {
        return ThrottlingSeverity::NONE;
    }
    const std::string &severity_reference =
            sensor_info_map_.at(sensor_name.data()).severity_reference;
    if (severity_reference == "") {
        return ThrottlingSeverity::NONE;
    }

    Temperature temp;
    if (!readTemperature(severity_reference, &temp, false)) {
        return ThrottlingSeverity::NONE;
    }
    LOG(VERBOSE) << sensor_name << "'s severity reference " << severity_reference
                 << " reading:" << toString(temp.throttlingStatus);
    return temp.throttlingStatus;
}

bool ThermalHelperImpl::readDataByType(std::string_view sensor_data, float *reading_value,
                                       const SensorFusionType type, const bool force_no_cache,
                                       std::map<std::string, float> *sensor_log_map) {
    switch (type) {
        case SensorFusionType::SENSOR:
            if (!readThermalSensor(sensor_data.data(), reading_value, force_no_cache,
                                   sensor_log_map)) {
                LOG(ERROR) << "Failed to get " << sensor_data.data() << " data";
                return false;
            }
            break;
        case SensorFusionType::ODPM:
            *reading_value = GetPowerStatusMap().at(sensor_data.data()).last_updated_avg_power;
            if (std::isnan(*reading_value)) {
                LOG(INFO) << "Power data " << sensor_data.data() << " is under collecting";
                return true;
            }
            (*sensor_log_map)[sensor_data.data()] = *reading_value;
            break;
        case SensorFusionType::CONSTANT:
            *reading_value = std::atof(sensor_data.data());
            break;
        case SensorFusionType::CDEV:
            int max_state;
            if (thermal_throttling_.getCdevMaxRequest(sensor_data.data(), &max_state)) {
                *reading_value = max_state;
                break;
            } else {
                return false;
            }
            break;
        default:
            break;
    }
    return true;
}

bool ThermalHelperImpl::runVirtualTempEstimator(std::string_view sensor_name,
                                                std::map<std::string, float> *sensor_log_map,
                                                const bool force_no_cache,
                                                std::vector<float> *outputs) {
    std::vector<float> model_inputs;
    std::vector<float> model_outputs;

    ATRACE_NAME(StringPrintf("ThermalHelper::runVirtualTempEstimator - %s", sensor_name.data())
                        .c_str());
    if (!(sensor_info_map_.count(sensor_name.data()) &&
          sensor_status_map_.count(sensor_name.data()))) {
        LOG(ERROR) << sensor_name << " not part of sensor_info_map_ or sensor_status_map_";
        return false;
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    if (sensor_info.virtual_sensor_info == nullptr ||
        sensor_info.virtual_sensor_info->vt_estimator == nullptr) {
        LOG(ERROR) << "vt_estimator not valid for " << sensor_name;
        return false;
    }

    if (outputs == nullptr) {
        LOG(ERROR) << "vt_estimator output is nullptr";
        return false;
    }

    model_inputs.reserve(sensor_info.virtual_sensor_info->linked_sensors.size());

    for (size_t i = 0; i < sensor_info.virtual_sensor_info->linked_sensors.size(); i++) {
        std::string linked_sensor = sensor_info.virtual_sensor_info->linked_sensors[i];

        if ((*sensor_log_map).count(linked_sensor.data())) {
            float value = (*sensor_log_map)[linked_sensor.data()];
            model_inputs.push_back(value);
        } else {
            LOG(ERROR) << "failed to read sensor: " << linked_sensor;
            return false;
        }
    }

    ::thermal::vtestimator::VtEstimatorStatus ret =
            sensor_info.virtual_sensor_info->vt_estimator->Estimate(model_inputs, &model_outputs);

    if (ret == ::thermal::vtestimator::kVtEstimatorOk) {
        *outputs = model_outputs;
        return true;
    } else if (ret == ::thermal::vtestimator::kVtEstimatorLowConfidence ||
               ret == ::thermal::vtestimator::kVtEstimatorUnderSampling) {
        std::string_view backup_sensor = sensor_info.virtual_sensor_info->backup_sensor;
        float backup_sensor_vt;
        if (backup_sensor.empty()) {
            LOG(ERROR) << "Failed to run estimator (ret: " << ret << ") for " << sensor_name
                       << " with no backup.";
            return false;
        }
        LOG(INFO) << "VT Estimator returned (ret: " << ret << ") for " << sensor_name
                  << ". Reading backup sensor [" << backup_sensor << "] data to use";
        if (!readDataByType(backup_sensor, &backup_sensor_vt, SensorFusionType::SENSOR,
                            force_no_cache, sensor_log_map)) {
            LOG(ERROR) << "Failed to read " << sensor_name.data() << "'s backup sensor "
                       << backup_sensor;
            return false;
        }
        model_outputs.clear();
        model_outputs.push_back(backup_sensor_vt);
        *outputs = model_outputs;
        return true;
    }

    LOG(ERROR) << "Failed to run estimator (ret: " << ret << ") for " << sensor_name;
    return false;
}

void ThermalHelperImpl::dumpVtEstimatorStatus(std::string_view sensor_name,
                                              std::ostringstream *dump_buf) const {
    if (!(sensor_info_map_.count(sensor_name.data()) &&
          sensor_status_map_.count(sensor_name.data()))) {
        LOG(ERROR) << sensor_name << " not part of sensor_info_map_ or sensor_status_map_";
        return;
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    if (sensor_info.virtual_sensor_info == nullptr ||
        sensor_info.virtual_sensor_info->vt_estimator == nullptr) {
        return;
    }

    sensor_info.virtual_sensor_info->vt_estimator->DumpStatus(sensor_name, dump_buf);
}

size_t ThermalHelperImpl::getPredictionMaxWindowMs(std::string_view sensor_name) {
    size_t predict_window = 0;

    ATRACE_NAME(StringPrintf("ThermalHelper::getPredictionMaxWindowMs - %s", sensor_name.data())
                        .c_str());

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    if (sensor_info.predictor_info == nullptr) {
        LOG(ERROR) << "No predictor info found for sensor: " << sensor_name;
        return 0;
    }

    std::string_view predict_sensor_name = sensor_info.predictor_info->sensor;
    const auto &predictor_sensor_info = sensor_info_map_.at(predict_sensor_name.data());
    ::thermal::vtestimator::VtEstimatorStatus ret =
            predictor_sensor_info.virtual_sensor_info->vt_estimator->GetMaxPredictWindowMs(
                    &predict_window);

    if (ret != ::thermal::vtestimator::kVtEstimatorOk) {
        LOG(ERROR) << "Failed to read prediction (ret: " << ret << ") from " << predict_sensor_name
                   << " for sensor " << sensor_name;
        return 0;
    }

    return predict_window;
}

float ThermalHelperImpl::readPredictionAfterTimeMs(std::string_view sensor_name,
                                                   const size_t time_ms) {
    float predicted_vt = NAN;

    ATRACE_NAME(
            StringPrintf("ThermalHelper::readPredictAfterTimeMs - %s", sensor_name.data()).c_str());

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    if (sensor_info.predictor_info == nullptr) {
        LOG(ERROR) << "No predictor info found for sensor: " << sensor_name;
        return NAN;
    }

    std::string_view predict_sensor_name = sensor_info.predictor_info->sensor;
    const auto &predictor_sensor_info = sensor_info_map_.at(predict_sensor_name.data());
    ::thermal::vtestimator::VtEstimatorStatus ret =
            predictor_sensor_info.virtual_sensor_info->vt_estimator->PredictAfterTimeMs(
                    time_ms, &predicted_vt);

    if (ret == ::thermal::vtestimator::kVtEstimatorOk) {
        return predicted_vt;
    } else if (ret == ::thermal::vtestimator::kVtEstimatorUnderSampling) {
        LOG(INFO) << predict_sensor_name << " cannot provide prediction for sensor " << sensor_name
                  << "while under sampling";
    } else if (ret == ::thermal::vtestimator::kVtEstimatorUnSupported) {
        LOG(INFO) << "PredictAfterTimeMs not supported with " << predict_sensor_name
                  << " for sensor " << sensor_name;
    } else {
        LOG(ERROR) << "Failed to read prediction (ret: " << ret << ") from " << predict_sensor_name
                   << " for sensor " << sensor_name;
    }

    return NAN;
}

bool ThermalHelperImpl::readTemperaturePredictions(std::string_view sensor_name,
                                                   std::vector<float> *predictions) {
    ATRACE_NAME(StringPrintf("ThermalHelper::readTemperaturePredictions - %s", sensor_name.data())
                        .c_str());

    if (predictions == nullptr) {
        LOG(ERROR) << " predictions is nullptr";
        return false;
    }

    if (!sensor_info_map_.count(sensor_name.data())) {
        LOG(ERROR) << sensor_name << " not part of sensor_info_map_";
        return false;
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    if (sensor_info.predictor_info == nullptr) {
        LOG(ERROR) << "No predictor info found for sensor: " << sensor_name;
        return false;
    }

    std::string predict_sensor_name = sensor_info.predictor_info->sensor;
    const auto &predictor_sensor_info = sensor_info_map_.at(predict_sensor_name);
    ::thermal::vtestimator::VtEstimatorStatus ret =
            predictor_sensor_info.virtual_sensor_info->vt_estimator->GetAllPredictions(predictions);

    if (ret != ::thermal::vtestimator::kVtEstimatorOk) {
        LOG(ERROR) << "Failed to read predictions (ret: " << ret << ") from " << predict_sensor_name
                   << " for sensor " << sensor_name;
        return false;
    }

    return true;
}

constexpr int kTranTimeoutParam = 2;

bool ThermalHelperImpl::readThermalSensor(std::string_view sensor_name, float *temp,
                                          const bool force_no_cache,
                                          std::map<std::string, float> *sensor_log_map) {
    std::string file_reading;
    boot_clock::time_point now = boot_clock::now();

    ATRACE_NAME(StringPrintf("ThermalHelper::readThermalSensor - %s", sensor_name.data()).c_str());
    if (!(sensor_info_map_.count(sensor_name.data()) &&
          sensor_status_map_.count(sensor_name.data()))) {
        return false;
    }

    const auto &sensor_info = sensor_info_map_.at(sensor_name.data());
    auto &sensor_status = sensor_status_map_.at(sensor_name.data());

    {
        std::shared_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
        if (sensor_status.override_status.emul_temp != nullptr) {
            *temp = sensor_status.override_status.emul_temp->temp;
            (*sensor_log_map)[sensor_name.data()] = *temp;
            return true;
        }
    }

    const auto since_last_update = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - sensor_status.thermal_cached.timestamp);

    // Check if thermal data need to be read from cache
    if (!force_no_cache &&
        (sensor_status.thermal_cached.timestamp != boot_clock::time_point::min()) &&
        (since_last_update < sensor_info.time_resolution) &&
        !isnan(sensor_status.thermal_cached.temp)) {
        *temp = sensor_status.thermal_cached.temp;
        (*sensor_log_map)[sensor_name.data()] = *temp;
        ATRACE_INT((sensor_name.data() + std::string("-cached")).c_str(), static_cast<int>(*temp));
        return true;
    }

    // Reading thermal sensor according to it's composition
    if (sensor_info.virtual_sensor_info == nullptr) {
        if (!thermal_sensors_.readThermalFile(sensor_name.data(), &file_reading) ||
            file_reading.empty()) {
            LOG(ERROR) << "failed to read sensor: " << sensor_name;
            return false;
        }
        *temp = std::stof(::android::base::Trim(file_reading));
    } else {
        const auto &linked_sensors_size = sensor_info.virtual_sensor_info->linked_sensors.size();
        std::vector<float> sensor_readings(linked_sensors_size, NAN);

        // Calculate temperature of each of the linked sensor
        for (size_t i = 0; i < linked_sensors_size; i++) {
            if (!readDataByType(sensor_info.virtual_sensor_info->linked_sensors[i],
                                &sensor_readings[i],
                                sensor_info.virtual_sensor_info->linked_sensors_type[i],
                                force_no_cache, sensor_log_map)) {
                LOG(ERROR) << "Failed to read " << sensor_name.data() << "'s linked sensor "
                           << sensor_info.virtual_sensor_info->linked_sensors[i];
                return false;
            }
            if (std::isnan(sensor_readings[i])) {
                LOG(INFO) << sensor_name << " data is under collecting";
                return true;
            }
        }

        if ((sensor_info.virtual_sensor_info->formula == FormulaOption::USE_ML_MODEL) ||
            (sensor_info.virtual_sensor_info->formula == FormulaOption::USE_LINEAR_MODEL)) {
            std::vector<float> vt_estimator_out;
            if (!runVirtualTempEstimator(sensor_name, sensor_log_map, force_no_cache,
                                         &vt_estimator_out)) {
                LOG(ERROR) << "Failed running VirtualEstimator for " << sensor_name;
                return false;
            }
            *temp = vt_estimator_out[0];
        } else {
            float temp_val = 0.0;
            for (size_t i = 0; i < linked_sensors_size; i++) {
                float coefficient = NAN;
                if (!readDataByType(sensor_info.virtual_sensor_info->coefficients[i], &coefficient,
                                    sensor_info.virtual_sensor_info->coefficients_type[i],
                                    force_no_cache, sensor_log_map)) {
                    LOG(ERROR) << "Failed to read " << sensor_name.data() << "'s coefficient "
                               << sensor_info.virtual_sensor_info->coefficients[i];
                    return false;
                }
                if (std::isnan(coefficient)) {
                    LOG(INFO) << sensor_name << " data is under collecting";
                    return true;
                }
                switch (sensor_info.virtual_sensor_info->formula) {
                    case FormulaOption::COUNT_THRESHOLD:
                        if ((coefficient < 0 && sensor_readings[i] < -coefficient) ||
                            (coefficient >= 0 && sensor_readings[i] >= coefficient))
                            temp_val += 1;
                        break;
                    case FormulaOption::WEIGHTED_AVG:
                        temp_val += sensor_readings[i] * coefficient;
                        break;
                    case FormulaOption::MAXIMUM:
                        if (i == 0)
                            temp_val = std::numeric_limits<float>::lowest();
                        if (sensor_readings[i] * coefficient > temp_val)
                            temp_val = sensor_readings[i] * coefficient;
                        break;
                    case FormulaOption::MINIMUM:
                        if (i == 0)
                            temp_val = std::numeric_limits<float>::max();
                        if (sensor_readings[i] * coefficient < temp_val)
                            temp_val = sensor_readings[i] * coefficient;
                        break;
                    default:
                        LOG(ERROR) << "Unknown formula type for sensor " << sensor_name.data();
                        return false;
                }
            }
            *temp = (temp_val + sensor_info.virtual_sensor_info->offset);
        }
    }

    if (!isnan(sensor_info.step_ratio) && !isnan(sensor_status.thermal_cached.temp) &&
        since_last_update < sensor_info.passive_delay * kTranTimeoutParam) {
        *temp = (sensor_info.step_ratio * *temp +
                 (1 - sensor_info.step_ratio) * sensor_status.thermal_cached.temp);
    }

    (*sensor_log_map)[sensor_name.data()] = *temp;
    ATRACE_INT(sensor_name.data(), static_cast<int>(*temp));

    {
        std::unique_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
        sensor_status.thermal_cached.temp = *temp;
        sensor_status.thermal_cached.timestamp = now;
    }
    auto real_temp = (*temp) * sensor_info.multiplier;
    thermal_stats_helper_.updateSensorTempStatsByThreshold(sensor_name, real_temp);
    return true;
}

// This is called in the different thread context and will update sensor_status
// uevent_sensors_map maps sensor which trigger uevent from thermal core driver to the temperature
// read from uevent.
std::chrono::milliseconds ThermalHelperImpl::thermalWatcherCallbackFunc(
        const std::unordered_map<std::string, float> &uevent_sensor_map) {
    std::vector<Temperature> temps;
    std::vector<std::string> cooling_devices_to_update;
    boot_clock::time_point now = boot_clock::now();
    auto min_sleep_ms = std::chrono::milliseconds::max();
    bool power_data_is_updated = false;

    for (const auto &[sensor, temp] : uevent_sensor_map) {
        if (!std::isnan(temp)) {
            std::unique_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
            sensor_status_map_[sensor].thermal_cached.temp = temp;
            sensor_status_map_[sensor].thermal_cached.timestamp = now;
        }
    }

    ATRACE_CALL();
    // Go through all virtual and physical sensor and update if needed
    for (auto &name_status_pair : sensor_status_map_) {
        bool force_update = false;
        bool force_no_cache = false;
        Temperature temp;
        SensorStatus &sensor_status = name_status_pair.second;
        const SensorInfo &sensor_info = sensor_info_map_.at(name_status_pair.first);
        bool max_throttling = false;

        // Only handle the sensors in allow list
        if (!sensor_info.is_watch) {
            continue;
        }

        ATRACE_NAME(StringPrintf("ThermalHelper::thermalWatcherCallbackFunc - %s",
                                 name_status_pair.first.data())
                            .c_str());

        std::chrono::milliseconds time_elapsed_ms = std::chrono::milliseconds::zero();
        auto sleep_ms = (sensor_status.severity != ThrottlingSeverity::NONE)
                                ? sensor_info.passive_delay
                                : sensor_info.polling_delay;

        if (sensor_info.virtual_sensor_info != nullptr &&
            !sensor_info.virtual_sensor_info->trigger_sensors.empty()) {
            for (size_t i = 0; i < sensor_info.virtual_sensor_info->trigger_sensors.size(); i++) {
                const auto &trigger_sensor_status =
                        sensor_status_map_.at(sensor_info.virtual_sensor_info->trigger_sensors[i]);
                if (trigger_sensor_status.severity != ThrottlingSeverity::NONE) {
                    sleep_ms = sensor_info.passive_delay;
                    break;
                }
            }
        }
        // Force update if it's first time we update temperature value after device boot
        if (sensor_status.last_update_time == boot_clock::time_point::min()) {
            force_update = true;

        } else {
            // Handle other update event
            time_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - sensor_status.last_update_time);
            // Update triggered from genlink or uevent
            if (uevent_sensor_map.size()) {
                // Checking virtual sensor
                if (sensor_info.virtual_sensor_info != nullptr) {
                    for (size_t i = 0; i < sensor_info.virtual_sensor_info->trigger_sensors.size();
                         i++) {
                        if (uevent_sensor_map.find(
                                    sensor_info.virtual_sensor_info->trigger_sensors[i]) !=
                            uevent_sensor_map.end()) {
                            force_update = true;
                            break;
                        }
                    }
                } else if (uevent_sensor_map.find(name_status_pair.first) !=
                           uevent_sensor_map.end()) {
                    // Checking physical sensor
                    force_update = true;
                    if (std::isnan(uevent_sensor_map.at(name_status_pair.first))) {
                        // Handle the case that uevent does not contain temperature
                        force_no_cache = true;
                    }
                }
            } else if (time_elapsed_ms > sleep_ms) {
                // Update triggered from normal polling cylce
                force_update = true;
            }
        }
        {
            std::lock_guard<std::shared_mutex> _lock(sensor_status_map_mutex_);
            max_throttling = sensor_status.override_status.max_throttling;
            if (sensor_status.override_status.pending_update) {
                force_update = sensor_status.override_status.pending_update;
                sensor_status.override_status.pending_update = false;
            }
        }
        LOG(VERBOSE) << "sensor " << name_status_pair.first
                     << ": time_elapsed=" << time_elapsed_ms.count()
                     << ", sleep_ms=" << sleep_ms.count() << ", force_update = " << force_update
                     << ", force_no_cache = " << force_no_cache;

        if (!force_update) {
            auto timeout_remaining = sleep_ms - time_elapsed_ms;
            if (min_sleep_ms > timeout_remaining) {
                min_sleep_ms = timeout_remaining;
            }
            LOG(VERBOSE) << "sensor " << name_status_pair.first
                         << ": timeout_remaining=" << timeout_remaining.count();
            continue;
        }

        std::pair<ThrottlingSeverity, ThrottlingSeverity> throttling_status;
        if (!readTemperature(name_status_pair.first, &temp, force_no_cache)) {
            LOG(ERROR) << __func__
                       << ": error reading temperature for sensor: " << name_status_pair.first;
            continue;
        }

        {
            std::unique_lock<std::shared_mutex> _lock(sensor_status_map_mutex_);
            if (sensor_status.pending_notification) {
                temps.push_back(temp);
                sleep_ms = (sensor_status.severity != ThrottlingSeverity::NONE)
                                   ? sensor_info.passive_delay
                                   : sensor_info.polling_delay;
                sensor_status.pending_notification = false;
            }
        }

        if (!power_data_is_updated) {
            power_files_.refreshPowerStatus();
            power_data_is_updated = true;
        }

        if (sensor_status.severity == ThrottlingSeverity::NONE) {
            thermal_throttling_.clearThrottlingData(name_status_pair.first);
        } else {
            // prepare for predictions for throttling compensation
            std::vector<float> sensor_predictions;
            if (sensor_info.predictor_info != nullptr &&
                sensor_info.predictor_info->support_pid_compensation) {
                if (!readTemperaturePredictions(name_status_pair.first, &sensor_predictions)) {
                    LOG(ERROR) << "Failed to read predictions of " << name_status_pair.first
                               << " for throttling compensation";
                }
            }

            // update thermal throttling request
            thermal_throttling_.thermalThrottlingUpdate(
                    temp, sensor_info, sensor_status.severity, time_elapsed_ms,
                    power_files_.GetPowerStatusMap(), cooling_device_info_map_, max_throttling,
                    sensor_predictions);
        }

        thermal_throttling_.computeCoolingDevicesRequest(
                name_status_pair.first, sensor_info, sensor_status.severity,
                &cooling_devices_to_update, &thermal_stats_helper_);
        if (min_sleep_ms > sleep_ms) {
            min_sleep_ms = sleep_ms;
        }

        LOG(VERBOSE) << "Sensor " << name_status_pair.first << ": sleep_ms=" << sleep_ms.count()
                     << ", min_sleep_ms voting result=" << min_sleep_ms.count();
        sensor_status.last_update_time = now;
    }

    if (!temps.empty()) {
        for (const auto &t : temps) {
            if (sensor_info_map_.at(t.name).send_cb && cb_) {
                cb_(t);
            }

            if (sensor_info_map_.at(t.name).send_powerhint) {
                power_hal_service_.sendPowerExtHint(t);
            }
        }
    }

    if (!cooling_devices_to_update.empty()) {
        updateCoolingDevices(cooling_devices_to_update);
    }

    int count_failed_reporting = thermal_stats_helper_.reportStats();
    if (count_failed_reporting != 0) {
        LOG(ERROR) << "Failed to report " << count_failed_reporting << " thermal stats";
    }

    const auto since_last_power_log_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - power_files_.GetPrevPowerLogTime());
    if (since_last_power_log_ms >= kPowerLogIntervalMs) {
        power_files_.logPowerStatus(now);
    }

    return min_sleep_ms;
}

}  // namespace implementation
}  // namespace thermal
}  // namespace hardware
}  // namespace android
}  // namespace aidl
