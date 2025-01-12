/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.apache.impala.testutil;

import org.apache.impala.hooks.QueryCompleteContext;
import org.apache.impala.hooks.QueryEventHook;

public class AlwaysErrorQueryEventHook implements QueryEventHook {
  @Override
  public void onImpalaStartup() {
    throw new RuntimeException("intentional error");
  }

  @Override
  public void onQueryComplete(QueryCompleteContext context) {
    throw new RuntimeException("intentional error");
  }
}
