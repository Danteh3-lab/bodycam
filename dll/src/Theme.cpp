#include "Theme.hpp"

#include <filesystem>

namespace nova_host::theme {
namespace {

FontSet g_fonts;

ImFont* TryAddFont(const std::filesystem::path& path, float size) {
	std::error_code code;
	if (!std::filesystem::exists(path, code)) return nullptr;
	const std::string utf8Path = path.string();
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(utf8Path.c_str(), size);
	return font;
}

} // namespace

ImVec4 ToVec4(ImU32 color) {
	return ImGui::ColorConvertU32ToFloat4(color);
}

void Apply() {
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowPadding = ImVec2(12, 12);
	style.FramePadding = ImVec2(9, 5);
	style.ItemSpacing = ImVec2(10, 7);
	style.ItemInnerSpacing = ImVec2(6, 4);
	style.FrameRounding = kFrameRounding;
	style.GrabRounding = kFrameRounding;
	style.PopupRounding = kPopupRounding;
	style.ScrollbarSize = 11.0f;
	style.ScrollbarRounding = 12.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.WindowRounding = kWindowRounding;
	style.ChildRounding = kChildRounding;
	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.GrabMinSize = 9.0f;

	ImVec4* c = style.Colors;
	c[ImGuiCol_Text] = ToVec4(kText);
	c[ImGuiCol_TextDisabled] = ToVec4(kTextDim);
	c[ImGuiCol_WindowBg] = ToVec4(kSurface0);
	c[ImGuiCol_ChildBg] = ToVec4(kSurface1);
	c[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.083f, 0.098f, 0.97f);
	c[ImGuiCol_Border] = ToVec4(kBorder);
	c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	c[ImGuiCol_FrameBg] = ToVec4(kSurface2);
	c[ImGuiCol_FrameBgHovered] = ToVec4(kSurface3);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.19f, 0.22f, 1.0f);
	c[ImGuiCol_TitleBg] = ToVec4(kSurface0);
	c[ImGuiCol_TitleBgActive] = ToVec4(kSurface0);
	c[ImGuiCol_TitleBgCollapsed] = ToVec4(kSurface0);
	c[ImGuiCol_MenuBarBg] = ToVec4(kSurface1);
	c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.055f, 0.065f, 0.6f);
	c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.22f, 0.25f, 1.0f);
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.27f, 0.30f, 0.34f, 1.0f);
	c[ImGuiCol_ScrollbarGrabActive] = ToVec4(kAccentDim);
	c[ImGuiCol_CheckMark] = ToVec4(kAccent);
	c[ImGuiCol_SliderGrab] = ToVec4(kAccentDim);
	c[ImGuiCol_SliderGrabActive] = ToVec4(kAccent);
	c[ImGuiCol_Button] = ToVec4(kSurface2);
	c[ImGuiCol_ButtonHovered] = ToVec4(kSurface3);
	c[ImGuiCol_ButtonActive] = ToVec4(kAccentDim);
	c[ImGuiCol_Header] = ImVec4(0.10f, 0.12f, 0.14f, 1.0f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.18f, 0.21f, 1.0f);
	c[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.23f, 0.26f, 1.0f);
	c[ImGuiCol_Separator] = ToVec4(kBorder);
	c[ImGuiCol_SeparatorHovered] = ToVec4(kAccentDim);
	c[ImGuiCol_SeparatorActive] = ToVec4(kAccent);
	c[ImGuiCol_ResizeGrip] = ImVec4(0.16f, 0.18f, 0.21f, 0.6f);
	c[ImGuiCol_ResizeGripHovered] = ToVec4(kAccentDim);
	c[ImGuiCol_ResizeGripActive] = ToVec4(kAccent);
	c[ImGuiCol_Tab] = ToVec4(kSurface1);
	c[ImGuiCol_TabHovered] = ToVec4(kSurface3);
	c[ImGuiCol_TabSelected] = ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
	c[ImGuiCol_TabSelectedOverline] = ToVec4(kAccent);
	c[ImGuiCol_TabDimmed] = ToVec4(kSurface1);
	c[ImGuiCol_TabDimmedSelected] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
	c[ImGuiCol_PlotLines] = ToVec4(kAccent);
	c[ImGuiCol_PlotLinesHovered] = ToVec4(kAccent);
	c[ImGuiCol_PlotHistogram] = ToVec4(kAccent);
	c[ImGuiCol_PlotHistogramHovered] = ToVec4(kAccent);
	c[ImGuiCol_TableHeaderBg] = ToVec4(kSurface1);
	c[ImGuiCol_TableBorderStrong] = ToVec4(kBorder);
	c[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.24f, 0.27f, 0.5f);
	c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
	c[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 0.84f, 0.88f, 0.35f);
	c[ImGuiCol_NavCursor] = ToVec4(kAccent);
	c[ImGuiCol_DragDropTarget] = ToVec4(kAccent);
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
}

bool LoadFonts(float baseSize, float dpiScale) {
	g_fonts = FontSet{};

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault(); // guaranteed fallback; index 0

	const float scale = dpiScale > 0.0f ? dpiScale : 1.0f;
	const std::filesystem::path fontDirectory = L"C:\\Windows\\Fonts";

	g_fonts.body = TryAddFont(fontDirectory / L"segoeui.ttf", baseSize * scale);
	g_fonts.heading = TryAddFont(fontDirectory / L"bahnschrift.ttf", (baseSize + 4.0f) * scale);
	g_fonts.mono = TryAddFont(fontDirectory / L"consola.ttf", (baseSize - 1.0f) * scale);
	g_fonts.systemFonts = g_fonts.body != nullptr;

	if (g_fonts.body != nullptr) io.FontDefault = g_fonts.body;
	if (g_fonts.heading == nullptr) g_fonts.heading = g_fonts.body;
	if (g_fonts.mono == nullptr) g_fonts.mono = g_fonts.body;

	return g_fonts.systemFonts;
}

FontSet& Fonts() {
	return g_fonts;
}

void PushHeading(float scale) {
	const float size = ImGui::GetFontSize() * 1.18f * scale;
	ImGui::PushFont(g_fonts.heading, size);
}

void PushMono(float scale) {
	const float size = ImGui::GetFontSize() * 0.92f * scale;
	ImGui::PushFont(g_fonts.mono, size);
}

void Pop() {
	ImGui::PopFont();
}

} // namespace nova_host::theme
