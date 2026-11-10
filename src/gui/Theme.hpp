#pragma once

#include "imgui.h"

namespace mathcompiler::gui {

struct FontBundle {
    ImFont* uiRegular = nullptr;
    ImFont* uiBold = nullptr;
    ImFont* monoRegular = nullptr;
    ImFont* monoLarge = nullptr;
};

struct Palette {
    static constexpr ImVec4 BgBase      = ImVec4(0.043f, 0.059f, 0.090f, 1.00f);
    static constexpr ImVec4 BgPanel     = ImVec4(0.071f, 0.094f, 0.141f, 1.00f);
    static constexpr ImVec4 BgElevated  = ImVec4(0.098f, 0.129f, 0.192f, 1.00f);
    static constexpr ImVec4 BgInput     = ImVec4(0.035f, 0.051f, 0.078f, 1.00f);
    static constexpr ImVec4 BgHover     = ImVec4(0.133f, 0.176f, 0.259f, 1.00f);
    static constexpr ImVec4 BgSelected  = ImVec4(0.118f, 0.227f, 0.373f, 1.00f);

    static constexpr ImVec4 BorderSubtle = ImVec4(0.141f, 0.180f, 0.251f, 1.00f);
    static constexpr ImVec4 BorderStrong = ImVec4(0.216f, 0.275f, 0.376f, 1.00f);

    static constexpr ImVec4 AccentCyan    = ImVec4(0.220f, 0.741f, 0.973f, 1.00f);
    static constexpr ImVec4 AccentIndigo  = ImVec4(0.506f, 0.549f, 0.973f, 1.00f);
    static constexpr ImVec4 AccentEmerald = ImVec4(0.204f, 0.827f, 0.600f, 1.00f);
    static constexpr ImVec4 AccentAmber   = ImVec4(0.984f, 0.749f, 0.141f, 1.00f);
    static constexpr ImVec4 AccentRose    = ImVec4(0.973f, 0.443f, 0.443f, 1.00f);
    static constexpr ImVec4 AccentPink    = ImVec4(0.957f, 0.447f, 0.714f, 1.00f);
    static constexpr ImVec4 AccentPurple  = ImVec4(0.655f, 0.545f, 0.980f, 1.00f);

    static constexpr ImVec4 TextPrimary   = ImVec4(0.945f, 0.961f, 0.976f, 1.00f);
    static constexpr ImVec4 TextSecondary = ImVec4(0.580f, 0.639f, 0.722f, 1.00f);
    static constexpr ImVec4 TextMuted     = ImVec4(0.392f, 0.455f, 0.545f, 1.00f);

    static constexpr ImVec4 ErrorBg       = ImVec4(0.231f, 0.082f, 0.106f, 1.00f);
    static constexpr ImVec4 SuccessBg     = ImVec4(0.059f, 0.184f, 0.153f, 1.00f);
    static constexpr ImVec4 InfoBg        = ImVec4(0.071f, 0.153f, 0.239f, 1.00f);
};

void applyDarkTheme(float dpiScale);
FontBundle loadStudioFonts(float dpiScale);

inline void pushFontSafe(ImFont* font) {
    if (!font) return;
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19200
    ImGui::PushFont(font, 0.0f);
#else
    ImGui::PushFont(font);
#endif
}

inline void popFontSafe(ImFont* font) {
    if (!font) return;
    ImGui::PopFont();
}

}  // namespace mathcompiler::gui
