// ============================================================================
// OverlayWindow — transparent D3D11 top-level overlay bound to the target
// window. Tracks client geometry, minimization, DPI, monitor moves and
// Alt-Tab. Exclusive fullscreen is not supported.
// ============================================================================
#pragma once
#include <Windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <string>

namespace mythos_host {

class OverlayWindow {
public:
	OverlayWindow() = default;
	~OverlayWindow();

	OverlayWindow(const OverlayWindow&) = delete;
	OverlayWindow& operator=(const OverlayWindow&) = delete;

	bool Initialize(HWND targetWindow, std::wstring* error);
	void Shutdown();

	[[nodiscard]] bool IsValid() const;
	[[nodiscard]] HWND Window() const { return window_; }
	[[nodiscard]] HWND Target() const { return target_; }
	[[nodiscard]] bool MenuVisible() const { return menuVisible_; }
	[[nodiscard]] bool CloseRequested() const { return closeRequested_; }
	[[nodiscard]] bool RendererFailed() const { return rendererFailed_.load(); }
	[[nodiscard]] float DpiScale() const;
	[[nodiscard]] static const wchar_t* ClassName();

	// Hides the overlay while the target is minimized or another app is in the
	// foreground; repositions/resizes the swap chain when the client area
	// changes. Returns false when the overlay should not render this frame.
	bool SyncToTarget();

	void SetMenuVisible(bool visible);
	void RequestClose() { closeRequested_ = true; }

	// Returns false when the device was lost and could not be recovered.
	bool BeginFrame(float* width, float* height);
	void EndFrame();

private:
	static LRESULT WINAPI WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool RegisterWindowClass();
	bool CreateDeviceResources();
	bool CreateRenderTarget();
	void ReleaseRenderTarget();
	bool ResizeSwapChain(int width, int height);
	bool RecreateDevice();
	void FocusWindow(HWND window);
	// Applies the hidden (layered/click-through) or visible (interactive)
	// extended style and returns false if any Win32 call fails.
	bool ApplyExtendedStyle();

	HWND window_ = nullptr;
	HWND target_ = nullptr;
	bool menuVisible_ = false;
	bool closeRequested_ = false;
	std::atomic<bool> rendererFailed_{ false }; // written by render, read by worker
	bool shown_ = true;
	int  width_ = 0;
	int  height_ = 0;
	bool imguiReady_ = false;

	Microsoft::WRL::ComPtr<ID3D11Device> device_;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;

	static constexpr const wchar_t* kWindowClass = L"MYTHOS.Overlay.Window";
};

} // namespace mythos_host
