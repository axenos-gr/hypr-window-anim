#pragma once

namespace hwa::plugin::watcher {
    // Debounced reload on config/effects changes
    void start();
    void stop();
}
