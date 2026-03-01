#include "ui/application.h"
#include "ui/panel_helpers.h"
#include "ui/icons.h"
#include <imgui.h>
#include "utils/logger.h"

namespace orpheus::ui {

void Application::RenderConsole() {
    ImGui::Begin("Console", &panels_.console);

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##console_filter", "Filter...", console_filter_, sizeof(console_filter_));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &console_auto_scroll_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_OR_TEXT(icons_loaded_, ICON_FA_XMARK " Clear", "Clear"))) {
        Logger::Instance().ClearBuffer();
    }

    ImGui::Separator();

    ImGui::BeginChild("##ConsoleScroll", ImVec2(0, 0), true);

    auto entries = Logger::Instance().GetRecentEntries(500);

    for (const auto& entry : entries) {
        if (console_filter_[0] != '\0') {
            if (entry.message.find(console_filter_) == std::string::npos) continue;
        }

        ImVec4 color;
        const char* prefix;
        switch (entry.level) {
            case spdlog::level::trace:    color = colors::Muted;   prefix = "TRC"; break;
            case spdlog::level::debug:    color = colors::Muted;   prefix = "DBG"; break;
            case spdlog::level::info:     color = colors::Success; prefix = "INF"; break;
            case spdlog::level::warn:     color = colors::Warning; prefix = "WRN"; break;
            case spdlog::level::err:      color = colors::Error;   prefix = "ERR"; break;
            case spdlog::level::critical: color = colors::Error;   prefix = "CRT"; break;
            default: color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); prefix = "???"; break;
        }

        ImGui::TextColored(colors::Muted, "[%s]", entry.timestamp.c_str());
        ImGui::SameLine();
        ImGui::TextColored(color, "[%s]", prefix);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", entry.message.c_str());
    }

    if (console_auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace orpheus::ui
