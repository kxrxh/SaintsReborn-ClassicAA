#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 failure "+std::to_string(uint32_t(hr)));}
template<class... T>void TestLog(const char* message,T&&...){std::cout<<message<<'\n';}
#define REXGPU_ERROR(...) TestLog(__VA_ARGS__)
#define REXGPU_WARN(...) TestLog(__VA_ARGS__)
#define REXGPU_INFO(...) TestLog(__VA_ARGS__)
namespace rex::ui::d3d12 {
struct D3D12Presenter {
  static constexpr DXGI_FORMAT kGuestOutputFormat=DXGI_FORMAT_R10G10B10A2_UNORM;
  static constexpr D3D12_RESOURCE_STATES kGuestOutputInternalState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};
struct Provider {
  ID3D12Device* device;
  ID3D12Device* GetDevice()const{return device;}
  D3D12_HEAP_FLAGS GetHeapFlagCreateNotZeroed()const{return D3D12_HEAP_FLAG_NONE;}
};
namespace util {
const D3D12_HEAP_PROPERTIES kHeapPropertiesDefault={D3D12_HEAP_TYPE_DEFAULT};
using DescriptorCpuGpuHandlePair=std::pair<D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_GPU_DESCRIPTOR_HANDLE>;
ID3D12RootSignature* CreateRootSignature(const Provider& provider,const D3D12_ROOT_SIGNATURE_DESC& desc){
  ComPtr<ID3DBlob> code,error;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error));
  ID3D12RootSignature* signature=nullptr;Check(provider.device->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&signature)));return signature;
}
ID3D12PipelineState* CreateComputePipeline(ID3D12Device* device,const void* code,size_t size,ID3D12RootSignature* signature){
  D3D12_COMPUTE_PIPELINE_STATE_DESC d={};d.pRootSignature=signature;d.CS={code,size};
  ID3D12PipelineState* pipeline=nullptr;Check(device->CreateComputePipelineState(&d,IID_PPV_ARGS(&pipeline)));return pipeline;
}
}
}
namespace rex::graphics::d3d12 {
namespace ui=rex::ui;
struct Commands {
  ID3D12GraphicsCommandList* list=nullptr;
  void D3DCopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* d,UINT x,UINT y,UINT z,const D3D12_TEXTURE_COPY_LOCATION* s,const D3D12_BOX* b){list->CopyTextureRegion(d,x,y,z,s,b);}
  void D3DCopyResource(ID3D12Resource* d,ID3D12Resource* s){list->CopyResource(d,s);}
  void D3DSetComputeRootSignature(ID3D12RootSignature* s){list->SetComputeRootSignature(s);}
  void D3DSetComputeRoot32BitConstants(UINT p,UINT n,const void* data,UINT offset){list->SetComputeRoot32BitConstants(p,n,data,offset);}
  void D3DSetComputeRootDescriptorTable(UINT p,D3D12_GPU_DESCRIPTOR_HANDLE h){list->SetComputeRootDescriptorTable(p,h);}
  void D3DDispatch(UINT x,UINT y,UINT z){list->Dispatch(x,y,z);}
};
struct D3D12CommandProcessor {
  ui::d3d12::Provider provider;
  Commands deferred_command_list_;
  ComPtr<ID3D12DescriptorHeap> heap;
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  uint64_t submission_current_=1,submission_completed_=0;
  std::vector<std::pair<uint64_t,ID3D12Resource*>> resources_for_deletion_;
  bool smaa_init_tried_=false,smaa_init_ok_=false;
  ComPtr<ID3D12RootSignature> smaa_root_signature_;
  ComPtr<ID3D12PipelineState> smaa_edges_pipeline_,smaa_weights_pipeline_,smaa_blend_pipeline_;
  ComPtr<ID3D12Resource> smaa_copy_,smaa_linear_,smaa_edges_,smaa_weights_,smaa_area_,smaa_search_;
  uint64_t smaa_textures_submission_=0;
  explicit D3D12CommandProcessor(ID3D12Device* device):provider{device}{}
  ~D3D12CommandProcessor(){for(auto& r:resources_for_deletion_)r.second->Release();}
  const ui::d3d12::Provider& GetD3D12Provider(){return provider;}
  void PushTransitionBarrier(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};barriers.push_back(b);
  }
  void SubmitBarriers(){if(!barriers.empty()){deferred_command_list_.list->ResourceBarrier(UINT(barriers.size()),barriers.data());barriers.clear();}}
  void SetExternalPipeline(ID3D12PipelineState* p){deferred_command_list_.list->SetPipelineState(p);}
  bool RequestOneUseSingleViewDescriptors(UINT count,ui::d3d12::util::DescriptorCpuGpuHandlePair* handles){
    D3D12_DESCRIPTOR_HEAP_DESC d={};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;d.NumDescriptors=count;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(provider.device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&heap)));ID3D12DescriptorHeap* h=heap.Get();deferred_command_list_.list->SetDescriptorHeaps(1,&h);
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto gpu=heap->GetGPUDescriptorHandleForHeapStart();UINT step=provider.device->GetDescriptorHandleIncrementSize(d.Type);
    for(UINT i=0;i<count;++i){handles[i]={cpu,gpu};cpu.ptr+=step;gpu.ptr+=step;}return true;
  }
  bool SmaaInitialize();void SmaaApply(ID3D12Resource*,uint32_t,uint32_t);static bool SmaaEnabled();
};
#include "../renderer-extension/backend.inc"
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
  D3D12_HEAP_PROPERTIES hp={};hp.Type=type;D3D12_RESOURCE_DESC d={};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> result;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&result)));return result;
}
int main(){try{
  bool debug=false;ComPtr<ID3D12Debug> debugger;
  if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugger)))){debugger->EnableDebugLayer();debug=true;}
  ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  ComPtr<IDXGIAdapter1> adapter;Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
  ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
  ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
  ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  rex::graphics::d3d12::D3D12CommandProcessor cp(device.Get());
  uint64_t fence_value=0;
  auto run=[&](UINT w,UINT h,bool enabled,bool diagonal){
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));cp.deferred_command_list_.list=list.Get();
    D3D12_RESOURCE_DESC td={};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R10G10B10A2_UNORM;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES hp={D3D12_HEAP_TYPE_DEFAULT};ComPtr<ID3D12Resource> image;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&image)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint={};UINT64 total=0;device->GetCopyableFootprints(&td,0,1,0,&footprint,nullptr,nullptr,&total);
    auto upload=Buffer(device.Get(),total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback=Buffer(device.Get(),total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<uint32_t> original(size_t(w)*h);uint8_t* mapped=nullptr;D3D12_RANGE no_read={0,0};Check(upload->Map(0,&no_read,reinterpret_cast<void**>(&mapped)));
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
      UINT r=320,g=650,b=100;
      if(diagonal)r=g=b=(float(y)>float(x)*0.61f+4.5f?900:80);
      const uint32_t value=r|(g<<10)|(b<<20)|(3u<<30);original[size_t(y)*w+x]=value;
      std::memcpy(mapped+size_t(y)*footprint.Footprint.RowPitch+x*4,&value,4);
    }upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION dest={};dest.pResource=image.Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION from={};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
    list->CopyTextureRegion(&dest,0,0,0,&from,nullptr);
    cp.PushTransitionBarrier(image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);cp.SubmitBarriers();
    rex::graphics::d3d12::g_smaa_enabled=enabled;
    cp.SmaaApply(image.Get(),w,h);cp.SubmitBarriers();
    if(enabled && !cp.smaa_init_ok_)throw std::runtime_error("SMAA initialization failed");
    cp.PushTransitionBarrier(image.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);cp.SubmitBarriers();
    D3D12_TEXTURE_COPY_LOCATION to={};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION source={};source.pResource=image.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to,0,0,0,&source,nullptr);Check(list->Close());ID3D12CommandList* commands=list.Get();queue->ExecuteCommandLists(1,&commands);
    Check(queue->Signal(fence.Get(),++fence_value));Check(fence->SetEventOnCompletion(fence_value,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU test timed out");
    cp.submission_completed_=cp.submission_current_++;
    D3D12_RANGE range={0,SIZE_T(total)};Check(readback->Map(0,&range,reinterpret_cast<void**>(&mapped)));
    size_t changed=0,intermediate=0,far_changed=0;
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){uint32_t value;std::memcpy(&value,mapped+size_t(y)*footprint.Footprint.RowPitch+x*4,4);auto before=original[size_t(y)*w+x];
      if(value!=before){++changed;if(std::abs(float(y)-float(x)*0.61f-4.5f)>4.0f)++far_changed;}
      auto r=value&1023;if(diagonal && r>80 && r<900)++intermediate;
      if((value>>30)!=3)throw std::runtime_error("Alpha changed unexpectedly");
    }readback->Unmap(0,nullptr);
    if(!diagonal || !enabled){if(changed)throw std::runtime_error("Flat/off pixels changed");}
    else if(!changed || !intermediate || far_changed)throw std::runtime_error("Diagonal smoothing / unchanged-background check failed");
    std::cout<<"PASS "<<w<<'x'<<h<<" enabled="<<enabled<<" diagonal="<<diagonal<<" changed="<<changed<<" intermediate="<<intermediate<<'\n';
  };
  run(64,64,false,false);run(64,64,true,false);run(257,131,true,true);run(129,67,false,true);run(129,67,true,false);
  if(debug){ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(device.As(&info))){for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T length=0;info->GetMessage(i,nullptr,&length);std::vector<uint8_t> bytes(length);auto message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());info->GetMessage(i,message,&length);if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)throw std::runtime_error(message->pDescription);}}}
  CloseHandle(event);std::cout<<"Native SMAA GPU validation passed; debug layer="<<debug<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
