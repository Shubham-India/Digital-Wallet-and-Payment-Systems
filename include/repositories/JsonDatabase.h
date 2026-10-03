#pragma once
#include <string>

#include "repositories/InMemoryRepositories.h"

namespace wallet {

// JSON-file persistence. The in-memory repositories are the working set; load() fills them
// from <dir>/*.json and commit() rewrites only the files whose repository changed
// (write to a temp file, then rename, so a crash never leaves a half-written file).
// Domain classes know nothing about JSON; all mapping lives in JsonDatabase.cpp.
class JsonDatabase : public InMemoryDatabase {
public:
    explicit JsonDatabase(std::string directory) : dir_(std::move(directory)) {}
    Result<void> load();  // missing files => empty; corrupt file => PersistenceFailure (file left untouched)
    Result<void> commit() override;
    const std::string& directory() const noexcept { return dir_; }
private:
    std::string path(const char* name) const { return dir_ + "/" + name + ".json"; }
    std::string dir_;
};

} // namespace wallet
