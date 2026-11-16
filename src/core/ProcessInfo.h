// Runtime information used by the status bar and diagnostics dialog.
#pragma once

#include "core/Result.h"

#include <QString>

namespace pkg::sysinfo {

/// Resident set size of this process, or an error when unavailable.
[[nodiscard]] Result<quint64> currentResidentBytes();

[[nodiscard]] QString operatingSystem();
[[nodiscard]] QString cpuDescription();
[[nodiscard]] int logicalCpuCount();
[[nodiscard]] quint64 totalRamBytes();

}  // namespace pkg::sysinfo
