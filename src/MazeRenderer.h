#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <vector>
#include "MazeLogic.h"

struct MazeVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
    float material;
    DirectX::XMFLOAT4 color;
};

class MazeRenderer {
public:
    bool initialize(HWND window, int quality, const maze::Grid& grid);
    void resize();
    bool rebuild(const maze::Grid& grid);
    bool render(const maze::World& world, bool showMap);
private:
    struct Geometry { std::vector<MazeVertex> vertices; std::vector<uint32_t> indices; };
    struct MapVertex { DirectX::XMFLOAT2 position; DirectX::XMFLOAT4 color; };
    struct Frame {
        DirectX::XMMATRIX viewProjection;
        float wallHeight, elapsed;
        float padding0[2];
        DirectX::XMFLOAT3 eye;
        float padding1;
    };
    HWND window_ = nullptr;
    int width_ = 1, height_ = 1;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_, mapVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_, mapPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_, mapLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> staticVB_, staticIB_, dynamicVB_, dynamicIB_, mapVB_, frameBuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> alphaBlend_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> alphaDepth_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> noCull_;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 10> textures_;
    Geometry staticGeometry_, dynamicGeometry_;
    uint32_t polyIndexCount_ = 0;
    uint32_t mapLineCount_ = 0;
    std::vector<MapVertex> mapVertices_;
    bool createTargets();
    bool loadTexture(int resource, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view);
    bool loadShaders();
    static void quad(Geometry& geometry, DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b,
                     DirectX::XMFLOAT3 c, DirectX::XMFLOAT3 d, DirectX::XMFLOAT3 normal,
                     float material, float u = 1.f, float v = 1.f,
                     DirectX::XMFLOAT4 color = {1.f, 1.f, 1.f, 1.f});
    void buildDynamic(const maze::World& world);
    void buildMap(const maze::World& world);
};
