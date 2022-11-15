#include <synchronization/IFullSyncStaleDataExpunger.h>

namespace quentier::synchronization {

bool operator==(
    const IFullSyncStaleDataExpunger::PreservedGuids & lhs,
    const IFullSyncStaleDataExpunger::PreservedGuids & rhs) noexcept
{
    return lhs.notebookGuids == rhs.notebookGuids &&
        lhs.noteGuids == rhs.noteGuids &&
        lhs.savedSearchGuids == rhs.savedSearchGuids &&
        lhs.tagGuids == rhs.tagGuids;
}

bool operator!=(
    const IFullSyncStaleDataExpunger::PreservedGuids & lhs,
    const IFullSyncStaleDataExpunger::PreservedGuids & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
