/**
 * @file isnapshot_writer.cpp
 * @brief Snapshot writer global accessor wiring
 */

#include "F_platform/tasks/boundary/isnapshot_writer.hpp"

namespace Scheduler {

static ISnapshotWriter* s_snapshotWriter = nullptr;

void setSnapshotWriter(ISnapshotWriter* writer) {
    s_snapshotWriter = writer;
}

ISnapshotWriter& snapshotWriter() {
    return *s_snapshotWriter;
}

} // namespace Scheduler
