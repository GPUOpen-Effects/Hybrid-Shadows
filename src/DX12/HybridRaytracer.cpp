// AMD SampleDX12 sample code
// 
// Copyright(c) 2020 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "stdafx.h"
#include <intrin.h>

#include "HybridRaytracer.h"


HybridRaytracer::HybridRaytracer(LPCSTR name) : FrameworkWindows(name)
{
	m_time = 0;
	m_bPlay = true;

	m_pGltfLoader = NULL;
}

//--------------------------------------------------------------------------------------
//
// OnParseCommandLine
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight)
{
	// set some default values
	*pWidth = 1920; 
	*pHeight = 1080; 
	m_activeScene = 0; //load the first one by default
	m_VsyncEnabled = false;
	m_bIsBenchmarking = false;
	m_fontSize = 13.f; // default value overridden by a json file if available
	m_isCpuValidationLayerEnabled = false;
	m_isGpuValidationLayerEnabled = false;
	m_activeCamera = 0;
	m_stablePowerState = false;


	//read globals
	auto process = [&](json jData)
	{
		*pWidth = jData.value("width", *pWidth);
		*pHeight = jData.value("height", *pHeight);
		m_fullscreenMode = jData.value("presentationMode", m_fullscreenMode);
		m_activeScene = jData.value("activeScene", m_activeScene);
		m_activeCamera = jData.value("activeCamera", m_activeCamera);
		m_isCpuValidationLayerEnabled = jData.value("CpuValidationLayerEnabled", m_isCpuValidationLayerEnabled);
		m_isGpuValidationLayerEnabled = jData.value("GpuValidationLayerEnabled", m_isGpuValidationLayerEnabled);
		m_VsyncEnabled = jData.value("vsync", m_VsyncEnabled);
		m_bIsBenchmarking = jData.value("benchmark", m_bIsBenchmarking);
		m_stablePowerState = jData.value("stablePowerState", m_stablePowerState);
		m_fontSize = jData.value("fontsize", m_fontSize);
	};

	//read json globals from commandline
	//
	try
	{
		if (strlen(lpCmdLine) > 0)
		{
			auto j3 = json::parse(lpCmdLine);
			process(j3);
		}
	}
	catch (json::parse_error)
	{
		Trace("Error parsing commandline\n");
		exit(0);
	}

	// read config file (and override values from commandline if so)
	//
	{
		std::ifstream f("HybridRaytracer.json");
		if (!f)
		{
			MessageBox(NULL, "Config file not found!\n", "Cauldron Panic!", MB_ICONERROR);
			exit(0);
		}

		try
		{
			f >> m_jsonConfigFile;
		}
		catch (json::parse_error)
		{
			MessageBox(NULL, "Error parsing HybridRaytracer.json!\n", "Cauldron Panic!", MB_ICONERROR);
			exit(0);
		}
	}


	json globals = m_jsonConfigFile["globals"];
	process(globals);

	// get the list of scenes
	for (const auto & scene : m_jsonConfigFile["scenes"])
		m_sceneNames.push_back(scene["name"]);
}

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnCreate()
{
	if(m_device.IsRT11Supported() == false)
	{ 
		ShowCustomErrorMessageBox(L"This sample requires a DXR 1.1 capable GPU.");
		PostQuitMessage(1);
	}


	//init the shader compiler
	InitDirectXCompiler();
	CreateShaderCache();

	m_UIState.Initialize();
	// Create a instance of the renderer and initialize it, we need to do that for each GPU
	m_pRenderer = new Renderer();
	m_pRenderer->OnCreate(&m_device, &m_swapChain, m_fontSize, &m_UIState);

	// init GUI (non gfx stuff)
	ImGUI_Init((void *)m_windowHwnd);

	OnResize();
	OnUpdateDisplay();

	// Init Camera, looking at the origin
	m_camera.LookAt(math::Vector4(0, 0, 5, 0), math::Vector4(0, 0, 0, 0));
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnDestroy()
{
	ImGUI_Shutdown();

	m_device.GPUFlush();

	m_pRenderer->UnloadScene();
	m_pRenderer->OnDestroyWindowSizeDependentResources();
	m_pRenderer->OnDestroy();

	delete m_pRenderer;

	//shut down the shader compiler 
	DestroyShaderCache(&m_device);

	if (m_pGltfLoader)
	{
		delete m_pGltfLoader;
		m_pGltfLoader = NULL;
	}
}

//--------------------------------------------------------------------------------------
//
// OnEvent, win32 sends us events and we forward them to ImGUI
//
//--------------------------------------------------------------------------------------
static void ToggleBool(bool& b) { b = !b; }
bool HybridRaytracer::OnEvent(MSG msg)
{
	if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
		return true;

	// handle function keys (F1, F2...) here, rest of the input is handled
	// by imGUI later in HandleInput() function
	bool bShiftKeyPressed = false;
	const WPARAM& KeyPressed = msg.wParam;
	switch (msg.message)
	{
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (msg.wParam == VK_SHIFT) bShiftKeyPressed = true;
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (KeyPressed == VK_SHIFT) bShiftKeyPressed = false;

		/* WINDOW TOGGLES */
		if (KeyPressed == VK_F1) ToggleBool(m_UIState.bShowControlsWindow);
		if (KeyPressed == VK_F2) ToggleBool(m_UIState.bShowProfilerWindow);
		break;
	}

	return true;
}

//--------------------------------------------------------------------------------------
//
// OnResize
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnResize()
{
	// Destroy resources (if we are not minimized)
	if (m_Width && m_Height && m_pRenderer)
	{
		m_pRenderer->OnDestroyWindowSizeDependentResources();
		m_pRenderer->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, &m_UIState);
	}
	
	m_camera.SetFov(AMD_PI_OVER_4, m_Width, m_Height, 0.1f, 1000.0f);
}

//--------------------------------------------------------------------------------------
//
// UpdateDisplay
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnUpdateDisplay()
{
	// Destroy resources (if we are not minimized)
	if (m_pRenderer)
	{
		m_pRenderer->OnUpdateDisplayDependentResources(&m_swapChain);
	}
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::LoadScene(int sceneIndex)
{
	json scene = m_jsonConfigFile["scenes"][sceneIndex];

	// release everything and load the GLTF, just the light json data, the rest (textures and geometry) will be done in the main loop
	if (m_pGltfLoader != NULL)
	{
		m_pRenderer->UnloadScene();
		m_pRenderer->OnDestroyWindowSizeDependentResources();
		m_pRenderer->OnDestroy();
		m_pGltfLoader->Unload();
		m_pRenderer->OnCreate(&m_device, &m_swapChain, m_fontSize, &m_UIState);
		m_pRenderer->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, &m_UIState);
	}

	delete(m_pGltfLoader);
	m_pGltfLoader = new GLTFCommon();
	if (m_pGltfLoader->Load(scene["directory"], scene["filename"]) == false)
	{
		MessageBox(NULL, "The selected model couldn't be found, please check the documentation", "Cauldron Panic!", MB_ICONERROR);
		exit(0);
	}

	// Load the UI settings, and also some defaults cameras and lights, in case the GLTF has none
	{
#define LOAD(j, key, val) val = j.value(key, val)

		// global settings
		LOAD(scene, "TAA", m_UIState.bUseTAA);
		LOAD(scene, "toneMapper", m_UIState.SelectedTonemapperIndex);
		LOAD(scene, "skyDomeType", m_UIState.SelectedSkydomeTypeIndex);
		LOAD(scene, "exposure", m_UIState.Exposure);
		LOAD(scene, "iblFactor", m_UIState.IBLFactor);
		LOAD(scene, "emmisiveFactor", m_UIState.EmissiveFactor);
		LOAD(scene, "skyDomeType", m_UIState.SelectedSkydomeTypeIndex);
		LOAD(scene, "sunSize", m_UIState.sunSizeAngle) * (2 * AMD_PI) / 360.0f;
		LOAD(scene, "hybirdMode", m_UIState.hMode);
		LOAD(scene, "alphaMaskMode", m_UIState.amMode);
		LOAD(scene, "shadowMapSize", m_UIState.shadowMapWidth);

		for (uint32_t i = 0; i < _countof(k_shadowMapWidthNames); ++i)
		{
			if (k_shadowMapWidths[i] == m_UIState.shadowMapWidth)
			{
				m_UIState.shadowMapWidthIndex = i;
				break;
			}
		}


		// Add a default light in case there are none
		//
		if (m_pGltfLoader->m_lights.size() == 0)
		{
			tfNode n;
			n.m_tranform.LookAt(PolarToVector(7.0f * AMD_PI / 16.0f, 5.5f * AMD_PI / 16.0f)*3.5f, math::Vector4(0, 0, 0, 0));

			tfLight l;
			l.m_type = tfLight::LIGHT_DIRECTIONAL;
			l.m_intensity = scene.value("intensity", 1.0f);
			l.m_color = math::Vector4(1.0f, 1.0f, 1.0f, 0.0f);
			l.m_range = 15;
			l.m_outerConeAngle = AMD_PI_OVER_4;
			l.m_innerConeAngle = AMD_PI_OVER_4 * 0.9f;

			m_pGltfLoader->AddLight(n, l);
		}
		
		// Allocate shadow information (if any)
		m_pRenderer->OnResizeShadowMapWidth(&m_UIState);

		// set default camera
		//
		json camera = scene["camera"];
		m_activeCamera = scene.value("activeCamera", m_activeCamera);
		math::Vector4 from = GetVector(GetElementJsonArray(camera, "defaultFrom", { 0.0, 0.0, 10.0 }));
		math::Vector4 to = GetVector(GetElementJsonArray(camera, "defaultTo", { 0.0, 0.0, 0.0 }));
		m_camera.LookAt(from, to);

		// set benchmarking state if enabled 
		//
		if (m_bIsBenchmarking)
		{
			std::string deviceName;
			std::string driverVersion;
			m_device.GetDeviceInfo(&deviceName, &driverVersion);
			BenchmarkConfig(scene["BenchmarkSettings"], m_activeCamera, m_pGltfLoader, deviceName, driverVersion);
		} 

		// indicate the mainloop we started loading a GLTF and it needs to load the rest (textures and geometry)
		m_loadingScene = true;
	}
}


//--------------------------------------------------------------------------------------
//
// OnUpdate
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnUpdate()
{
	ImGuiIO& io = ImGui::GetIO();

	//If the mouse was not used by the GUI then it's for the camera
	if (io.WantCaptureMouse)
	{
		io.MouseDelta.x = 0;
		io.MouseDelta.y = 0;
		io.MouseWheel = 0;
	}
	
	// Update Camera
	UpdateCamera(m_camera, io);
	if (m_UIState.bUseTAA)
	{
		static uint32_t Seed;
		m_camera.SetProjectionJitter(m_Width, m_Height, Seed);
	}

	// Keyboard & Mouse
	HandleInput(io);

	// Animation Update
	if (m_bPlay)
		m_time += (float)m_deltaTime / 1000.0f; // animation time in seconds

	if (m_pGltfLoader)
	{
		m_pGltfLoader->SetAnimationTime(0, m_time);
		m_pGltfLoader->TransformScene(0, math::Matrix4::identity());
	}
}
void HybridRaytracer::HandleInput(const ImGuiIO& io)
{
	auto fnIsKeyTriggered = [&io](char key) { return io.KeysDown[key] && io.KeysDownDuration[key] == 0.0f; };
	
	// Handle Keyboard/Mouse input here

	/* MAGNIFIER CONTROLS */
	if (fnIsKeyTriggered('L'))                       m_UIState.ToggleMagnifierLock();
	if (fnIsKeyTriggered('M') || io.MouseClicked[2]) ToggleBool(m_UIState.bUseMagnifier); // middle mouse / M key toggles magnifier

	if (io.MouseClicked[1] && m_UIState.bUseMagnifier) // right mouse click
		m_UIState.ToggleMagnifierLock();
}
void HybridRaytracer::UpdateCamera(Camera& cam, const ImGuiIO& io)
{
	float yaw = cam.GetYaw();
	float pitch = cam.GetPitch();
	float distance = cam.GetDistance();

	cam.UpdatePreviousMatrices(); // set previous view matrix

	// Sets Camera based on UI selection (WASD, Orbit or any of the GLTF cameras)
	if ((io.KeyCtrl == false) && (io.MouseDown[0] == true))
	{
		yaw -= io.MouseDelta.x / 100.f;
		pitch += io.MouseDelta.y / 100.f;
	}

	// Choose camera movement depending on setting
	if (m_activeCamera == 0)
	{
		//  Orbiting
		distance -= (float)io.MouseWheel / 3.0f;
		distance = std::max<float>(distance, 0.1f);

		bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);

		cam.UpdateCameraPolar(yaw, pitch, 
			panning ? -io.MouseDelta.x / 100.0f : 0.0f, 
			panning ? io.MouseDelta.y / 100.0f : 0.0f, 
			distance);
	}
	else if (m_activeCamera == 1)
	{
		//  WASD
		cam.UpdateCameraWASD(yaw, pitch, io.KeysDown, io.DeltaTime);
	}
	else if (m_activeCamera > 1)
	{
		// Use a camera from the GLTF
		m_pGltfLoader->GetCamera(m_activeCamera - 2, &cam);
	}
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void HybridRaytracer::OnRender()
{
	// Do any start of frame necessities
	BeginFrame();

	ImGUI_UpdateIO();
	ImGui::NewFrame();

	if (m_loadingScene)
	{
		// the scene loads in chunks, that way we can show a progress bar
		static int loadingStage = 0;
		loadingStage = m_pRenderer->LoadScene(m_pGltfLoader, loadingStage);
		if (loadingStage == 0)
		{
			m_time = 0;
			m_loadingScene = false;
		}
	}
	else if (m_pGltfLoader && m_bIsBenchmarking)
	{
		// Benchmarking takes control of the time, and exits the app when the animation is done
		std::vector<TimeStamp> timeStamps = m_pRenderer->GetTimingValues();
		m_time = BenchmarkLoop(timeStamps, &m_camera, m_pRenderer->GetScreenshotFileName());
	}
	else
	{
		BuildUI();  // UI logic. Note that the rendering of the UI happens later.
		OnUpdate(); // Update camera, handle keyboard/mouse input
	}

	// Do Render frame using AFR
	m_pRenderer->OnRender(&m_UIState, m_camera, &m_swapChain);

	// Framework will handle Present and some other end of frame logic
	EndFrame();
}


//--------------------------------------------------------------------------------------
//
// WinMain
//
//--------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nCmdShow)
{
	LPCSTR Name = "Hybrid Shadows v1.0";

	// create new DX sample
	return RunFramework(hInstance, lpCmdLine, nCmdShow, new HybridRaytracer(Name));
}
