/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "gtest/gtest.h"
#include "perfmgr/JobQueueManager.h"
#include "perfmgr/NodeLooperThread.h"

namespace android {
namespace perfmgr {

const size_t OVER_POOL_SIZE = DEFAULT_POOL_SIZE + 4;

// Helper function to create a Job for testing
Job *createJob(const std::string &hint_type, int schedule_time, bool is_cancel = false) {
    Job *job = new Job();
    job->hint_type = hint_type;
    job->schedule_time =
            std::chrono::time_point<std::chrono::steady_clock>(std::chrono::seconds(schedule_time));
    job->is_cancel = is_cancel;
    return job;
}

// Test fixture class
class JobQueueManagerTest : public ::testing::Test {
  protected:
    JobQueueManager jobMgr_;
    void TearDown() override {
        // Clean up any jobs that might be left in the queue
        Job *job;
        while ((job = jobMgr_.dequeueRequest()) != nullptr) {
            delete job;
        }
    }
};

TEST_F(JobQueueManagerTest, TestEnqueueAndDequeue) {
    Job *job1 = createJob("type1", 2);
    Job *job2 = createJob("type2", 1);

    jobMgr_.enqueueRequest(job1);
    jobMgr_.enqueueRequest(job2);

    Job *dequeuedJob1 = jobMgr_.dequeueRequest();
    Job *dequeuedJob2 = jobMgr_.dequeueRequest();

    ASSERT_NE(dequeuedJob1, nullptr);
    ASSERT_NE(dequeuedJob2, nullptr);

    // Verify that the jobs are dequeued in the correct order (based on schedule time)
    ASSERT_EQ(dequeuedJob1->hint_type, "type2");
    ASSERT_EQ(dequeuedJob2->hint_type, "type1");

    delete dequeuedJob1;
    delete dequeuedJob2;
}

TEST_F(JobQueueManagerTest, TestEmptyQueue) {
    Job *dequeuedJob = jobMgr_.dequeueRequest();
    ASSERT_EQ(dequeuedJob, nullptr);
}

TEST_F(JobQueueManagerTest, TestPoolAllocation) {
    // Enqueue more jobs than the default pool size to force pool expansion.
    for (int i = 0; i < OVER_POOL_SIZE; ++i) {
        Job *job = createJob("test", i);
        jobMgr_.enqueueRequest(job);
    }

    // Dequeue all of them to ensure the pool is reused
    for (int i = 0; i < OVER_POOL_SIZE; ++i) {
        Job *job = jobMgr_.dequeueRequest();
        ASSERT_NE(job, nullptr);
        delete job;
    }

    // Check if the queue is empty
    Job *dequeuedJob = jobMgr_.dequeueRequest();
    ASSERT_EQ(dequeuedJob, nullptr);
}

TEST_F(JobQueueManagerTest, TestJobReset) {
    Job *job = createJob("test", 1);
    jobMgr_.enqueueRequest(job);
    Job *dequeuedJob = jobMgr_.dequeueRequest();
    ASSERT_NE(dequeuedJob, nullptr);
    ASSERT_EQ(dequeuedJob->hint_type, "test");
    jobMgr_.returnJob(dequeuedJob);  // Return the job to the pool

    // Now, enqueue another job
    Job *job2 = createJob("new_test", 2);
    jobMgr_.enqueueRequest(job2);
    Job *dequeuedJob2 = jobMgr_.dequeueRequest();
    ASSERT_NE(dequeuedJob2, nullptr);
    ASSERT_EQ(dequeuedJob2->hint_type, "new_test");
    jobMgr_.returnJob(dequeuedJob2);
}

TEST_F(JobQueueManagerTest, TestGetFreeJobAndReturnJob) {
    // Get a free job
    Job *job = jobMgr_.getFreeJob();
    ASSERT_NE(job, nullptr);

    // Set some data in the job
    job->hint_type = "test_type";
    job->schedule_time = std::chrono::steady_clock::now();
    job->is_cancel = true;

    // Return the job
    jobMgr_.returnJob(job);

    // Test the pool size, we allocate more than the pool size to verify it works.
    // Also, verify all jobs are reset.
    for (int i = 0; i < OVER_POOL_SIZE; i++) {
        Job *job3 = jobMgr_.getFreeJob();
        ASSERT_NE(job3, nullptr);
        ASSERT_EQ(job3->hint_type, "");
        ASSERT_EQ(job3->is_cancel, false);
        jobMgr_.returnJob(job3);
    }
}

}  // namespace perfmgr
}  // namespace android
