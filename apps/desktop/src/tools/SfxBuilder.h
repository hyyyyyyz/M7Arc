#pragma once

#include <QString>
#include <functional>

#include "DistributionBuilder.h"

namespace m7arc::tools {

// Returns an empty string on success. Run from an operation worker for large archives.
QString createConsoleSfx(const QString &stub, const QString &archive, const QString &output,
                         const std::function<bool()> &cancelled = {});

}
