#pragma once

#include "common/ipc_protocol.hpp"

namespace vocaltri::cli {

class Client {
public:
    static int run(ipc::Command command);
};

} // namespace vocaltri::cli
