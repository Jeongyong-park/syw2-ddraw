#include "gpu.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace hq {
static const char shader[]=R"(
Texture2D<uint> source : register(t0);
cbuffer Settings : register(b0) { uint w; uint h; uint bits; uint smooth; float4 palette[256]; };
struct V { float4 pos:SV_POSITION; float2 uv:TEXCOORD; };
V vs(uint id:SV_VertexID) {
    V o; o.uv=float2((id<<1)&2,id&2);
    o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 color(int2 p) {
    p=clamp(p,int2(0,0),int2(w-1,h-1));
    int x=p.x*(bits/8); uint a=source.Load(int3(x,p.y,0));
    if(bits==8) return palette[a];
    uint b=source.Load(int3(x+1,p.y,0));
    if(bits==16) { uint v=a|(b<<8); return float4(float3((v>>11)&31,(v>>5)&63,v&31)/float3(31,63,31),1); }
    return float4(source.Load(int3(x+2,p.y,0))/255.0,b/255.0,a/255.0,1);
}
float4 ps(V i):SV_TARGET {
    float2 p=i.uv*float2(w,h);
    if(smooth==0) return color(int2(floor(p)));
    p-=0.5; int2 q=int2(floor(p)); float2 f=frac(p);
    return lerp(lerp(color(q),color(q+int2(1,0)),f.x),lerp(color(q+int2(0,1)),color(q+1),f.x),f.y);
}
)";
struct Gpu::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    int cw=0,ch=0,pitch=0,th=0;
    HRESULT init(HWND window) {
        DXGI_SWAP_CHAIN_DESC s{};
        s.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        s.SampleDesc.Count=1; s.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        s.BufferCount=1; s.OutputWindow=window; s.Windowed=TRUE;
        s.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
        HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,
            D3D11_SDK_VERSION,&s,&swap,&device,nullptr,&context);
        if(FAILED(hr)) return hr;
        ComPtr<IDXGIFactory> factory;
        hr=swap->GetParent(IID_PPV_ARGS(&factory)); if(FAILED(hr)) return hr;
        factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
        ComPtr<ID3DBlob> vs,ps,errors;
        hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors);
        if(FAILED(hr)) return hr;
        hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors);
        if(FAILED(hr)) return hr;
        hr=device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex); if(FAILED(hr)) return hr;
        hr=device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel); if(FAILED(hr)) return hr;
        D3D11_BUFFER_DESC b{}; b.ByteWidth=16+256*16; b.Usage=D3D11_USAGE_DEFAULT; b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        return device->CreateBuffer(&b,nullptr,&constants);
    }
    HRESULT draw(HWND window,const Image& image,const Palette& pal,Viewport v,bool vsync,bool linear,std::vector<uint32_t>* capture) {
        HRESULT hr;
        if(!device) { hr=init(window); if(FAILED(hr)) return hr; }
        RECT r{}; GetClientRect(window,&r); if(r.right<=0 || r.bottom<=0) return S_OK;
        if(!target || cw!=r.right || ch!=r.bottom) {
            context->OMSetRenderTargets(0,nullptr,nullptr); target.Reset();
            hr=swap->ResizeBuffers(0,r.right,r.bottom,DXGI_FORMAT_UNKNOWN,0); if(FAILED(hr)) return hr;
            ComPtr<ID3D11Texture2D> buffer; hr=swap->GetBuffer(0,IID_PPV_ARGS(&buffer)); if(FAILED(hr)) return hr;
            hr=device->CreateRenderTargetView(buffer.Get(),nullptr,&target); if(FAILED(hr)) return hr;
            cw=r.right; ch=r.bottom;
        }
        if(!texture || pitch!=image.pitch || th!=image.height) {
            ID3D11ShaderResourceView* empty=nullptr; context->PSSetShaderResources(0,1,&empty);
            view.Reset(); texture.Reset();
            D3D11_TEXTURE2D_DESC t{}; t.Width=image.pitch; t.Height=image.height;
            t.MipLevels=1; t.ArraySize=1; t.Format=DXGI_FORMAT_R8_UINT; t.SampleDesc.Count=1;
            t.Usage=D3D11_USAGE_DYNAMIC; t.BindFlags=D3D11_BIND_SHADER_RESOURCE; t.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            hr=device->CreateTexture2D(&t,nullptr,&texture); if(FAILED(hr)) return hr;
            hr=device->CreateShaderResourceView(texture.Get(),nullptr,&view); if(FAILED(hr)) return hr;
            pitch=image.pitch; th=image.height;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr=context->Map(texture.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped); if(FAILED(hr)) return hr;
        for(int y=0;y<image.height;++y)
            std::memcpy(static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch,image.bytes.data()+y*image.pitch,image.pitch);
        context->Unmap(texture.Get(),0);
        struct Constants { unsigned w,h,bits,smooth; float palette[256][4]; } c{};
        c.w=image.width; c.h=image.height; c.bits=image.bpp; c.smooth=linear;
        for(int i=0;i<256;++i) { c.palette[i][0]=((pal[i]>>16)&255)/255.f; c.palette[i][1]=((pal[i]>>8)&255)/255.f; c.palette[i][2]=(pal[i]&255)/255.f; c.palette[i][3]=1; }
        context->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);
        auto rt=target.Get(); context->OMSetRenderTargets(1,&rt,nullptr);
        const float black[]={0,0,0,1}; context->ClearRenderTargetView(rt,black);
        D3D11_VIEWPORT vp{float(v.x),float(v.y),float(v.width),float(v.height),0,1}; context->RSSetViewports(1,&vp);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
        auto cb=constants.Get(); context->PSSetConstantBuffers(0,1,&cb);
        auto srv=view.Get(); context->PSSetShaderResources(0,1,&srv);
        context->Draw(3,0);
        if(capture) {
            ComPtr<ID3D11Texture2D> back, staging;
            hr=swap->GetBuffer(0,IID_PPV_ARGS(&back)); if(FAILED(hr)) return hr;
            D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
            desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            hr=device->CreateTexture2D(&desc,nullptr,&staging); if(FAILED(hr)) return hr;
            context->CopyResource(staging.Get(),back.Get());
            D3D11_MAPPED_SUBRESOURCE read{};
            hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&read); if(FAILED(hr)) return hr;
            capture->resize(size_t(cw)*ch);
            for(int y=0;y<ch;++y) {
                auto row=static_cast<const unsigned char*>(read.pData)+y*read.RowPitch;
                for(int x=0;x<cw;++x) (*capture)[size_t(y)*cw+x]=(uint32_t(row[x*4])<<16)|(uint32_t(row[x*4+1])<<8)|row[x*4+2];
            }
            context->Unmap(staging.Get(),0);
        }
        return swap->Present(vsync?1:0,0);
    }
};
Gpu::Gpu():impl(std::make_unique<Impl>()) {}
Gpu::~Gpu()=default;
HRESULT Gpu::present(HWND w,const Image& i,const Palette& p,Viewport v,bool sync,bool linear,std::vector<uint32_t>* capture) { return impl->draw(w,i,p,v,sync,linear,capture); }
}
