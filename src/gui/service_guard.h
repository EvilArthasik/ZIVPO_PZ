#pragma once

namespace trayapp::gui {

enum class StartupDecision {
    Continue,
    Exit
};

[[nodiscard]] StartupDecision CheckServiceStartup();
[[nodiscard]] bool IsParentServiceProcess();

}
