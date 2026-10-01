#include "MazeRenderer.h"
#include "resource.h"
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {
std::vector<uint8_t> resourceData(int identifier) {
    HRSRC resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(identifier), RT_RCDATA);
    if (!resource) return {};
    HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), resource);
    auto* bytes = static_cast<const uint8_t*>(LockResource(loaded));
    return bytes ? std::vector<uint8_t>(bytes, bytes + SizeofResource(GetModuleHandleW(nullptr), resource)) : std::vector<uint8_t>{};
}

bool createBuffer(ID3D11Device* device, const void* source, UINT byteCount, UINT binding,
                  ComPtr<ID3D11Buffer>& buffer, bool dynamic = false) {
    if (!byteCount) return false;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = byteCount;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.BindFlags = binding;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA data{source};
    return SUCCEEDED(device->CreateBuffer(&desc, source ? &data : nullptr, &buffer));
}

XMFLOAT3 vector3(float x, float y, float z) { return {x, y, z}; }
} // namespace

bool MazeRenderer::createTargets() {
    RECT rect{}; GetClientRect(window_, &rect);
    width_ = std::max(1L, rect.right - rect.left);
    height_ = std::max(1L, rect.bottom - rect.top);
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    if (FAILED(device_->CreateRenderTargetView(back.Get(), nullptr, &target_))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width_; desc.Height = height_; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, &texture)) &&
           SUCCEEDED(device_->CreateDepthStencilView(texture.Get(), nullptr, &depth_));
}

bool MazeRenderer::loadTexture(int resource, ComPtr<ID3D11ShaderResourceView>& view) {
    auto bytes = resourceData(resource);
    if (bytes.empty()) return false;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(bytes.data(), UINT(bytes.size())))) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                               WICDecodeMetadataCacheOnLoad, &decoder))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height) return false;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom))) return false;
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 0; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) return false;
    context_->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), width * 4, 0);
    context_->GenerateMips(view.Get());
    return true;
}

bool MazeRenderer::loadShaders() {
    auto vsBytes = resourceData(IDR_MAZE_VS), psBytes = resourceData(IDR_MAZE_PS);
    auto mapVS = resourceData(IDR_MAP_VS), mapPS = resourceData(IDR_MAP_PS);
    if (vsBytes.empty() || psBytes.empty() || mapVS.empty() || mapPS.empty()) return false;
    if (FAILED(device_->CreateVertexShader(vsBytes.data(), vsBytes.size(), nullptr, &vertexShader_)) ||
        FAILED(device_->CreatePixelShader(psBytes.data(), psBytes.size(), nullptr, &pixelShader_)) ||
        FAILED(device_->CreateVertexShader(mapVS.data(), mapVS.size(), nullptr, &mapVertexShader_)) ||
        FAILED(device_->CreatePixelShader(mapPS.data(), mapPS.size(), nullptr, &mapPixelShader_))) return false;
    D3D11_INPUT_ELEMENT_DESC desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    D3D11_INPUT_ELEMENT_DESC mapDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    return SUCCEEDED(device_->CreateInputLayout(desc, UINT(std::size(desc)), vsBytes.data(),
                                                 vsBytes.size(), &layout_)) &&
           SUCCEEDED(device_->CreateInputLayout(mapDesc, UINT(std::size(mapDesc)), mapVS.data(), mapVS.size(), &mapLayout_));
}

bool MazeRenderer::initialize(HWND window, int quality, const maze::Grid& grid) {
    window_ = window;
    RECT rect{}; GetClientRect(window_, &rect);
    width_ = std::max(1L, rect.right - rect.left); height_ = std::max(1L, rect.bottom - rect.top);
    DXGI_SWAP_CHAIN_DESC chain{};
    chain.BufferCount = 2; chain.BufferDesc.Width = width_; chain.BufferDesc.Height = height_;
    chain.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; chain.OutputWindow = window_;
    chain.SampleDesc.Count = 1; chain.Windowed = TRUE; chain.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL feature{};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                             D3D11_SDK_VERSION, &chain, &swap_, &device_,
                                             &feature, &context_)) || !createTargets() || !loadShaders()) return false;
    const int ids[] = {IDR_WALL, IDR_FLOOR, IDR_CEILING, IDR_PIC,
                       IDR_RAT, IDR_FIN, IDR_START, IDR_GL, IDR_DX, IDR_YB};
    for (size_t i = 0; i < textures_.size(); ++i)
        if (!loadTexture(ids[i], textures_[i])) return false;
    if (!createBuffer(device_.Get(), nullptr, sizeof(Frame), D3D11_BIND_CONSTANT_BUFFER, frameBuffer_)) return false;
    // A 32x32 maze uses at most about 4,100 wall quads. Dynamic geometry is tiny.
    if (!createBuffer(device_.Get(), nullptr, 8192 * sizeof(MazeVertex), D3D11_BIND_VERTEX_BUFFER, dynamicVB_, true) ||
        !createBuffer(device_.Get(), nullptr, 12288 * sizeof(uint32_t), D3D11_BIND_INDEX_BUFFER, dynamicIB_, true) ||
        !createBuffer(device_.Get(), nullptr, 8192 * sizeof(MapVertex), D3D11_BIND_VERTEX_BUFFER, mapVB_, true)) return false;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = quality == 2 ? D3D11_FILTER_ANISOTROPIC :
                     quality == 1 ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxAnisotropy = 16; sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sampler, &sampler_))) return false;
    D3D11_BLEND_DESC blend{};
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA; target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE; target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blend, &alphaBlend_))) return false;
    D3D11_DEPTH_STENCIL_DESC alphaDepth{};
    alphaDepth.DepthEnable = TRUE;
    alphaDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    alphaDepth.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(device_->CreateDepthStencilState(&alphaDepth, &alphaDepth_))) return false;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&raster, &noCull_))) return false;
    return rebuild(grid);
}

void MazeRenderer::resize() {
    if (!swap_) return;
    RECT rect{}; GetClientRect(window_, &rect);
    if (rect.right <= 0 || rect.bottom <= 0) return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset(); depth_.Reset();
    if (SUCCEEDED(swap_->ResizeBuffers(0, UINT(rect.right), UINT(rect.bottom),
                                      DXGI_FORMAT_UNKNOWN, 0))) createTargets();
}

void MazeRenderer::quad(Geometry& geometry, XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c,
                        XMFLOAT3 d, XMFLOAT3 normal, float material, float u, float v,
                        XMFLOAT4 color) {
    auto base = uint32_t(geometry.vertices.size());
    geometry.vertices.insert(geometry.vertices.end(), {
        {a, normal, {0, 0}, material, color}, {b, normal, {u, 0}, material, color},
        {c, normal, {u, v}, material, color}, {d, normal, {0, v}, material, color}
    });
    geometry.indices.insert(geometry.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
}

bool MazeRenderer::rebuild(const maze::Grid& grid) {
    staticGeometry_ = {};
    float n = float(grid.size);
    quad(staticGeometry_, {0,0,0}, {n,0,0}, {n,0,n}, {0,0,n}, {0,1,0}, 1, n, n);
    quad(staticGeometry_, {0,maze::roomHeight,n}, {n,maze::roomHeight,n},
         {n,maze::roomHeight,0}, {0,maze::roomHeight,0}, {0,-1,0}, 2, n, n);
    for (int z = 0; z < grid.size; ++z) for (int x = 0; x < grid.size; ++x) {
        const auto& cell = grid.at(x, z);
        float a = float(x), b = float(z);
        // As in maze.js, each closed side is emitted per cell. The same wall
        // therefore has a face visible from both adjacent corridors.
        for (int direction = 0; direction < 4; ++direction) {
            if (cell.open[direction]) continue;
            float material = 0.f;
            switch (cell.picture[direction]) {
            case maze::WallPicture::teapot: material = 3.f; break;
            case maze::WallPicture::dx: material = 9.f; break;
            case maze::WallPicture::yb: material = 10.f; break;
            default: break;
            }
            switch (direction) {
            case maze::north: quad(staticGeometry_, {a,0,b}, {a+1,0,b}, {a+1,maze::roomHeight,b}, {a,maze::roomHeight,b}, {0,0,1}, material); break;
            case maze::east: quad(staticGeometry_, {a+1,0,b}, {a+1,0,b+1}, {a+1,maze::roomHeight,b+1}, {a+1,maze::roomHeight,b}, {-1,0,0}, material); break;
            case maze::south: quad(staticGeometry_, {a+1,0,b+1}, {a,0,b+1}, {a,maze::roomHeight,b+1}, {a+1,maze::roomHeight,b+1}, {0,0,-1}, material); break;
            case maze::west: quad(staticGeometry_, {a,0,b+1}, {a,0,b}, {a,maze::roomHeight,b}, {a,maze::roomHeight,b+1}, {1,0,0}, material); break;
            }
        }
    }
    staticVB_.Reset(); staticIB_.Reset();
    return createBuffer(device_.Get(), staticGeometry_.vertices.data(),
                        UINT(staticGeometry_.vertices.size() * sizeof(MazeVertex)),
                        D3D11_BIND_VERTEX_BUFFER, staticVB_) &&
           createBuffer(device_.Get(), staticGeometry_.indices.data(),
                        UINT(staticGeometry_.indices.size() * sizeof(uint32_t)),
                        D3D11_BIND_INDEX_BUFFER, staticIB_);
}

void MazeRenderer::buildDynamic(const maze::World& world) {
    dynamicGeometry_ = {};
    const float angle = world.elapsed * 1.8f;
    const float sn = std::sin(angle), cs = std::cos(angle);
    const float golden = (1.f + std::sqrt(5.f)) * .5f;
    const std::vector<XMFLOAT3> tetra = {{1,1,1},{-1,-1,1},{-1,1,-1},{1,-1,-1}};
    const std::vector<std::array<int,3>> tetraFaces = {{0,1,2},{0,3,1},{0,2,3},{1,3,2}};
    const std::vector<XMFLOAT3> octa = {{0,1,0},{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{0,-1,0}};
    const std::vector<std::array<int,3>> octaFaces = {
        {0,1,2},{0,2,3},{0,3,4},{0,4,1},{5,2,1},{5,3,2},{5,4,3},{5,1,4}
    };
    const std::vector<XMFLOAT3> icosa = {
        {-1,golden,0},{1,golden,0},{-1,-golden,0},{1,-golden,0},
        {0,-1,golden},{0,1,golden},{0,-1,-golden},{0,1,-golden},
        {golden,0,-1},{golden,0,1},{-golden,0,-1},{-golden,0,1}
    };
    const std::vector<std::array<int,3>> icosaFaces = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    auto triangle = [&](XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT4 tint) {
        XMVECTOR ab = XMVectorSubtract(XMLoadFloat3(&b), XMLoadFloat3(&a));
        XMVECTOR ac = XMVectorSubtract(XMLoadFloat3(&c), XMLoadFloat3(&a));
        XMFLOAT3 normal{};
        XMStoreFloat3(&normal, XMVector3Normalize(XMVector3Cross(ab, ac)));
        auto base = uint32_t(dynamicGeometry_.vertices.size());
        dynamicGeometry_.vertices.insert(dynamicGeometry_.vertices.end(), {
            {a,normal,{0,0},8.f,tint},{b,normal,{1,0},8.f,tint},{c,normal,{.5f,1},8.f,tint}
        });
        dynamicGeometry_.indices.insert(dynamicGeometry_.indices.end(), {base,base+1,base+2});
    };
    for (const auto& poly : world.grid.polyhedra) {
        if (!poly.active) continue;
        float cx = float(poly.cell.x) + .5f, cz = float(poly.cell.z) + .5f;
        auto transformed = [&](XMFLOAT3 original) {
            float scale = poly.type == 3 ? .145f : .17f;
            return vector3(cx + scale * (original.x * cs - original.z * sn),
                           maze::roomHeight * (world.wallHeight - .5f) + scale * original.y,
                           cz + scale * (original.x * sn + original.z * cs));
        };
        auto emit = [&](const std::vector<XMFLOAT3>& vertices,
                        const std::vector<std::array<int,3>>& faces) {
            for (size_t i = 0; i < faces.size(); ++i) {
                float tone = .56f + .34f * float(i % 7) / 6.f;
                auto f = faces[i];
                triangle(transformed(vertices[f[0]]), transformed(vertices[f[1]]),
                         transformed(vertices[f[2]]), {tone,tone,tone,1});
            }
        };
        switch (poly.type) {
        case 0: emit(tetra, tetraFaces); break;
        case 1: emit(octa, octaFaces); break;
        case 2: emit(icosa, icosaFaces); break;
        default: {
            // The dodecahedron is the dual of the icosahedron: one vertex at
            // each triangular face center, five centers around each vertex.
            std::vector<XMFLOAT3> centers;
            for (auto f : icosaFaces) {
                XMVECTOR centroid = XMVectorScale(XMVectorAdd(XMVectorAdd(
                    XMLoadFloat3(&icosa[f[0]]), XMLoadFloat3(&icosa[f[1]])),
                    XMLoadFloat3(&icosa[f[2]])), 1.f / 3.f);
                XMFLOAT3 center{};
                XMStoreFloat3(&center, XMVector3Normalize(centroid));
                centers.push_back(center);
            }
            for (size_t vertex = 0; vertex < icosa.size(); ++vertex) {
                XMVECTOR normal = XMVector3Normalize(XMLoadFloat3(&icosa[vertex]));
                XMVECTOR reference = XMVector3Normalize(XMVector3Cross(
                    normal, XMVectorSet(0,1,0,0)));
                if (XMVectorGetX(XMVector3LengthSq(reference)) < .01f)
                    reference = XMVector3Normalize(XMVector3Cross(normal, XMVectorSet(1,0,0,0)));
                XMVECTOR tangent = XMVector3Cross(normal, reference);
                std::vector<std::pair<float,int>> around;
                for (int face = 0; face < int(icosaFaces.size()); ++face) {
                    auto f = icosaFaces[face];
                    if (f[0] != int(vertex) && f[1] != int(vertex) && f[2] != int(vertex)) continue;
                    XMVECTOR center = XMLoadFloat3(&centers[face]);
                    around.push_back({std::atan2(XMVectorGetX(XMVector3Dot(center,tangent)),
                                                 XMVectorGetX(XMVector3Dot(center,reference))), face});
                }
                std::sort(around.begin(), around.end());
                if (around.size() != 5) continue;
                float tone = .55f + .35f * float(vertex % 6) / 5.f;
                for (int k = 1; k < 4; ++k)
                    triangle(transformed(centers[around[0].second]),
                             transformed(centers[around[k].second]),
                             transformed(centers[around[k+1].second]), {tone,tone,tone,1});
            }
            break;
        }
        }
    }
    polyIndexCount_ = uint32_t(dynamicGeometry_.indices.size());

    struct Sprite { float x, z, width, height, material, distance; };
    std::vector<Sprite> sprites;
    auto push = [&](float x, float z, float width, float height, float material) {
        float sx = x - world.camera.px, sz = z - world.camera.pz;
        sprites.push_back({x,z,width,height,material,sx*sx+sz*sz});
    };
    push(float(world.grid.startSign.x)+.5f, float(world.grid.startSign.z)+.5f, .75f, .75f, 6.f);
    push(float(world.grid.finish.x)+.5f, float(world.grid.finish.z)+.5f, .62f, .62f, 5.f);
    for (auto cell : world.grid.glSigns) push(float(cell.x)+.5f, float(cell.z)+.5f, .7f, .7f, 7.f);
    push(world.rat.px, world.rat.pz, .58f, .58f, 4.f);
    std::sort(sprites.begin(), sprites.end(), [](const Sprite& a, const Sprite& b) {
        return a.distance > b.distance;
    });
    float rightX = std::sin(world.camera.yaw), rightZ = -std::cos(world.camera.yaw);
    for (const Sprite& sprite : sprites) {
        float half = sprite.width * .5f;
        float bottom = maze::roomHeight * (world.wallHeight - 1.f) +
                       (maze::roomHeight - sprite.height) * .5f;
        float top = bottom + sprite.height;
        XMFLOAT3 normal{-std::cos(world.camera.yaw),0,-std::sin(world.camera.yaw)};
        quad(dynamicGeometry_,
             {sprite.x-rightX*half,top,sprite.z-rightZ*half},
             {sprite.x+rightX*half,top,sprite.z+rightZ*half},
             {sprite.x+rightX*half,bottom,sprite.z+rightZ*half},
             {sprite.x-rightX*half,bottom,sprite.z-rightZ*half}, normal, sprite.material);
    }
}

void MazeRenderer::buildMap(const maze::World& world) {
    mapVertices_.clear();
    float stepY = .58f / float(world.grid.size);
    float stepX = stepY * float(height_) / float(width_);
    const XMFLOAT4 walls{.78f, .92f, 1.f, .78f};
    const XMFLOAT4 goal{.12f, .43f, 1.f, 1.f};
    const XMFLOAT4 camera{.62f, .85f, 1.f, 1.f};
    auto point = [&](float x, float z, XMFLOAT4 color) {
        return MapVertex{{-.94f + x * stepX, .9f - z * stepY}, color};
    };
    auto line = [&](float x1, float z1, float x2, float z2) {
        mapVertices_.push_back(point(x1,z1,walls)); mapVertices_.push_back(point(x2,z2,walls));
    };
    for (int z = 0; z < world.grid.size; ++z) for (int x = 0; x < world.grid.size; ++x) {
        auto& cell = world.grid.at(x,z);
        if (!cell.open[maze::north]) line(float(x),float(z),float(x+1),float(z));
        if (!cell.open[maze::west]) line(float(x),float(z),float(x),float(z+1));
        if (x == world.grid.size-1 && !cell.open[maze::east])
            line(float(x+1),float(z),float(x+1),float(z+1));
        if (z == world.grid.size-1 && !cell.open[maze::south])
            line(float(x),float(z+1),float(x+1),float(z+1));
    }
    mapLineCount_ = uint32_t(mapVertices_.size());
    auto triangle = [&](MapVertex a, MapVertex b, MapVertex c) {
        mapVertices_.push_back(a); mapVertices_.push_back(b); mapVertices_.push_back(c);
    };
    const float gx = float(world.grid.finish.x) + .5f;
    const float gz = float(world.grid.finish.z) + .5f;
    constexpr int segments = 24;
    for (int i = 0; i < segments; ++i) {
        float a = 2.f * maze::pi * float(i) / float(segments);
        float b = 2.f * maze::pi * float(i + 1) / float(segments);
        triangle(point(gx,gz,goal),
                 point(gx + .28f * std::cos(a), gz + .28f * std::sin(a), goal),
                 point(gx + .28f * std::cos(b), gz + .28f * std::sin(b), goal));
    }
    float cx = world.camera.px, cz = world.camera.pz;
    float fx = std::cos(world.camera.yaw), fz = std::sin(world.camera.yaw);
    float bx = cx - fx * .22f, bz = cz - fz * .22f;
    triangle(point(cx + fx * .46f, cz + fz * .46f, camera),
             point(bx - fz * .26f, bz + fx * .26f, camera),
             point(bx + fz * .26f, bz - fx * .26f, camera));
}

bool MazeRenderer::render(const maze::World& world, bool showMap) {
    if (!target_ || !depth_ || !staticVB_) return false;
    XMVECTOR eye = XMVectorSet(world.camera.px, maze::roomHeight * .5f, world.camera.pz, 1);
    XMVECTOR forward = XMVectorSet(std::cos(world.camera.yaw),0,
                                  std::sin(world.camera.yaw),0);
    XMVECTOR up = XMVector3Rotate(XMVectorSet(0,1,0,0),
                                 XMQuaternionRotationAxis(forward, world.roll));
    XMMATRIX view = XMMatrixLookToLH(eye,forward,up);
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(65.f),
                                float(width_)/float(height_), .02f, 80.f);
    Frame frame{XMMatrixTranspose(view*projection),world.wallHeight,world.elapsed,
                {0,0},{world.camera.px,maze::roomHeight * .5f,world.camera.pz},0};
    context_->UpdateSubresource(frameBuffer_.Get(),0,nullptr,&frame,0,0);
    const float clear[] = {.016f,.018f,.021f,1.f};
    context_->OMSetRenderTargets(1,target_.GetAddressOf(),depth_.Get());
    context_->ClearRenderTargetView(target_.Get(),clear);
    context_->ClearDepthStencilView(depth_.Get(),D3D11_CLEAR_DEPTH,1.f,0);
    D3D11_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};
    context_->RSSetViewports(1,&viewport);
    context_->RSSetState(noCull_.Get());
    context_->IASetInputLayout(layout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(),nullptr,0);
    context_->PSSetShader(pixelShader_.Get(),nullptr,0);
    context_->VSSetConstantBuffers(0,1,frameBuffer_.GetAddressOf());
    context_->PSSetSamplers(0,1,sampler_.GetAddressOf());
    std::array<ID3D11ShaderResourceView*,8> views{};
    for (size_t i=0;i<views.size();++i) views[i]=textures_[i].Get();
    context_->PSSetShaderResources(0,UINT(views.size()),views.data());
    UINT stride=sizeof(MazeVertex), offset=0;
    context_->IASetVertexBuffers(0,1,staticVB_.GetAddressOf(),&stride,&offset);
    context_->IASetIndexBuffer(staticIB_.Get(),DXGI_FORMAT_R32_UINT,0);
    context_->DrawIndexed(UINT(staticGeometry_.indices.size()),0,0);

    buildDynamic(world);
    if (!dynamicGeometry_.indices.empty()) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(dynamicVB_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        memcpy(mapped.pData,dynamicGeometry_.vertices.data(),dynamicGeometry_.vertices.size()*sizeof(MazeVertex));
        context_->Unmap(dynamicVB_.Get(),0);
        if (FAILED(context_->Map(dynamicIB_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        memcpy(mapped.pData,dynamicGeometry_.indices.data(),dynamicGeometry_.indices.size()*sizeof(uint32_t));
        context_->Unmap(dynamicIB_.Get(),0);
        context_->IASetVertexBuffers(0,1,dynamicVB_.GetAddressOf(),&stride,&offset);
        context_->IASetIndexBuffer(dynamicIB_.Get(),DXGI_FORMAT_R32_UINT,0);
        if (polyIndexCount_) context_->DrawIndexed(polyIndexCount_,0,0);
        if (polyIndexCount_ < dynamicGeometry_.indices.size()) {
            const float blendFactor[] = {0,0,0,0};
            context_->OMSetBlendState(alphaBlend_.Get(),blendFactor,0xffffffff);
            context_->OMSetDepthStencilState(alphaDepth_.Get(),0);
            context_->DrawIndexed(UINT(dynamicGeometry_.indices.size()-polyIndexCount_),polyIndexCount_,0);
            context_->OMSetDepthStencilState(nullptr,0);
            context_->OMSetBlendState(nullptr,blendFactor,0xffffffff);
        }
    }
    if (showMap) {
        buildMap(world);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(mapVB_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
            memcpy(mapped.pData,mapVertices_.data(),mapVertices_.size()*sizeof(MapVertex));
            context_->Unmap(mapVB_.Get(),0);
            UINT mapStride=sizeof(MapVertex);
            context_->IASetInputLayout(mapLayout_.Get());
            context_->IASetVertexBuffers(0,1,mapVB_.GetAddressOf(),&mapStride,&offset);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            context_->VSSetShader(mapVertexShader_.Get(),nullptr,0);
            context_->PSSetShader(mapPixelShader_.Get(),nullptr,0);
            const float blendFactor[] = {0,0,0,0};
            context_->OMSetBlendState(alphaBlend_.Get(),blendFactor,0xffffffff);
            context_->OMSetDepthStencilState(alphaDepth_.Get(),0);
            context_->Draw(mapLineCount_,0);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->Draw(UINT(mapVertices_.size())-mapLineCount_,mapLineCount_);
            context_->OMSetDepthStencilState(nullptr,0);
            context_->OMSetBlendState(nullptr,blendFactor,0xffffffff);
        }
    }
    return SUCCEEDED(swap_->Present(1,0));
}
