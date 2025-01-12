// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <string>

#include "common/thread-debug-info.h"
#include "testutil/gtest-util.h"
#include "util/container-util.h"
#include "util/thread.h"

#include "common/names.h"

namespace impala {

TEST(ThreadDebugInfo, Ids) {
  // This test checks if SetInstanceId() stores the
  // string representation of a TUniqueId correctly.
  ThreadDebugInfo thread_debug_info;
  TUniqueId instance_id;
  instance_id.hi = 123;
  instance_id.lo = 456;
  thread_debug_info.SetInstanceId(instance_id);
  string instance_id_str = PrintId(instance_id);

  EXPECT_EQ(instance_id, thread_debug_info.GetInstanceId());
  EXPECT_EQ(instance_id_str, PrintId(thread_debug_info.GetInstanceId()));

  TUniqueId query_id;
  query_id.hi = 1234;
  query_id.lo = 4567;
  thread_debug_info.SetQueryId(query_id);
  string query_id_str = PrintId(query_id);

  EXPECT_EQ(query_id, thread_debug_info.GetQueryId());
  EXPECT_EQ(query_id_str, PrintId(thread_debug_info.GetQueryId()));
}

TEST(ThreadDebugInfo, ThreadName) {
  // Checks if we can store the thread name.
  // If thread name is too long, the prefix of the thread name is stored.
  ThreadDebugInfo thread_debug_info;
  string thread_name = "thread-1";
  thread_debug_info.SetThreadName(thread_name);

  EXPECT_EQ(thread_name, thread_debug_info.GetThreadName());

  string a_255(255, 'a');
  string b_255(255, 'b');
  string a_b = a_255 + b_255;
  thread_debug_info.SetThreadName(a_b);
  string stored_name = thread_debug_info.GetThreadName();

  // Let's check if we stored the corrects parts of thread name
  string expected = a_255.substr(0, 244) + "..." + b_255.substr(0, 8);
  EXPECT_EQ(expected, stored_name);
}

TEST(ThreadDebugInfo, Global) {
  // Checks if the constructor of the local ThreadDebugInfo object set the
  // global pointer to itself.
  ThreadDebugInfo thread_debug_info;
  ThreadDebugInfo* global_thread_debug_info = GetThreadDebugInfo();

  EXPECT_EQ(&thread_debug_info, global_thread_debug_info);
}

TEST(ThreadDebugInfo, ThreadCreateRelationships) {
  // Checks if child thread extracts debug info from parent automatically.
  // Child's thread name is given in Thread::Create
  // Child's instance_id_ should be the same as parent's instance_id_
  // Child should store a copy of its parent's thread name.
  // Child should store its parent's system thread id.
  string parent_name = "Parent";
  string child_name = "Child";

  ThreadDebugInfo parent_tdi;
  parent_tdi.SetThreadName(parent_name);
  TUniqueId instance_id;
  instance_id.hi = 123;
  instance_id.lo = 456;
  parent_tdi.SetInstanceId(instance_id);

  TUniqueId query_id;
  query_id.hi = 123;
  query_id.lo = 456;
  parent_tdi.SetQueryId(query_id);

  std::unique_ptr<Thread> child_thread;
  auto f = [instance_id, query_id, child_name, parent_name, &parent_tdi]() {
    // In child's thread the global ThreadDebugInfo object points to the child's own
    // ThreadDebugInfo object which was automatically created in Thread::SuperviseThread
    ThreadDebugInfo* child_tdi = GetThreadDebugInfo();
    EXPECT_EQ(child_name, child_tdi->GetThreadName());
    EXPECT_EQ(instance_id, child_tdi->GetInstanceId());
    EXPECT_EQ(query_id, child_tdi->GetQueryId());
    EXPECT_EQ(parent_name, child_tdi->GetParentThreadName());
    EXPECT_EQ(parent_tdi.GetSystemThreadId(), child_tdi->GetParentSystemThreadId());
  };
  ASSERT_OK(Thread::Create("Test", child_name, f, &child_thread));
  child_thread->Join();
}

TEST(ThreadDebugInfo, Scoping) {
  TUniqueId id;
  id.hi = 123;
  id.lo = 456;
  TUniqueId id2;
  id.hi = 234;
  id.lo = 345;

  ThreadDebugInfo tdi;
  ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetQueryId());
  ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetInstanceId());
  {
    ScopedThreadContext s(&tdi, id);
    ASSERT_EQ(id, tdi.GetQueryId());
    ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetInstanceId());
  }
  ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetQueryId());
  ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetInstanceId());
  {
    ScopedThreadContext s(&tdi, id, id2);
    ASSERT_EQ(id, tdi.GetQueryId());
    ASSERT_EQ(id2, tdi.GetInstanceId());
  }
  ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetQueryId());
  ASSERT_EQ(ThreadDebugInfo::ZERO_THREAD_ID, tdi.GetInstanceId());
}

}

IMPALA_TEST_MAIN();
