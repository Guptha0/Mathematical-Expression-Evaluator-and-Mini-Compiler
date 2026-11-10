#include "Theme.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace mathcompiler::gui {

namespace {

ImFont* tryLoadFirstAvailableFont(ImGuiIO& io,
                                  const std::array<const char*, 4>& candidates,
                                  float pixelSize) {
    for (const char* path : candidates) {
        if (!path) continue;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            ImFontConfig cfg;
            cfg.OversampleH = 2;
            cfg.OversampleV = 2;
            cfg.PixelSnapH = true;
            if (ImFont* loaded = io.Fonts->AddFontFromFileTTF(path, pixelSize, &cfg)) {
                return loaded;
            }
        }
    }
    return nullptr;
}

}  // namespace

void applyDarkTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};

    style.WindowPadding     = ImVec2(12.0f, 12.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.CellPadding       = ImVec2(8.0f, 6.0f);
    style.ItemSpacing       = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 11.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    if (dpiScale > 1.0f) {
        style.ScaleAllSizes(dpiScale);
    }

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = Palette::TextPrimary;
    colors[ImGuiCol_TextDisabled]          = Palette::TextMuted;
    colors[ImGuiCol_WindowBg]              = Palette::BgBase;
    colors[ImGuiCol_ChildBg]               = Palette::BgPanel;
    colors[ImGuiCol_PopupBg]               = Palette::BgElevated;
    colors[ImGuiCol_Border]                = Palette::BorderSubtle;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = Palette::BgInput;
    colors[ImGuiCol_FrameBgHovered]        = Palette::BgElevated;
    colors[ImGuiCol_FrameBgActive]         = Palette::BgHover;
    colors[ImGuiCol_TitleBg]               = Palette::BgBase;
    colors[ImGuiCol_TitleBgActive]         = Palette::BgPanel;
    colors[ImGuiCol_TitleBgCollapsed]      = Palette::BgBase;
    colors[ImGuiCol_MenuBarBg]             = Palette::BgPanel;
    colors[ImGuiCol_ScrollbarBg]           = Palette::BgBase;
    colors[ImGuiCol_ScrollbarGrab]         = Palette::BorderStrong;
    colors[ImGuiCol_ScrollbarGrabHovered]  = Palette::TextMuted;
    colors[ImGuiCol_ScrollbarGrabActive]   = Palette::AccentCyan;
    colors[ImGuiCol_CheckMark]             = Palette::AccentCyan;
    colors[ImGuiCol_SliderGrab]            = Palette::AccentCyan;
    colors[ImGuiCol_SliderGrabActive]      = Palette::AccentIndigo;

    colors[ImGuiCol_Button]                = Palette::BgElevated;
    colors[ImGuiCol_ButtonHovered]         = Palette::BgHover;
    colors[ImGuiCol_ButtonActive]          = Palette::BgSelected;

    colors[ImGuiCol_Header]                = Palette::BgElevated;
    colors[ImGuiCol_HeaderHovered]         = Palette::BgHover;
    colors[ImGuiCol_HeaderActive]          = Palette::BgSelected;

    colors[ImGuiCol_Separator]             = Palette::BorderSubtle;
    colors[ImGuiCol_SeparatorHovered]      = Palette::AccentCyan;
    colors[ImGuiCol_SeparatorActive]       = Palette::AccentIndigo;

    colors[ImGuiCol_ResizeGrip]            = Palette::BorderSubtle;
    colors[ImGuiCol_ResizeGripHovered]     = Palette::AccentCyan;
    colors[ImGuiCol_ResizeGripActive]      = Palette::AccentIndigo;

    colors[ImGuiCol_Tab]                   = Palette::BgBase;
    colors[ImGuiCol_TabHovered]            = Palette::BgHover;
    colors[ImGuiCol_TabSelected]           = Palette::BgElevated;
    colors[ImGuiCol_TabSelectedOverline]   = Palette::AccentCyan;
    colors[ImGuiCol_TabDimmed]             = Palette::BgBase;
    colors[ImGuiCol_TabDimmedSelected]     = Palette::BgPanel;

    colors[ImGuiCol_TableHeaderBg]         = Palette::BgElevated;
    colors[ImGuiCol_TableBorderStrong]     = Palette::BorderSubtle;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.141f, 0.180f, 0.251f, 0.55f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.220f, 0.741f, 0.973f, 0.28f);
    colors[ImGuiCol_NavCursor]             = Palette::AccentCyan;
}

FontBundle loadStudioFonts(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    const float scale = dpiScale > 0.5f ? dpiScale : 1.0f;

    FontBundle bundle;

    const std::array<const char*, 4> uiCandidates = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        nullptr};
    const std::array<const char*, 4> uiBoldCandidates = {
        "C:/Windows/Fonts/segoeuib.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        nullptr};
    const std::array<const char*, 4> monoCandidates = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/CascadiaMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        nullptr};

    bundle.uiRegular = tryLoadFirstAvailableFont(io, uiCandidates, 16.0f * scale);
    if (!bundle.uiRegular) {
        ImFontConfig fallbackCfg;
        fallbackCfg.SizePixels = 15.0f * scale;
        bundle.uiRegular = io.Fonts->AddFontDefault(&fallbackCfg);
    }

    bundle.uiBold = tryLoadFirstAvailableFont(io, uiBoldCandidates, 16.0f * scale);
    if (!bundle.uiBold) {
        bundle.uiBold = bundle.uiRegular;
    }

    bundle.monoRegular = tryLoadFirstAvailableFont(io, monoCandidates, 15.5f * scale);
    if (!bundle.monoRegular) {
        bundle.monoRegular = bundle.uiRegular;
    }

    bundle.monoLarge = tryLoadFirstAvailableFont(io, monoCandidates, 24.0f * scale);
    if (!bundle.monoLarge) {
        bundle.monoLarge = bundle.monoRegular;
    }

    return bundle;
}

}  // namespace mathcompiler::gui
