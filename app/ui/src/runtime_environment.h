#pragma once
#include <QString>

// Prepares paths for direct launches of a worktree-bound client; starts no host.
bool prepareRuntimeEnvironment(QString* error);
