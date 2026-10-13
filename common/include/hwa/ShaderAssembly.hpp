#pragma once

#include <string>
#include <string_view>

namespace hwa {
    // Body is source string 1: errors read "1:<line>"
    std::string assembleFragment(std::string_view effectBody);
}
