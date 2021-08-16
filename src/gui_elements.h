#pragma once

#include <imgui.h>
#include <format>


namespace gui {


    template <typename ...Args>
    void FormattedText(std::string_view tmpl, const Args... args) {

        auto text = std::format(tmpl, args...);

        ImGui::TextUnformatted(text.c_str());
    }

} // ns gui