#include "common/deepgram_key.hpp"

#include "common/app_settings.hpp"

namespace vocaltri {

QString deepgramApiKey() {
    return loadAppSettings().apiKey;
}

} // namespace vocaltri
