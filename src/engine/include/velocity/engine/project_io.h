#pragma once
// Project persistence — the canonical JSON serialization defined by docs/03
// §1 as the interchange form. NOTE: docs/03 specifies a SQLite container
// (.vep) as the primary format for autosave/journal/lazy-load; that lands
// with the durability workstream. This JSON form is forward-compatible with
// it (same entities, same field names) and is what File→Save/Open use today.

#include <velocity/engine/model.h>
#include <velocity/foundation/expected.h>

#include <filesystem>
#include <string>

namespace velocity::engine {

inline constexpr int kProjectSchemaVersion = 1;

[[nodiscard]] std::string serializeProject(const Sequence& seq);
Expected<SnapshotPtr, std::string> deserializeProject(const std::string& json);

Expected<void, std::string> saveProject(const SnapshotPtr& seq,
                                        const std::filesystem::path& file);
Expected<SnapshotPtr, std::string> loadProject(const std::filesystem::path& file);

} // namespace velocity::engine
