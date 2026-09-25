#include <utils.hpp>

#ifdef DEBUG_SCREEN

#include <imgui.h>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace {
    std::mutex debug_stream_mutex;
    std::map<std::string, std::string> debug_stream_entries;
}

extern "C" void add_debug_stream(const char* entry, size_t entrylen, const char* content, size_t contentlen) {
    std::lock_guard<std::mutex> lock(debug_stream_mutex);
    std::string key(entry, entrylen);
    if (contentlen == 0) {
        debug_stream_entries.erase(key);
        return;
    }
    debug_stream_entries[key] = std::string(content, contentlen);
}

namespace {
    struct ColourTag {
        std::string_view tag;
        ImVec4 colour;
    };
    constexpr ImVec4 kDefaultColour{1.0f, 1.0f, 1.0f, 1.0f};
    constexpr ColourTag kColourTags[] = {
        {"\\red", {1.0f, 0.35f, 0.35f, 1.0f}},
        {"\\green", {0.35f, 1.0f, 0.35f, 1.0f}},
        {"\\blue", {0.4f, 0.6f, 1.0f, 1.0f}},
        {"\\yellow", {1.0f, 0.9f, 0.3f, 1.0f}},
        {"\\normal", kDefaultColour},
    };

    // Renders one logical (already newline-split) line, honouring inline \colour tags.
    // Splits into coloured runs and lays them out with SameLine() so they stay on one row.
    void render_debug_line(std::string_view line) {
        ImVec4 colour = kDefaultColour;
        bool first_run = true;
        size_t pos = 0;

        while (pos <= line.size()) {
            size_t next_tag_pos = std::string_view::npos;
            const ColourTag* next_tag = nullptr;
            for (const auto& tag : kColourTags) {
                size_t found = line.find(tag.tag, pos);
                if (found != std::string_view::npos && (next_tag_pos == std::string_view::npos || found < next_tag_pos)) {
                    next_tag_pos = found;
                    next_tag = &tag;
                }
            }

            std::string_view run = next_tag_pos == std::string_view::npos ? line.substr(pos) : line.substr(pos, next_tag_pos - pos);
            if (!run.empty()) {
                if (!first_run) ImGui::SameLine(0.0f, 0.0f);
                first_run = false;
                ImGui::TextColored(colour, "%.*s", (int)run.size(), run.data());
            }

            if (next_tag == nullptr) break;
            colour = next_tag->colour;
            pos = next_tag_pos + next_tag->tag.size();
        }

        if (first_run) ImGui::NewLine();  // blank line still needs to take up vertical space
    }
}  // namespace

void render_debug_screen() {
    std::map<std::string, std::string> snapshot;
    {
        std::lock_guard<std::mutex> lock(debug_stream_mutex);
        snapshot = debug_stream_entries;  // copy so ImGui calls aren't made under the lock
    }
    if (snapshot.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    constexpr float pad = 8.0f;
    constexpr float column_width = 340.0f;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##debug_screen", nullptr, flags);
    ImGui::SetWindowFontScale(2.f);

    float column_x = pad;
    float cursor_y = pad;

    for (const auto& [entry, content] : snapshot) {
        (void)entry;  // entry name is intentionally never rendered — only content

        size_t line_count = 1;
        for (char c : content)
            if (c == '\n') ++line_count;
        float entry_height = line_count * ImGui::GetTextLineHeightWithSpacing();

        // if this entry won't fit under the current cursor, start a fresh column to the right
        if (cursor_y > pad && cursor_y + entry_height > io.DisplaySize.y - pad) {
            column_x += column_width + pad;
            cursor_y = pad;
        }

        size_t line_start = 0;
        std::string_view content_view = content;
        while (true) {
            size_t newline_pos = content_view.find('\n', line_start);
            std::string_view line =
                newline_pos == std::string_view::npos ? content_view.substr(line_start) : content_view.substr(line_start, newline_pos - line_start);
            ImGui::SetCursorPos(ImVec2(column_x, cursor_y));
            render_debug_line(line);
            cursor_y = ImGui::GetCursorPosY();
            if (newline_pos == std::string_view::npos) break;
            line_start = newline_pos + 1;
        }
    }

    ImGui::End();
}

#endif  // DEBUG_SCREEN
