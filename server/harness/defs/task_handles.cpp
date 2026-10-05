/**
 * @file task_handles.cpp
 * @brief Storage for the H_TASKS_L3 downward signal (opaque task handles)
 *
 * Kept separate from harness_connect.cpp so host tests can link it without the
 * firmware init contracts.
 */
#include "harness/defs/harness_tasks_l3.hpp"

namespace Harness {

static TaskHandles s_handles;

void setTaskHandles(const TaskHandles& handles) { s_handles = handles; }
const TaskHandles& taskHandles() { return s_handles; }

} // namespace Harness
