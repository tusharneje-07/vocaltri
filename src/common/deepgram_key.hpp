#pragma once

#include <QString>

namespace vocaltri {

// Environment variables take precedence over the local file. The file is only
// accepted when it is readable by the owner and by nobody else.
QString deepgramApiKey();

} // namespace vocaltri
