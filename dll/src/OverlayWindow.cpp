#include "OverlayWindow.hpp"

#include "Theme.hpp"

#include "nova/Logging.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <dwmapi.h>
#include <dxgi1_2.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message,
                                                             WPARAM wParam, LPARAM lParam);

namespace nova_host {
namespace {

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
	D3D_FEATURE_LEVEL_11_1,
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
};

RECT TargetClientRect(HWND target) {
	RECT client{};
	GetClientRect(target, &client);
	POINT topLeft{ client.left, client.top };
	ClientToScreen(target, &topLeft);
	return RECT{ topLeft.x, topLeft.y, topLeft.x + client.right, topLeft.y + client.bottom };
}

} // namespace

OverlayWindow::~OverlayWindow() {
	Shutdown();
}

bool OverlayWindow::IsValid() const {
	return window_ != nullptr && device_ != nullptr && context_ != nullptr && swapChain_ != nullptr;
}

const wchar_t* OverlayWindow::ClassName() {
	return kWindowClass;
}

float OverlayWindow::DpiScale() const {
	if (imguiReady_ && window_ != nullptr) {
		const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window_);
		if (scale > 0.0f) return scale;
	}
	return 1.0f;
}

bool OverlayWindow::RegisterWindowClass() {
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &OverlayWindow::WindowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	// No class cursor: the game owns the cursor whenever the menu is hidden.
	windowClass.hCursor = nullptr;
	windowClass.lpszClassName = kWindowClass;
	if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		return false;
	}
	return true;
}

bool OverlayWindow::Initialize(HWND targetWindow, std::wstring* error) {
	if (targetWindow == nullptr) {
		if (error != nullptr) *error = L"No target window.";
		return false;
	}
	target_ = targetWindow;

	if (!RegisterWindowClass()) {
		if (error != nullptr) *error = L"RegisterClassEx failed.";
		return false;
	}

	const RECT rect = TargetClientRect(target_);
	width_ = rect.right - rect.left;
	height_ = rect.bottom - rect.top;

	window_ = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		kWindowClass, L"NOVA Overlay", WS_POPUP,
		rect.left, rect.top, width_, height_,
		nullptr, nullptr, GetModuleHandleW(nullptr), this);
	if (window_ == nullptr) {
		if (error != nullptr) *error = L"CreateWindowEx failed.";
		return false;
	}
	SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	// Start hidden (layered + click-through) with the layered attributes
	// initialised. The window is created without WS_VISIBLE and is shown only
	// after the style is in place; startup fails instead of showing an
	// unstyled window.
	if (!ApplyExtendedStyle()) {
		if (error != nullptr) *error = L"Overlay extended style setup failed.";
		return false;
	}

	const MARGINS margins = { -1, -1, -1, -1 };
	DwmExtendFrameIntoClientArea(window_, &margins);

	ShowWindow(window_, SW_SHOWNOACTIVATE);
	UpdateWindow(window_);

	if (!CreateDeviceResources()) {
		if (error != nullptr) *error = L"D3D11 device/swap chain creation failed.";
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr; // layout persists through NOVA settings
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	theme::Apply();
	theme::LoadFonts(15.0f, DpiScale());

	if (!ImGui_ImplWin32_Init(window_) ||
	    !ImGui_ImplDX11_Init(device_.Get(), context_.Get())) {
		if (error != nullptr) *error = L"ImGui backend initialization failed.";
		return false;
	}
	ImGui_ImplDX11_CreateDeviceObjects();
	imguiReady_ = true;

	nova::LogInfo("overlay window created (" + std::to_string(width_) + "x" +
	              std::to_string(height_) + ")");
	return true;
}

bool OverlayWindow::CreateDeviceResources() {
	UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

	DXGI_SWAP_CHAIN_DESC1 description = {};
	description.Width = static_cast<UINT>(width_);
	description.Height = static_cast<UINT>(height_);
	description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	description.Stereo = FALSE;
	description.SampleDesc.Count = 1;
	description.SampleDesc.Quality = 0;
	description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	description.BufferCount = 2;
	description.Scaling = DXGI_SCALING_STRETCH;
	description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	description.Flags = 0;

	HRESULT result = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
		kFeatureLevels, ARRAYSIZE(kFeatureLevels), D3D11_SDK_VERSION,
		device_.ReleaseAndGetAddressOf(), &featureLevel, context_.ReleaseAndGetAddressOf());
	if (FAILED(result)) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
	if (FAILED(device_.As(&dxgiDevice))) return false;

	Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
	if (FAILED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf()))) return false;

	Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
	if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())))) return false;

	if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), window_, &description,
	                                           nullptr, nullptr,
	                                           swapChain_.ReleaseAndGetAddressOf()))) {
		return false;
	}

	factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
	return CreateRenderTarget();
}

bool OverlayWindow::CreateRenderTarget() {
	if (swapChain_ == nullptr || device_ == nullptr) return false;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
	if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf())))) {
		return false;
	}
	return SUCCEEDED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr,
	                                                 renderTarget_.ReleaseAndGetAddressOf()));
}

void OverlayWindow::ReleaseRenderTarget() {
	renderTarget_.Reset();
}

bool OverlayWindow::ResizeSwapChain(int width, int height) {
	if (width <= 0 || height <= 0) return false;
	if (swapChain_ == nullptr) return false;

	ReleaseRenderTarget();
	context_->OMSetRenderTargets(0, nullptr, nullptr);

	const HRESULT result = swapChain_->ResizeBuffers(0, static_cast<UINT>(width),
	                                                 static_cast<UINT>(height),
	                                                 DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(result)) {
		nova::LogWarn("swap chain resize failed (0x" + std::to_string(static_cast<unsigned long>(result)) + ")");
		return RecreateDevice();
	}
	width_ = width;
	height_ = height;
	return CreateRenderTarget();
}

bool OverlayWindow::RecreateDevice() {
	nova::LogWarn("recreating overlay device resources");
	ReleaseRenderTarget();
	swapChain_.Reset();
	context_.Reset();
	device_.Reset();
	if (imguiReady_) {
		ImGui_ImplDX11_Shutdown();
		imguiReady_ = false;
	}
	if (!CreateDeviceResources()) return false;
	if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get())) return false;
	ImGui_ImplDX11_CreateDeviceObjects();
	imguiReady_ = true;
	return true;
}

bool OverlayWindow::SyncToTarget() {
	if (window_ == nullptr || target_ == nullptr) return false;
	if (!IsWindow(target_)) return false;

	if (IsIconic(target_)) {
		if (shown_) {
			ShowWindow(window_, SW_HIDE);
			shown_ = false;
		}
		return false;
	}

	const HWND foreground = GetForegroundWindow();
	const bool ours = foreground == window_ || foreground == target_;
	if (!ours) {
		if (shown_) {
			ShowWindow(window_, SW_HIDE);
			shown_ = false;
		}
		return false;
	}
	if (!shown_) {
		ShowWindow(window_, SW_SHOWNOACTIVATE);
		shown_ = true;
	}

	const RECT rect = TargetClientRect(target_);
	const int width = rect.right - rect.left;
	const int height = rect.bottom - rect.top;
	if (width <= 0 || height <= 0) return false;

	if (width != width_ || height != height_) {
		SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top, width, height,
		             SWP_NOACTIVATE | SWP_NOREDRAW);
		if (!ResizeSwapChain(width, height)) {
			rendererFailed_ = true;
			return false;
		}
	} else {
		RECT current{};
		GetWindowRect(window_, &current);
		if (current.left != rect.left || current.top != rect.top) {
			SetWindowPos(window_, HWND_TOPMOST, rect.left, rect.top, 0, 0,
			             SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
		}
	}

	if (menuVisible_ && foreground == target_) {
		FocusWindow(window_);
	}
	return true;
}

bool OverlayWindow::ApplyExtendedStyle() {
	if (window_ == nullptr) return false;

	// Hidden menu: mirror the reference overlay — a layered window with
	// WS_EX_TRANSPARENT is skipped by hit-testing entirely, so the game owns
	// mouse input and its own cursor. Visible menu: drop layering so the panel
	// receives clicks and keyboard focus.
	LONG_PTR style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
	if (!menuVisible_) {
		style |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
	}

	SetLastError(ERROR_SUCCESS);
	SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
	if (GetLastError() != ERROR_SUCCESS) return false;

	if (!menuVisible_) {
		// A window that carries WS_EX_LAYERED stays invisible until its layered
		// attributes are initialised; this is required at creation and whenever
		// the style is re-added. Alpha 255 leaves the D3D11 content untouched;
		// per-pixel transparency still comes from the swap chain.
		if (SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA) == FALSE) return false;
	}

	return SetWindowPos(window_, nullptr, 0, 0, 0, 0,
	                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
}

void OverlayWindow::FocusWindow(HWND window) {
	if (window == nullptr) return;
	const HWND foreground = GetForegroundWindow();
	const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
	const DWORD currentThread = GetCurrentThreadId();
	const bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
	                      AttachThreadInput(foregroundThread, currentThread, TRUE) != 0;
	SetForegroundWindow(window);
	SetFocus(window);
	if (attached) AttachThreadInput(foregroundThread, currentThread, FALSE);
}

void OverlayWindow::SetMenuVisible(bool visible) {
	menuVisible_ = visible;
	if (!ApplyExtendedStyle()) {
		nova::LogWarn("failed to apply overlay extended style");
	}
	if (visible) {
		FocusWindow(window_);
	} else if (GetForegroundWindow() == window_) {
		// Give activation back to the game only when we actually hold it; the
		// game otherwise keeps its own activation and cursor state.
		FocusWindow(target_);
	}
}

bool OverlayWindow::BeginFrame(float* width, float* height) {
	if (!IsValid() || !imguiReady_) return false;

	// Track DPI changes live (monitor moves, per-monitor scaling).
	ImGui::GetStyle().FontScaleDpi = DpiScale();

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	if (width != nullptr) *width = io.DisplaySize.x;
	if (height != nullptr) *height = io.DisplaySize.y;
	return true;
}

void OverlayWindow::EndFrame() {
	if (!IsValid() || !imguiReady_) return;

	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
	context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	const HRESULT result = swapChain_->Present(1, 0);
	if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
		nova::LogWarn("overlay device lost; attempting recovery");
		if (!RecreateDevice()) {
			rendererFailed_ = true;
			nova::LogError("overlay device recovery failed");
		}
	}
}

void OverlayWindow::Shutdown() {
	if (window_ != nullptr) {
		SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
	}
	if (imguiReady_) {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		imguiReady_ = false;
	}
	if (ImGui::GetCurrentContext() != nullptr) {
		ImGui::DestroyContext();
	}
	ReleaseRenderTarget();
	swapChain_.Reset();
	context_.Reset();
	device_.Reset();

	if (window_ != nullptr) {
		DestroyWindow(window_);
		window_ = nullptr;
	}
	UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

LRESULT WINAPI OverlayWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	if (self != nullptr && self->menuVisible_ &&
	    ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam)) {
		return TRUE;
	}

	switch (message) {
	case WM_CLOSE:
		if (self != nullptr) self->closeRequested_ = true;
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_ERASEBKGND:
		return 1; // DWM composition handles the background
	case WM_NCHITTEST:
		// With the menu hidden the overlay must be fully transparent to the
		// mouse so the game owns aiming and its own cursor handling.
		if (self == nullptr || !self->menuVisible_) return HTTRANSPARENT;
		break;
	case WM_SETCURSOR:
		// Never let the system cursor linger over the overlay: the game draws
		// its own crosshair/cursor, and ImGui draws the software cursor while
		// the menu is open.
		SetCursor(nullptr);
		return TRUE;
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
		break;
	case WM_ACTIVATE:
	case WM_ACTIVATEAPP:
	case WM_NCACTIVATE:
		if (self != nullptr) self->ApplyExtendedStyle();
		break;
	default:
		break;
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace nova_host
