// Native integration of iryoku SMAA 1x High. The shader/LUT headers retain
// their original MIT license. This file is included inside rex::graphics::d3d12.
#include "shader_source.h"
#include "AreaTex.h"
#include "SearchTex.h"

namespace {
std::atomic<bool> g_smaa_enabled{false};
std::atomic<int> g_smaa_status{0};  // -1 failed, 0 off, 1 requested, 2 applied
}

bool D3D12CommandProcessor::SmaaEnabled() {
  return g_smaa_enabled.load(std::memory_order_relaxed);
}

void D3D12CommandProcessor::SmaaResetWorkTextures() {
  smaa_copy_.Reset();
  smaa_linear_.Reset();
  smaa_edges_.Reset();
  smaa_weights_.Reset();
  smaa_textures_submission_ = 0;
}

bool D3D12CommandProcessor::SmaaInitialize() {
  using Compile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                   ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
  HMODULE compiler = GetModuleHandleW(L"d3dcompiler_47.dll");
  if (!compiler) compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  auto compile = compiler ? reinterpret_cast<Compile>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
  if (!compile) { REXGPU_ERROR("SMAA: shader compiler unavailable"); return false; }
  const auto& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  D3D12_DESCRIPTOR_RANGE ranges[5] = {};
  D3D12_ROOT_PARAMETER parameters[6] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.Num32BitValues = 4;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (UINT i = 0; i < 5; ++i) {
    ranges[i].RangeType = i < 2 ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[i].NumDescriptors = 1;
    ranges[i].BaseShaderRegister = i < 2 ? i : i - 2;
    parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i + 1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
    parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
  for (UINT i = 0; i < 2; ++i) {
    samplers[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[i].ShaderRegister = i;
    samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 6; desc.pParameters = parameters;
  desc.NumStaticSamplers = 2; desc.pStaticSamplers = samplers;
  smaa_root_signature_.Attach(ui::d3d12::util::CreateRootSignature(provider, desc));
  if (!smaa_root_signature_) { REXGPU_ERROR("SMAA: root signature creation failed"); return false; }
  auto build = [&](const char* entry, Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    HRESULT hr = compile(kSmaaShader, sizeof(kSmaaShader) - 1, "native_smaa", nullptr, nullptr,
                         entry, "cs_5_1", 1u << 15, 0, &code, &errors);
    if (errors) REXGPU_WARN("SMAA {}: {}", entry, std::string(
        static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    if (FAILED(hr) || !code) return false;
    pipeline.Attach(ui::d3d12::util::CreateComputePipeline(device, code->GetBufferPointer(),
        code->GetBufferSize(), smaa_root_signature_.Get()));
    return pipeline != nullptr;
  };
  if (!build("edges_cs", smaa_edges_pipeline_) || !build("weights_cs", smaa_weights_pipeline_) ||
      !build("blend_cs", smaa_blend_pipeline_)) {
    REXGPU_ERROR("SMAA: shader pipeline creation failed"); return false;
  }
  auto lut = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& texture, UINT width, UINT height,
                 DXGI_FORMAT format, const uint8_t* bytes, UINT source_pitch) {
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = width; td.Height = height;
    td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = format; td.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault,
        provider.GetHeapFlagCreateNotZeroed(), &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&texture)))) return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 total = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES upload_heap = {}; upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return false;
    void* mapped = nullptr; D3D12_RANGE no_read = {0, 0};
    if (FAILED(upload->Map(0, &no_read, &mapped))) return false;
    for (UINT y = 0; y < height; ++y) std::memcpy(
        static_cast<uint8_t*>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch,
        bytes + size_t(y) * source_pitch, source_pitch);
    upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
    deferred_command_list_.D3DCopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    PushTransitionBarrier(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    resources_for_deletion_.emplace_back(submission_current_, upload.Detach());
    return true;
  };
  if (!lut(smaa_area_, AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM,
           areaTexBytes, AREATEX_PITCH) ||
      !lut(smaa_search_, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM,
           searchTexBytes, SEARCHTEX_PITCH)) {
    REXGPU_ERROR("SMAA: lookup texture upload failed"); return false;
  }
  REXGPU_INFO("SMAA: native 1x High initialized (three passes, linear-space blending)");
  return true;
}

void D3D12CommandProcessor::SmaaApply(ID3D12Resource* output, uint32_t width, uint32_t height) {
  if (!SmaaEnabled() || !output || !width || !height) return;
  if (!smaa_init_tried_) {
    smaa_init_tried_ = true;
    smaa_init_ok_ = SmaaInitialize();
    if (!smaa_init_ok_) g_smaa_status.store(-1, std::memory_order_relaxed);
  }
  if (!smaa_init_ok_) return;
  const auto& provider = GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  auto retire = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& r) {
    if (!r) return;
    if (submission_completed_ < smaa_textures_submission_) {
      r->AddRef(); resources_for_deletion_.emplace_back(smaa_textures_submission_, r.Get());
    }
    r.Reset();
  };
  if (smaa_copy_ && (smaa_copy_->GetDesc().Width != width || smaa_copy_->GetDesc().Height != height)) {
    retire(smaa_copy_); retire(smaa_linear_); retire(smaa_edges_); retire(smaa_weights_);
  }
  auto create = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& r, DXGI_FORMAT format, bool uav) {
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = width; td.Height = height;
    td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = format; td.SampleDesc.Count = 1;
    td.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    return SUCCEEDED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault,
        provider.GetHeapFlagCreateNotZeroed(), &td, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        nullptr, IID_PPV_ARGS(&r)));
  };
  const auto color_format = ui::d3d12::D3D12Presenter::kGuestOutputFormat;
  if (!smaa_copy_) {
    if (!create(smaa_copy_, color_format, false) ||
        !create(smaa_linear_, DXGI_FORMAT_R16G16B16A16_FLOAT, true) ||
        !create(smaa_edges_, DXGI_FORMAT_R8G8B8A8_UNORM, true) ||
        !create(smaa_weights_, DXGI_FORMAT_R8G8B8A8_UNORM, true)) {
      REXGPU_ERROR("SMAA: work texture allocation failed");
      smaa_init_ok_ = false; g_smaa_status.store(-1, std::memory_order_relaxed); return;
    }
    REXGPU_INFO("SMAA: work textures {}x{}", width, height);
  }
  ui::d3d12::util::DescriptorCpuGpuHandlePair h[15];
  if (!RequestOneUseSingleViewDescriptors(15, h)) return;
  smaa_textures_submission_ = submission_current_;
  auto srv = [&](ID3D12Resource* r, DXGI_FORMAT format, int at) {
    D3D12_SHADER_RESOURCE_VIEW_DESC d = {}; d.Format = format;
    d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(r, &d, h[at].first);
  };
  auto uav = [&](ID3D12Resource* r, DXGI_FORMAT format, int at) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC d = {}; d.Format = format;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(r, nullptr, &d, h[at].first);
  };
  uav(smaa_edges_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 0);
  uav(smaa_linear_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
  srv(smaa_copy_.Get(), color_format, 2);
  srv(smaa_area_.Get(), DXGI_FORMAT_R8G8_UNORM, 3);
  srv(smaa_search_.Get(), DXGI_FORMAT_R8_UNORM, 4);
  uav(smaa_weights_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 5);
  uav(nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, 6);  // unused UAV in this shader
  srv(smaa_edges_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 7);
  srv(smaa_area_.Get(), DXGI_FORMAT_R8G8_UNORM, 8);
  srv(smaa_search_.Get(), DXGI_FORMAT_R8_UNORM, 9);
  uav(output, color_format, 10);
  uav(nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, 11);  // unused UAV in this shader
  srv(smaa_linear_.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 12);
  srv(smaa_weights_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 13);
  srv(smaa_copy_.Get(), color_format, 14);
  const auto out_state = ui::d3d12::D3D12Presenter::kGuestOutputInternalState;
  const auto read_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  const auto write_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  PushTransitionBarrier(output, out_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  PushTransitionBarrier(smaa_copy_.Get(), read_state, D3D12_RESOURCE_STATE_COPY_DEST);
  SubmitBarriers();
  deferred_command_list_.D3DCopyResource(smaa_copy_.Get(), output);
  PushTransitionBarrier(smaa_copy_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, read_state);
  PushTransitionBarrier(output, D3D12_RESOURCE_STATE_COPY_SOURCE, write_state);
  const float metrics[4] = {1.0f / width, 1.0f / height, float(width), float(height)};
  deferred_command_list_.D3DSetComputeRootSignature(smaa_root_signature_.Get());
  auto pass = [&](ID3D12PipelineState* pipeline, int first) {
    deferred_command_list_.D3DSetComputeRoot32BitConstants(0, 4, metrics, 0);
    for (int i = 0; i < 5; ++i)
      deferred_command_list_.D3DSetComputeRootDescriptorTable(1 + i, h[first + i].second);
    SetExternalPipeline(pipeline); SubmitBarriers();
    deferred_command_list_.D3DDispatch((width + 7) / 8, (height + 7) / 8, 1);
  };
  PushTransitionBarrier(smaa_edges_.Get(), read_state, write_state);
  PushTransitionBarrier(smaa_linear_.Get(), read_state, write_state);
  pass(smaa_edges_pipeline_.Get(), 0);
  PushTransitionBarrier(smaa_edges_.Get(), write_state, read_state);
  PushTransitionBarrier(smaa_linear_.Get(), write_state, read_state);
  PushTransitionBarrier(smaa_weights_.Get(), read_state, write_state);
  pass(smaa_weights_pipeline_.Get(), 5);
  PushTransitionBarrier(smaa_weights_.Get(), write_state, read_state);
  pass(smaa_blend_pipeline_.Get(), 10);
  PushTransitionBarrier(output, write_state, out_state);
  g_smaa_status.store(2, std::memory_order_relaxed);
}
