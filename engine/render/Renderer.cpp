#include "Renderer.h"

#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace {

#ifdef _WIN32
GLuint loadTextureWicRgba8(const wchar_t* filePath) {
    if (filePath == nullptr) {
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        hr = S_OK;
    }
    if (FAILED(hr)) {
        return 0;
    }

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || factory == nullptr) {
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(filePath, nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || decoder == nullptr) {
        factory->Release();
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || frame == nullptr) {
        decoder->Release();
        factory->Release();
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || converter == nullptr) {
        frame->Release();
        decoder->Release();
        factory->Release();
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    hr = converter->Initialize(frame,
                               GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone,
                               nullptr,
                               0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    UINT width = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    hr = converter->CopyPixels(nullptr,
                               width * 4,
                               static_cast<UINT>(pixels.size()),
                               pixels.data());
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (comInitialized) {
            CoUninitialize();
        }
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    if (comInitialized) {
        CoUninitialize();
    }

    return texture;
}
#endif

} // namespace

static GLuint compileShader(GLenum type, const char* src) {
	GLuint id = glCreateShader(type);
	glShaderSource(id, 1, &src, nullptr);
	glCompileShader(id);
	GLint success;
	glGetShaderiv(id, GL_COMPILE_STATUS, &success);
	if (!success) {
		char buf[512];
		glGetShaderInfoLog(id, 512, nullptr, buf);
		std::cerr << "Shader compile error: " << buf << std::endl;
	}
	return id;
}

static GLuint createProgram(const char* vertSrc, const char* fragSrc) {
	GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
	GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	GLint success;
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if (!success) {
		char buf[512];
		glGetProgramInfoLog(prog, 512, nullptr, buf);
		std::cerr << "Program link error: " << buf << std::endl;
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	return prog;
}

static std::string loadFileToString(const char* path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in) return {};
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

Renderer::Renderer() {
}

Renderer::~Renderer() {
	shutdown();
}

void Renderer::onScroll(double xoffset, double yoffset) {
	// yoffset is vertical scroll (positive means scroll up) — zoom in when scrolling up
	m_Zoom -= static_cast<float>(yoffset) * 0.25f;
	if (m_Zoom < 0.2f) m_Zoom = 0.2f;
	if (m_Zoom > 10.0f) m_Zoom = 10.0f;
}

void Renderer::onMouseButton(int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
		if (action == GLFW_PRESS) {
			m_Orbiting = true;
		} else if (action == GLFW_RELEASE) {
			m_Orbiting = false;
		}
	}
}

void Renderer::onCursorPos(double xpos, double ypos) {
	// Always update last pos; when orbiting, apply deltas
	double dx = xpos - m_LastMouseX;
	double dy = ypos - m_LastMouseY;

	if (m_Orbiting) {
		const float sensitivity = 0.0055f; // tune this for feel
		m_Yaw += static_cast<float>(dx) * sensitivity; // left -> turn right
		m_Pitch += static_cast<float>(dy) * sensitivity; // down -> go up

		// Clamp pitch: never let camera go below horizon
        // min ~5 degrees above ground, max ~80 degrees overhead
        const float minPitch =  5.0f * 3.14159265f / 180.0f;  // just above ground
        const float maxPitch = 80.0f * 3.14159265f / 180.0f;  // near top-down

        if (m_Pitch < minPitch) m_Pitch = minPitch;
        if (m_Pitch > maxPitch) m_Pitch = maxPitch;
	}

	m_LastMouseX = xpos;
	m_LastMouseY = ypos;
}

void Renderer::getGroundAxes(float& forwardX, float& forwardZ,
                             float& rightX, float& rightZ) const {
    // View direction from camera to target based on yaw/pitch.
    const float dirX = std::cos(m_Pitch) * std::cos(m_Yaw);
    const float dirZ = std::cos(m_Pitch) * std::sin(m_Yaw);

    // Camera looks toward the target, which is opposite the eye offset direction.
    forwardX = -dirX;
    forwardZ = -dirZ;

    float fLen = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (fLen > 0.0001f) {
        forwardX /= fLen;
        forwardZ /= fLen;
    } else {
        forwardX = 0.0f;
        forwardZ = -1.0f;
    }

    rightX = -forwardZ;
    rightZ = forwardX;
}

void Renderer::init() {
	if (m_Initialized) return;

	// Cube vertices (positions only)
	float vertices[] = {
		-0.5f, -0.5f, -0.5f,
		 0.5f, -0.5f, -0.5f,
		 0.5f,  0.5f, -0.5f,
		-0.5f,  0.5f, -0.5f,
		-0.5f, -0.5f,  0.5f,
		 0.5f, -0.5f,  0.5f,
		 0.5f,  0.5f,  0.5f,
		-0.5f,  0.5f,  0.5f
	};

	unsigned int indices[] = {
		0,1,2, 2,3,0,
		4,5,6, 6,7,4,
		0,1,5, 5,4,0,
		2,3,7, 7,6,2,
		0,3,7, 7,4,0,
		1,2,6, 6,5,1
	};

	glGenVertexArrays(1, &m_VAO);
	glGenBuffers(1, &m_VBO);
	glGenBuffers(1, &m_EBO);

	glBindVertexArray(m_VAO);

	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

	glBindVertexArray(0);

    std::vector<float> gridVertices;

    int gridSize = 100;
    float spacing = 1.8f;

    for (int i = -gridSize; i <= gridSize; i++) {
        // lines parallel to Z
        gridVertices.push_back(i * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back(-gridSize * spacing);

        gridVertices.push_back(i * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back(gridSize * spacing);

        // lines parallel to X
        gridVertices.push_back(-gridSize * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back(i * spacing);

        gridVertices.push_back(gridSize * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back(i * spacing);
    }

	// create a full-screen triangle (NDC) for the shader-based infinite grid
	float quadVerts[] = {
		-1.0f, -1.0f,
		 3.0f, -1.0f,
		-1.0f,  3.0f
	};

	glGenVertexArrays(1, &m_QuadVAO);
	glGenBuffers(1, &m_QuadVBO);

	glBindVertexArray(m_QuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);


    // Load grid shader from assets (no fallback)
    std::string gvert = loadFileToString("engine/assets/shaders/grid.vert");
    std::string gfrag = loadFileToString("engine/assets/shaders/grid.frag");
    if (!gvert.empty() && !gfrag.empty()) {
        m_GridShader = createProgram(gvert.c_str(), gfrag.c_str());
    } else {
        std::cerr << "Failed to load grid shader files: engine/assets/shaders/grid.vert or .frag\n";
        m_GridShader = 0;
    }

    std::string vert = loadFileToString("engine/assets/shaders/basic.vert");
    std::string frag = loadFileToString("engine/assets/shaders/basic.frag");
    if (!vert.empty() && !frag.empty()) {
        m_Shader = createProgram(vert.c_str(), frag.c_str());
    } else {
        std::cerr << "Failed to load basic shader files: engine/assets/shaders/basic.vert or .frag\n";
        m_Shader = 0;
    }

    // load player shader (separate files)
    std::string pvert = loadFileToString("engine/assets/shaders/player.vert");
    std::string pfrag = loadFileToString("engine/assets/shaders/player.frag");
    if (!pvert.empty() && !pfrag.empty()) {
        m_PlayerShader = createProgram(pvert.c_str(), pfrag.c_str());
    } else {
        std::cerr << "Failed to load player shader files: engine/assets/shaders/player.vert or .frag\n";
        m_PlayerShader = 0;
    }
    // enable MSAA
    glEnable(GL_MULTISAMPLE);
	// enable depth test so cube renders correctly
	glEnable(GL_DEPTH_TEST);

#ifdef _WIN32
    m_BlockAtlasTexture = loadTextureWicRgba8(L"engine/assets/textures/chronloss-textures.png");
    if (m_BlockAtlasTexture == 0) {
        std::cerr << "Failed to load block atlas: engine/assets/textures/chronloss-textures.png\n";
    }
#else
    std::cerr << "Block atlas loading is currently implemented for Windows only.\n";
#endif

    // chunk streamer will populate chunks on demand

	m_Initialized = true;
}

void Renderer::shutdown() {
	if (!m_Initialized) return;
	if (m_EBO) glDeleteBuffers(1, &m_EBO);
	if (m_VBO) glDeleteBuffers(1, &m_VBO);
	if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
	if (m_QuadVBO) glDeleteBuffers(1, &m_QuadVBO);
	if (m_QuadVAO) glDeleteVertexArrays(1, &m_QuadVAO);
	if (m_Shader) glDeleteProgram(m_Shader);
    if (m_PlayerShader) glDeleteProgram(m_PlayerShader);
	if (m_GridShader) glDeleteProgram(m_GridShader);
    if (m_BlockAtlasTexture) glDeleteTextures(1, &m_BlockAtlasTexture);
	m_EBO = m_VBO = m_VAO = m_Shader = 0;
	m_QuadVBO = m_QuadVAO = m_GridShader = 0;
    m_BlockAtlasTexture = 0;
	m_Initialized = false;
}

void Renderer::beginFrame(float playerX, float playerY, float playerZ,
                          float camOffsetX, float camOffsetY, float camOffsetZ,
                          float deltaTime) {
    if (!m_Initialized) init();

    m_LastDrawCallCount = 0;
    m_LastVertexCount = 0;

    // clear color and depth
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- APPLY LERP TO CAMERA Y ---
    // The higher the speed multiplier, the faster the camera catches up.
    float lerpSpeed = 5.0f;
    float lerpSpeedPlayer = 10.0f;
    m_SmoothCameraY += (playerY - m_SmoothCameraY) * lerpSpeed * deltaTime;

    m_SmoothPlayerY += (playerY - m_SmoothPlayerY) * lerpSpeedPlayer * deltaTime;

    // Build an orthographic projection and an isometric view (no external math libs)
    auto normalize = [](float v[3]) {
        float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        if (len == 0.0f) return;
        v[0] /= len; v[1] /= len; v[2] /= len;
    };
    auto cross = [](const float a[3], const float b[3], float out[3]) {
        out[0] = a[1]*b[2] - a[2]*b[1];
        out[1] = a[2]*b[0] - a[0]*b[2];
        out[2] = a[0]*b[1] - a[1]*b[0];
    };
    auto dot = [](const float a[3], const float b[3]) {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    };

    // orthographic projection (column-major)
    auto ortho = [](float left, float right, float bottom, float top, float nearZ, float farZ, float out[16]) {
        float rl = right - left;
        float tb = top - bottom;
        float fn = farZ - nearZ;
        out[0] = 2.0f / rl; out[1] = 0; out[2] = 0; out[3] = 0;
        out[4] = 0; out[5] = 2.0f / tb; out[6] = 0; out[7] = 0;
        out[8] = 0; out[9] = 0; out[10] = -2.0f / fn; out[11] = 0;
        out[12] = -(right + left) / rl; out[13] = -(top + bottom) / tb; out[14] = -(farZ + nearZ) / fn; out[15] = 1.0f;
    };

    // lookAt (column-major)
    auto lookAt = [&](const float eye[3], const float center[3], const float upIn[3], float out[16]) {
        float f[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
        normalize(f);
        float up[3] = { upIn[0], upIn[1], upIn[2] };
        normalize(up);
        float s[3];
        cross(f, up, s);
        normalize(s);
        float u[3];
        cross(s, f, u);

        // column-major
        out[0] = s[0]; out[1] = u[0]; out[2] = -f[0]; out[3] = 0;
        out[4] = s[1]; out[5] = u[1]; out[6] = -f[1]; out[7] = 0;
        out[8] = s[2]; out[9] = u[2]; out[10] = -f[2]; out[11] = 0;
        out[12] = -dot(s, eye); out[13] = -dot(u, eye); out[14] = dot(f, eye); out[15] = 1.0f;
    };

    // perspective projection (column-major)
    auto perspective = [](float fovY, float aspect, float nearZ, float farZ, float out[16]) {
        float f = 1.0f / std::tan(fovY / 2.0f);
        for (int i = 0; i < 16; ++i) out[i] = 0.0f;
        out[0]  = f / aspect;
        out[5]  = f;
        out[10] = (farZ + nearZ) / (nearZ - farZ);
        out[11] = -1.0f;
        out[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    };

    auto mulMat = [](const float a[16], const float b[16], float out[16]) {
        // out = a * b (both column-major)
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    sum += a[i*4 + row] * b[col*4 + i];
                }
                out[col*4 + row] = sum;
            }
        }
    };

    int width = 800; // fallback default
    int height = 600;
    // try to query viewport size
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    width = vp[2]; height = vp[3];

    float aspect = width > 0 ? (float)width / (float)height : 4.0f/3.0f;

    float zoom = m_Zoom; // keep for grid shader usage

    float fovRadians = 60.0f * (3.14159265f / 180.0f); // 60-degree Field of View
    float nearZ = 0.1f;
    float farZ = 1000.0f;

    float P[16];
    perspective(fovRadians, aspect, nearZ, farZ, P);

    // Camera distance comes from the configured offsets, while orientation
    // comes from interactive yaw/pitch (middle-mouse orbit).
    const float configuredDistance = std::sqrt(
        camOffsetX * camOffsetX + camOffsetY * camOffsetY + camOffsetZ * camOffsetZ);
    if (configuredDistance > 0.001f) {
        m_Distance = configuredDistance * m_Zoom; // Apply zoom multiplier here
    }

    const float dirX = std::cos(m_Pitch) * std::cos(m_Yaw);
    const float dirY = std::sin(m_Pitch);
    const float dirZ = std::cos(m_Pitch) * std::sin(m_Yaw);

    // Camera looks at the player's X/Z but uses a smoothed Y to avoid jitter
    float center[3] = { playerX, m_SmoothPlayerY, playerZ };
    float eye[3] = {
        playerX + dirX * m_Distance,
        m_SmoothCameraY + dirY * m_Distance,
        playerZ + dirZ * m_Distance
    };

    // // Cast ray FROM player center TOWARD ideal eye position
    // float rayDirX = idealEye[0] - center[0];
    // float rayDirY = idealEye[1] - center[1];
    // float rayDirZ = idealEye[2] - center[2];

    // float maxDist = std::sqrt(rayDirX * rayDirX + rayDirY * rayDirY + rayDirZ * rayDirZ);
    // if (maxDist > 0.0001f) {
    //     rayDirX /= maxDist;
    //     rayDirY /= maxDist;
    //     rayDirZ /= maxDist;
    // }

    // float finalDist = maxDist; // default: no obstruction
    // float stepSize = 0.1f;     // smaller = more accurate, but more checks

    // for (float t = stepSize; t <= maxDist; t += stepSize) {
    //     float cx = center[0] + rayDirX * t;
    //     float cy = center[1] + rayDirY * t;
    //     float cz = center[2] + rayDirZ * t;

    //     int blockX = static_cast<int>(std::floor(cx));
    //     int blockY = static_cast<int>(std::floor(cy));
    //     int blockZ = static_cast<int>(std::floor(cz));

    //     std::uint8_t blockId = m_Streamer.getBlockAtWorld(blockX, blockY, blockZ);

    //     if (BlockRegistry::get(blockId).isSolid) {
    //         finalDist = std::max(0.5f, t - stepSize); // pull back one step, keep min distance
    //         break;
    //     }
    // }

    // float eye[3] = {
    //     center[0] + rayDirX * finalDist,
    //     center[1] + rayDirY * finalDist,
    //     center[2] + rayDirZ * finalDist
    // };

    float upv[3] = {0.0f, 1.0f, 0.0f};

    float gridCenterX = std::floor(eye[0]);
    float gridCenterZ = std::floor(eye[2]);

    float V[16];
    lookAt(eye, center, upv, V);

    float MVP[16];
    mulMat(P, V, MVP);

    m_LastCameraFrame.eye[0] = eye[0];
    m_LastCameraFrame.eye[1] = eye[1];
    m_LastCameraFrame.eye[2] = eye[2];
    m_LastCameraFrame.center[0] = center[0];
    m_LastCameraFrame.center[1] = center[1];
    m_LastCameraFrame.center[2] = center[2];
    m_LastCameraFrame.up[0] = upv[0];
    m_LastCameraFrame.up[1] = upv[1];
    m_LastCameraFrame.up[2] = upv[2];
    m_LastCameraFrame.fovRadians = fovRadians;
    m_LastCameraFrame.aspect = aspect;
    m_LastCameraFrame.viewportWidth = width;
    m_LastCameraFrame.viewportHeight = height;

    // DRAW THE INFINITE GRID FIRST (BACKGROUND)
    if (m_GridShader && m_QuadVAO) {
        glUseProgram(m_GridShader);
        
        // Enable Alpha Blending for the grid lines/fade
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        // compute camera basis in world space
        float fwd[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
        float upcpy[3] = { upv[0], upv[1], upv[2] };
        
        // normalize fwd
        float flen = std::sqrt(fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2]);
        fwd[0]/=flen; fwd[1]/=flen; fwd[2]/=flen;
        
        // compute right = normalize(cross(fwd, up))
        float rightv[3] = { fwd[1]*upcpy[2] - fwd[2]*upcpy[1], fwd[2]*upcpy[0] - fwd[0]*upcpy[2], fwd[0]*upcpy[1] - fwd[1]*upcpy[0] };
        float rlen = std::sqrt(rightv[0]*rightv[0] + rightv[1]*rightv[1] + rightv[2]*rightv[2]);
        if (rlen > 1e-6f) { rightv[0]/=rlen; rightv[1]/=rlen; rightv[2]/=rlen; }
        
        // recompute orthonormal up
        float upn[3] = { rightv[1]*fwd[2] - rightv[2]*fwd[1], rightv[2]*fwd[0] - rightv[0]*fwd[2], rightv[0]*fwd[1] - rightv[1]*fwd[0] };

        // Bind Uniforms
        GLint locCamPos = glGetUniformLocation(m_GridShader, "camPos");
        GLint locRight  = glGetUniformLocation(m_GridShader, "camRight");
        GLint locUp     = glGetUniformLocation(m_GridShader, "camUp");
        GLint locFwd    = glGetUniformLocation(m_GridShader, "camForward");
        GLint locHalfW  = glGetUniformLocation(m_GridShader, "halfWidth");
        GLint locHalfH  = glGetUniformLocation(m_GridShader, "halfHeight");
        GLint locZoom   = glGetUniformLocation(m_GridShader, "zoom");
        GLint locColor  = glGetUniformLocation(m_GridShader, "gridColor");
        GLint locThick  = glGetUniformLocation(m_GridShader, "lineThickness");

        if (locCamPos >= 0) glUniform3f(locCamPos, eye[0], eye[1], eye[2]);
        if (locRight >= 0)  glUniform3f(locRight, rightv[0], rightv[1], rightv[2]);
        if (locUp >= 0)     glUniform3f(locUp, upn[0], upn[1], upn[2]);
        if (locFwd >= 0)    glUniform3f(locFwd, fwd[0], fwd[1], fwd[2]);
        if (locHalfW >= 0)  glUniform1f(locHalfW, zoom * aspect);
        if (locHalfH >= 0)  glUniform1f(locHalfH, zoom);
        if (locZoom >= 0)   glUniform1f(locZoom, zoom);
        if (locColor >= 0)  glUniform3f(locColor, 0.2f, 0.2f, 0.2f);
        if (locThick >= 0)  glUniform1f(locThick, 0.02f);

        glBindVertexArray(m_QuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        ++m_LastDrawCallCount;
        m_LastVertexCount += 3;
        glBindVertexArray(0);

        // Restore state for the cube
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST); 
    }

    // Ensure nearby chunks are requested and uploaded
    m_Streamer.tick(center[0], center[1], center[2]);

    // DRAW THE WORLD CHUNKS OVER THE GRID
    glUseProgram(m_Shader);
    
    GLint loc = glGetUniformLocation(m_Shader, "MVP");
    if (loc >= 0) glUniformMatrix4fv((GLint)loc, 1, GL_FALSE, MVP);

    GLint colorLoc = glGetUniformLocation(m_Shader, "color");
    if (colorLoc >= 0) glUniform3f(colorLoc, 0.6f, 0.6f, 0.6f);

    GLint ambLoc = glGetUniformLocation(m_Shader, "ambientStrength");
    if (ambLoc >= 0) glUniform1f(ambLoc, 1.0f);

    GLint atlasLoc = glGetUniformLocation(m_Shader, "uAtlas");
    if (atlasLoc >= 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_BlockAtlasTexture);
        glUniform1i(atlasLoc, 0);
    }

    // Upload any pending meshes and render streamed chunks (with per-chunk transform)
    std::size_t chunkDrawCalls = 0;
    std::size_t chunkVertexCount = 0;
    m_Streamer.renderAll(MVP, m_Shader, &chunkDrawCalls, &chunkVertexCount);
    m_LastDrawCallCount += chunkDrawCalls;
    m_LastVertexCount += chunkVertexCount;

    // DRAW THE PLAYER AS TWO RED STACKED BLOCKS
    if (m_VAO && m_PlayerShader) {
        glUseProgram(m_PlayerShader);
        // size of each block
        const float blockHalfX = 0.3f; // half-size on X
        const float blockHalfY = 0.3f; // half-size on Y
        const float blockHalfZ = 0.3f; // half-size on Z

        // helpers to build simple transform matrices (column-major)
        auto makeTranslate = [](float x, float y, float z, float out[16]) {
            // identity then translation
            for (int i = 0; i < 16; ++i) out[i] = 0.0f;
            out[0] = 1.0f; out[5] = 1.0f; out[10] = 1.0f; out[15] = 1.0f;
            out[12] = x; out[13] = y; out[14] = z;
        };
        auto makeScale = [](float sx, float sy, float sz, float out[16]) {
            for (int i = 0; i < 16; ++i) out[i] = 0.0f;
            out[0] = sx; out[5] = sy; out[10] = sz; out[15] = 1.0f;
        };

        // compute model matrices and draw two cubes stacked vertically
        float T[16], S[16], M[16], MVP_model[16];

        GLint locMVP = glGetUniformLocation(m_PlayerShader, "MVP");
        GLint locColor2 = glGetUniformLocation(m_PlayerShader, "color");

        // bottom block center: sit on player's Y (player's Y treated as feet at ground)
        float bottomCenterY = m_SmoothPlayerY + blockHalfY;
        makeTranslate(playerX, bottomCenterY, playerZ, T);
        makeScale(blockHalfX * 2.0f, blockHalfY * 2.0f, blockHalfZ * 2.0f, S);
        mulMat(T, S, M); // M = T * S
        mulMat(MVP, M, MVP_model); // MVP_model = MVP * M
        if (locMVP >= 0) glUniformMatrix4fv(locMVP, 1, GL_FALSE, MVP_model);
        if (locColor2 >= 0) glUniform3f(locColor2, 1.0f, 0.0f, 0.0f);
        glBindVertexArray(m_VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        ++m_LastDrawCallCount;
        m_LastVertexCount += 36;
        glBindVertexArray(0);

        // top block stacked above bottom
        float topCenterY = bottomCenterY + (blockHalfY * 2.0f);
        makeTranslate(playerX, topCenterY, playerZ, T);
        // reuse S for same block size
        mulMat(T, S, M);
        mulMat(MVP, M, MVP_model);
        if (locMVP >= 0) glUniformMatrix4fv(locMVP, 1, GL_FALSE, MVP_model);
        if (locColor2 >= 0) glUniform3f(locColor2, 1.0f, 0.0f, 0.0f);
        glBindVertexArray(m_VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        ++m_LastDrawCallCount;
        m_LastVertexCount += 36;
        glBindVertexArray(0);
        // restore chunk shader
        glUseProgram(m_Shader);
    }

    // Draw hovered block outline last so it reads clearly on top of terrain.
    if (m_HasHighlightBlock && m_VAO && m_PlayerShader) {
        auto makeTranslate = [](float x, float y, float z, float out[16]) {
            for (int i = 0; i < 16; ++i) out[i] = 0.0f;
            out[0] = 1.0f; out[5] = 1.0f; out[10] = 1.0f; out[15] = 1.0f;
            out[12] = x; out[13] = y; out[14] = z;
        };
        auto makeScale = [](float sx, float sy, float sz, float out[16]) {
            for (int i = 0; i < 16; ++i) out[i] = 0.0f;
            out[0] = sx; out[5] = sy; out[10] = sz; out[15] = 1.0f;
        };

        float T[16], S[16], M[16], MVP_model[16];
        makeTranslate(static_cast<float>(m_HighlightBlockX) + 0.5f,
                  static_cast<float>(m_HighlightBlockY) + 0.5f,
                  static_cast<float>(m_HighlightBlockZ) + 0.5f,
                      T);
        makeScale(1.02f, 1.02f, 1.02f, S);
        mulMat(T, S, M);
        mulMat(MVP, M, MVP_model);

        glUseProgram(m_PlayerShader);
        GLint locMVP = glGetUniformLocation(m_PlayerShader, "MVP");
        GLint locColor = glGetUniformLocation(m_PlayerShader, "color");
        if (locMVP >= 0) glUniformMatrix4fv(locMVP, 1, GL_FALSE, MVP_model);
        if (locColor >= 0) glUniform3f(locColor, 1.0f, 0.95f, 0.2f);

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(2.0f);
        glBindVertexArray(m_VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        ++m_LastDrawCallCount;
        m_LastVertexCount += 36;
        glBindVertexArray(0);
        glLineWidth(1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glUseProgram(m_Shader);
    }
}

void Renderer::endFrame() {
	// nothing for now
}