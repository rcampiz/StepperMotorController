/**
 * @file isnapshot_writer.cpp
 * @brief Snapshot writer global accessor wiring
 */

#include "harness/pins/isnapshot_writer.hpp"

namespace Harness {

static ISnapshotWriter* s_snapshotWriter = nullptr;

void setSnapshotWriter(ISnapshotWriter* writer) {
    s_snapshotWriter = writer;
}

ISnapshotWriter& snapshotWriter() {
    return *s_snapshotWriter;
}

} // namespace Harness
