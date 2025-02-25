#include <iomanip>

#include "../util/util_time.h"

#include "dxvk_device.h"
#include "dxvk_graphics.h"
#include "dxvk_pipemanager.h"
#include "dxvk_spec_const.h"
#include "dxvk_state_cache.h"

namespace dxvk {

  DxvkGraphicsPipelineVertexInputState::DxvkGraphicsPipelineVertexInputState() {
    
  }


  DxvkGraphicsPipelineVertexInputState::DxvkGraphicsPipelineVertexInputState(
    const DxvkDevice*                     device,
    const DxvkGraphicsPipelineStateInfo&  state,
    const DxvkShader*                     vs) {
    std::array<uint32_t, MaxNumVertexBindings> viBindingMap = { };

    iaInfo.topology               = state.ia.primitiveTopology();
    iaInfo.primitiveRestartEnable = state.ia.primitiveRestart();

    uint32_t attrMask = vs->info().inputMask;
    uint32_t bindingMask = 0;

    // Find out which bindings are used based on the attribute mask
    for (uint32_t i = 0; i < state.il.attributeCount(); i++) {
      if (attrMask & (1u << state.ilAttributes[i].location()))
        bindingMask |= 1u << state.ilAttributes[i].binding();
    }

    // Process vertex bindings. We will compact binding numbers on
    // the fly so that vertex buffers can be updated more easily.
    uint32_t bindingCount = 0;

    for (uint32_t i = 0; i < state.il.bindingCount(); i++) {
      uint32_t bindingIndex = state.ilBindings[i].binding();

      if (bindingMask & (1u << bindingIndex)) {
        viBindingMap[bindingIndex] = i;

        VkVertexInputBindingDescription& binding = viBindings[bindingCount++];
        binding.binding = i;
        binding.stride = state.ilBindings[i].stride();
        binding.inputRate = state.ilBindings[i].inputRate();

        if (state.ilBindings[i].inputRate() == VK_VERTEX_INPUT_RATE_INSTANCE
         && state.ilBindings[i].divisor()   != 1) {
          VkVertexInputBindingDivisorDescriptionEXT& divisor = viDivisors[viDivisorInfo.vertexBindingDivisorCount++];
          divisor.binding = i;
          divisor.divisor = state.ilBindings[i].divisor();
        }
      }
    }

    if (bindingCount) {
      bool supportsDivisor = device->features().extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor;

      viInfo.vertexBindingDescriptionCount = bindingCount;
      viInfo.pVertexBindingDescriptions = viBindings.data();

      if (viDivisorInfo.vertexBindingDivisorCount && supportsDivisor) {
        viDivisorInfo.pVertexBindingDivisors = viDivisors.data();
        viInfo.pNext = &viDivisorInfo;
      }
    }

    // Process vertex attributes, filtering out unused ones
    uint32_t attrCount = 0;

    for (uint32_t i = 0; i < state.il.attributeCount(); i++) {
      if (attrMask & (1u << state.ilAttributes[i].location())) {
        VkVertexInputAttributeDescription& attr = viAttributes[attrCount++];
        attr.location = state.ilAttributes[i].location();
        attr.binding = viBindingMap[state.ilAttributes[i].binding()];
        attr.format = state.ilAttributes[i].format();
        attr.offset = state.ilAttributes[i].offset();
      }
    }

    if (attrCount) {
      viInfo.vertexAttributeDescriptionCount = attrCount;
      viInfo.pVertexAttributeDescriptions = viAttributes.data();
    }

    // We need to be consistent with the pipeline state vector since
    // the normalized state may otherwise change beavhiour here.
    viUseDynamicVertexStrides = state.useDynamicVertexStrides();
  }


  bool DxvkGraphicsPipelineVertexInputState::eq(const DxvkGraphicsPipelineVertexInputState& other) const {
    bool eq = iaInfo.topology                         == other.iaInfo.topology
           && iaInfo.primitiveRestartEnable           == other.iaInfo.primitiveRestartEnable
           && viInfo.vertexBindingDescriptionCount    == other.viInfo.vertexBindingDescriptionCount
           && viInfo.vertexAttributeDescriptionCount  == other.viInfo.vertexAttributeDescriptionCount
           && viDivisorInfo.vertexBindingDivisorCount == other.viDivisorInfo.vertexBindingDivisorCount
           && viUseDynamicVertexStrides               == other.viUseDynamicVertexStrides;

    for (uint32_t i = 0; i < viInfo.vertexBindingDescriptionCount && eq; i++) {
      const auto& a = viBindings[i];
      const auto& b = other.viBindings[i];

      eq = a.binding    == b.binding
        && a.stride     == b.stride
        && a.inputRate  == b.inputRate;
    }

    for (uint32_t i = 0; i < viInfo.vertexAttributeDescriptionCount && eq; i++) {
      const auto& a = viAttributes[i];
      const auto& b = other.viAttributes[i];

      eq = a.location   == b.location
        && a.binding    == b.binding
        && a.format     == b.format
        && a.offset     == b.offset;
    }

    for (uint32_t i = 0; i < viDivisorInfo.vertexBindingDivisorCount; i++) {
      const auto& a = viDivisors[i];
      const auto& b = other.viDivisors[i];

      eq = a.binding    == b.binding
        && a.divisor    == b.divisor;
    }

    return eq;
  }


  size_t DxvkGraphicsPipelineVertexInputState::hash() const {
    DxvkHashState hash;
    hash.add(uint32_t(iaInfo.topology));
    hash.add(uint32_t(iaInfo.primitiveRestartEnable));
    hash.add(uint32_t(viInfo.vertexBindingDescriptionCount));
    hash.add(uint32_t(viInfo.vertexAttributeDescriptionCount));
    hash.add(uint32_t(viDivisorInfo.vertexBindingDivisorCount));
    hash.add(uint32_t(viUseDynamicVertexStrides));

    for (uint32_t i = 0; i < viInfo.vertexBindingDescriptionCount; i++) {
      hash.add(uint32_t(viBindings[i].binding));
      hash.add(uint32_t(viBindings[i].stride));
      hash.add(uint32_t(viBindings[i].inputRate));
    }

    for (uint32_t i = 0; i < viInfo.vertexAttributeDescriptionCount; i++) {
      hash.add(uint32_t(viAttributes[i].location));
      hash.add(uint32_t(viAttributes[i].binding));
      hash.add(uint32_t(viAttributes[i].format));
      hash.add(uint32_t(viAttributes[i].offset));
    }

    for (uint32_t i = 0; i < viDivisorInfo.vertexBindingDivisorCount; i++) {
      hash.add(uint32_t(viDivisors[i].binding));
      hash.add(uint32_t(viDivisors[i].divisor));
    }

    return hash;
  }


  DxvkGraphicsPipelineVertexInputLibrary::DxvkGraphicsPipelineVertexInputLibrary(
          DxvkDevice*                           device,
    const DxvkGraphicsPipelineVertexInputState& state)
  : m_device(device) {
    auto vk = m_device->vkd();

    VkDynamicState dynamicState = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
    VkPipelineDynamicStateCreateInfo dyInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };

    if (state.viUseDynamicVertexStrides) {
      dyInfo.dynamicStateCount = 1;
      dyInfo.pDynamicStates = &dynamicState;
    }

    VkGraphicsPipelineLibraryCreateInfoEXT libInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT };
    libInfo.flags             = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &libInfo };
    info.flags                = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
    info.pVertexInputState    = &state.viInfo;
    info.pInputAssemblyState  = &state.iaInfo;
    info.pDynamicState        = &dyInfo;
    info.basePipelineIndex    = -1;

    VkResult vr = vk->vkCreateGraphicsPipelines(vk->device(),
      VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);

    if (vr)
      throw DxvkError("Failed to create vertex input pipeline library");
  }


  DxvkGraphicsPipelineVertexInputLibrary::~DxvkGraphicsPipelineVertexInputLibrary() {
    auto vk = m_device->vkd();

    vk->vkDestroyPipeline(vk->device(), m_pipeline, nullptr);
  }


  DxvkGraphicsPipelineFragmentOutputState::DxvkGraphicsPipelineFragmentOutputState() {

  }


  DxvkGraphicsPipelineFragmentOutputState::DxvkGraphicsPipelineFragmentOutputState(
    const DxvkDevice*                     device,
    const DxvkGraphicsPipelineStateInfo&  state,
    const DxvkShader*                     fs) {
    // Set up color formats and attachment blend states. Disable the write
    // mask for any attachment that the fragment shader does not write to.
    uint32_t fsOutputMask = fs ? fs->info().outputMask : 0u;

    // Dual-source blending can only write to one render target
    if (state.useDualSourceBlending())
      fsOutputMask &= 0x1;

    const VkColorComponentFlags rgbaWriteMask
      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    cbInfo.logicOpEnable  = state.om.enableLogicOp();
    cbInfo.logicOp        = state.om.logicOp();

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      rtColorFormats[i] = state.rt.getColorFormat(i);

      if (rtColorFormats[i]) {
        rtInfo.colorAttachmentCount = i + 1;

        auto formatInfo = lookupFormatInfo(rtColorFormats[i]);

        if ((fsOutputMask & (1 << i)) && formatInfo) {
          VkColorComponentFlags writeMask = state.omBlend[i].colorWriteMask();

          if (writeMask != rgbaWriteMask) {
            writeMask = util::remapComponentMask(
              state.omBlend[i].colorWriteMask(), state.omSwizzle[i].mapping());
          }

          writeMask &= formatInfo->componentMask;

          if (writeMask == formatInfo->componentMask)
            writeMask = rgbaWriteMask;

          if (writeMask) {
            cbAttachments[i] = state.omBlend[i].state();
            cbAttachments[i].colorWriteMask = writeMask;

            // If we're rendering to an emulated alpha-only render target, fix up blending
            if (cbAttachments[i].blendEnable && formatInfo->componentMask == VK_COLOR_COMPONENT_R_BIT && state.omSwizzle[i].rIndex() == 3) {
              cbAttachments[i].srcColorBlendFactor = util::remapAlphaToColorBlendFactor(
                std::exchange(cbAttachments[i].srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE));
              cbAttachments[i].dstColorBlendFactor = util::remapAlphaToColorBlendFactor(
                std::exchange(cbAttachments[i].dstAlphaBlendFactor, VK_BLEND_FACTOR_ZERO));
              cbAttachments[i].colorBlendOp =
                std::exchange(cbAttachments[i].alphaBlendOp, VK_BLEND_OP_ADD);
            }
          }
        }
      }
    }

    if (rtInfo.colorAttachmentCount) {
      rtInfo.pColorAttachmentFormats = rtColorFormats.data();

      cbInfo.attachmentCount = rtInfo.colorAttachmentCount;
      cbInfo.pAttachments = cbAttachments.data();
    }

    // Set up depth-stencil format accordingly.
    VkFormat rtDepthFormat = state.rt.getDepthStencilFormat();

    if (rtDepthFormat) {
      auto rtDepthFormatInfo = lookupFormatInfo(rtDepthFormat);

      if (rtDepthFormatInfo->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
        rtInfo.depthAttachmentFormat = rtDepthFormat;

      if (rtDepthFormatInfo->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
        rtInfo.stencilAttachmentFormat = rtDepthFormat;
    }

    // Set up multisample state based on shader info as well
    // as rasterization state and render target sample counts.
    if (state.ms.sampleCount())
      msInfo.rasterizationSamples = VkSampleCountFlagBits(state.ms.sampleCount());
    else if (state.rs.sampleCount())
      msInfo.rasterizationSamples = VkSampleCountFlagBits(state.rs.sampleCount());
    else
      msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    if (fs && fs->flags().test(DxvkShaderFlag::HasSampleRateShading)) {
      msInfo.sampleShadingEnable  = VK_TRUE;
      msInfo.minSampleShading     = 1.0f;
    }

    msSampleMask                  = state.ms.sampleMask() & ((1u << msInfo.rasterizationSamples) - 1);
    msInfo.pSampleMask            = &msSampleMask;
    msInfo.alphaToCoverageEnable  = state.ms.enableAlphaToCoverage();

    // We need to be fully consistent with the pipeline state here, and
    // while we could consistently infer it, just don't take any chances
    cbUseDynamicBlendConstants = state.useDynamicBlendConstants();
  }


  bool DxvkGraphicsPipelineFragmentOutputState::eq(const DxvkGraphicsPipelineFragmentOutputState& other) const {
    bool eq = rtInfo.colorAttachmentCount     == other.rtInfo.colorAttachmentCount
           && rtInfo.depthAttachmentFormat    == other.rtInfo.depthAttachmentFormat
           && rtInfo.stencilAttachmentFormat  == other.rtInfo.stencilAttachmentFormat
           && cbInfo.logicOpEnable            == other.cbInfo.logicOpEnable
           && cbInfo.logicOp                  == other.cbInfo.logicOp
           && cbInfo.attachmentCount          == other.cbInfo.attachmentCount
           && msInfo.rasterizationSamples     == other.msInfo.rasterizationSamples
           && msInfo.sampleShadingEnable      == other.msInfo.sampleShadingEnable
           && msInfo.minSampleShading         == other.msInfo.minSampleShading
           && msInfo.alphaToCoverageEnable    == other.msInfo.alphaToCoverageEnable
           && msInfo.alphaToOneEnable         == other.msInfo.alphaToOneEnable
           && msSampleMask                    == other.msSampleMask
           && cbUseDynamicBlendConstants      == other.cbUseDynamicBlendConstants;

    for (uint32_t i = 0; i < rtInfo.colorAttachmentCount && eq; i++)
      eq = rtColorFormats[i] == other.rtColorFormats[i];

    for (uint32_t i = 0; i < cbInfo.attachmentCount && eq; i++) {
      const auto& a = cbAttachments[i];
      const auto& b = other.cbAttachments[i];

      eq = a.blendEnable    == b.blendEnable
        && a.colorWriteMask == b.colorWriteMask;

      if (a.blendEnable && eq) {
        eq = a.srcColorBlendFactor == b.srcColorBlendFactor
          && a.dstColorBlendFactor == b.dstColorBlendFactor
          && a.colorBlendOp        == b.colorBlendOp
          && a.srcAlphaBlendFactor == b.srcAlphaBlendFactor
          && a.dstAlphaBlendFactor == b.dstAlphaBlendFactor
          && a.alphaBlendOp        == b.alphaBlendOp;
      }
    }

    return eq;
  }


  size_t DxvkGraphicsPipelineFragmentOutputState::hash() const {
    DxvkHashState hash;
    hash.add(uint32_t(rtInfo.colorAttachmentCount));
    hash.add(uint32_t(rtInfo.depthAttachmentFormat));
    hash.add(uint32_t(rtInfo.stencilAttachmentFormat));
    hash.add(uint32_t(cbInfo.logicOpEnable));
    hash.add(uint32_t(cbInfo.logicOp));
    hash.add(uint32_t(cbInfo.attachmentCount));
    hash.add(uint32_t(msInfo.rasterizationSamples));
    hash.add(uint32_t(msInfo.alphaToCoverageEnable));
    hash.add(uint32_t(msInfo.alphaToOneEnable));
    hash.add(uint32_t(msSampleMask));
    hash.add(uint32_t(cbUseDynamicBlendConstants));

    for (uint32_t i = 0; i < rtInfo.colorAttachmentCount; i++)
      hash.add(uint32_t(rtColorFormats[i]));

    for (uint32_t i = 0; i < cbInfo.attachmentCount; i++) {
      hash.add(uint32_t(cbAttachments[i].blendEnable));
      hash.add(uint32_t(cbAttachments[i].colorWriteMask));

      if (cbAttachments[i].blendEnable) {
        hash.add(uint32_t(cbAttachments[i].srcColorBlendFactor));
        hash.add(uint32_t(cbAttachments[i].dstColorBlendFactor));
        hash.add(uint32_t(cbAttachments[i].colorBlendOp));
        hash.add(uint32_t(cbAttachments[i].srcAlphaBlendFactor));
        hash.add(uint32_t(cbAttachments[i].dstAlphaBlendFactor));
        hash.add(uint32_t(cbAttachments[i].alphaBlendOp));
      }
    }

    return hash;
  }


  DxvkGraphicsPipelineFragmentOutputLibrary::DxvkGraphicsPipelineFragmentOutputLibrary(
          DxvkDevice*                               device,
    const DxvkGraphicsPipelineFragmentOutputState&  state)
  : m_device(device) {
    auto vk = m_device->vkd();

    VkDynamicState dynamicState = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
    VkPipelineDynamicStateCreateInfo dyInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };

    if (state.cbUseDynamicBlendConstants) {
      dyInfo.dynamicStateCount  = 1;
      dyInfo.pDynamicStates     = &dynamicState;
    }

    // pNext is non-const for some reason, but this is only an input
    // structure, so we should be able to safely use const_cast.
    VkGraphicsPipelineLibraryCreateInfoEXT libInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT };
    libInfo.pNext             = const_cast<VkPipelineRenderingCreateInfo*>(&state.rtInfo);
    libInfo.flags             = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &libInfo };
    info.flags                = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
    info.pColorBlendState     = &state.cbInfo;
    info.pMultisampleState    = &state.msInfo;
    info.pDynamicState        = &dyInfo;
    info.basePipelineIndex    = -1;

    VkResult vr = vk->vkCreateGraphicsPipelines(vk->device(),
      VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);

    if (vr)
      throw DxvkError("Failed to create vertex input pipeline library");
  }


  DxvkGraphicsPipelineFragmentOutputLibrary::~DxvkGraphicsPipelineFragmentOutputLibrary() {
    auto vk = m_device->vkd();

    vk->vkDestroyPipeline(vk->device(), m_pipeline, nullptr);
  }


  DxvkGraphicsPipelinePreRasterizationState::DxvkGraphicsPipelinePreRasterizationState() {
    
  }


  DxvkGraphicsPipelinePreRasterizationState::DxvkGraphicsPipelinePreRasterizationState(
    const DxvkDevice*                     device,
    const DxvkGraphicsPipelineStateInfo&  state,
    const DxvkShader*                     gs) {
    // Set up tessellation state
    tsInfo.patchControlPoints = state.ia.patchVertexCount();
    
    // Set up basic rasterization state
    rsInfo.depthClampEnable         = VK_TRUE;
    rsInfo.polygonMode              = state.rs.polygonMode();
    rsInfo.depthBiasEnable          = state.rs.depthBiasEnable();
    rsInfo.lineWidth                = 1.0f;

    // Set up rasterized stream depending on geometry shader state.
    // Rasterizing stream 0 is default behaviour in all situations.
    int32_t streamIndex = gs ? gs->info().xfbRasterizedStream : 0;

    if (streamIndex > 0) {
      rsXfbStreamInfo.pNext = std::exchange(rsInfo.pNext, &rsXfbStreamInfo);
      rsXfbStreamInfo.rasterizationStream = uint32_t(streamIndex);
    } else if (streamIndex < 0) {
      rsInfo.rasterizerDiscardEnable = VK_TRUE;
    }

    // Set up depth clip state. If the extension is not supported,
    // use depth clamp instead, even though this is not accurate.
    if (device->features().extDepthClipEnable.depthClipEnable) {
      rsDepthClipInfo.pNext = std::exchange(rsInfo.pNext, &rsDepthClipInfo);
      rsDepthClipInfo.depthClipEnable = state.rs.depthClipEnable();
    } else {
      rsInfo.depthClampEnable = !state.rs.depthClipEnable();
    }

    // Set up conservative rasterization if requested by the application.
    if (state.rs.conservativeMode() != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
      rsConservativeInfo.pNext = std::exchange(rsInfo.pNext, &rsConservativeInfo);
      rsConservativeInfo.conservativeRasterizationMode = state.rs.conservativeMode();
      rsConservativeInfo.extraPrimitiveOverestimationSize = 0.0f;
    }
  }


  DxvkGraphicsPipelineFragmentShaderState::DxvkGraphicsPipelineFragmentShaderState() {

  }


  DxvkGraphicsPipelineFragmentShaderState::DxvkGraphicsPipelineFragmentShaderState(
    const DxvkDevice*                     device,
    const DxvkGraphicsPipelineStateInfo&  state) {
    VkImageAspectFlags dsReadOnlyAspects = state.rt.getDepthStencilReadOnlyAspects();

    bool enableDepthWrites = !(dsReadOnlyAspects & VK_IMAGE_ASPECT_DEPTH_BIT);
    bool enableStencilWrites = !(dsReadOnlyAspects & VK_IMAGE_ASPECT_STENCIL_BIT);

    dsInfo.depthTestEnable        = state.ds.enableDepthTest();
    dsInfo.depthWriteEnable       = state.ds.enableDepthWrite() && enableDepthWrites;
    dsInfo.depthCompareOp         = state.ds.depthCompareOp();
    dsInfo.depthBoundsTestEnable  = state.ds.enableDepthBoundsTest();
    dsInfo.stencilTestEnable      = state.ds.enableStencilTest();
    dsInfo.front                  = state.dsFront.state(enableStencilWrites);
    dsInfo.back                   = state.dsBack.state(enableStencilWrites);
  }


  DxvkGraphicsPipeline::DxvkGraphicsPipeline(
          DxvkDevice*                 device,
          DxvkPipelineManager*        pipeMgr,
          DxvkGraphicsPipelineShaders shaders,
          DxvkBindingLayoutObjects*   layout,
          DxvkShaderPipelineLibrary*  vsLibrary,
          DxvkShaderPipelineLibrary*  fsLibrary)
  : m_device        (device),
    m_manager       (pipeMgr),
    m_workers       (&pipeMgr->m_workers),
    m_stateCache    (&pipeMgr->m_stateCache),
    m_stats         (&pipeMgr->m_stats),
    m_shaders       (std::move(shaders)),
    m_bindings      (layout),
    m_barrier       (layout->getGlobalBarrier()),
    m_vsLibrary     (vsLibrary),
    m_fsLibrary     (fsLibrary) {
    m_vsIn  = m_shaders.vs != nullptr ? m_shaders.vs->info().inputMask  : 0;
    m_fsOut = m_shaders.fs != nullptr ? m_shaders.fs->info().outputMask : 0;

    if (m_shaders.gs != nullptr) {
      if (m_shaders.gs->flags().test(DxvkShaderFlag::HasTransformFeedback)) {
        m_flags.set(DxvkGraphicsPipelineFlag::HasTransformFeedback);

        m_barrier.stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
                         |  VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
        m_barrier.access |= VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT
                         |  VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT
                         |  VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
      }

      if (m_shaders.gs->info().xfbRasterizedStream < 0)
        m_flags.set(DxvkGraphicsPipelineFlag::HasRasterizerDiscard);
    }
    
    if (m_barrier.access & VK_ACCESS_SHADER_WRITE_BIT)
      m_flags.set(DxvkGraphicsPipelineFlag::HasStorageDescriptors);
  }
  
  
  DxvkGraphicsPipeline::~DxvkGraphicsPipeline() {
    for (const auto& instance : m_pipelines)
      this->destroyPipeline(instance.fastHandle.load());

    for (const auto& instance : m_basePipelines)
      this->destroyPipeline(instance.handle);
  }
  
  
  DxvkGlobalPipelineBarrier DxvkGraphicsPipeline::getGlobalBarrier(
    const DxvkGraphicsPipelineStateInfo&    state) const {
    DxvkGlobalPipelineBarrier barrier = m_barrier;

    if (state.il.bindingCount()) {
      barrier.stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
      barrier.access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }

    return barrier;
  }


  std::pair<VkPipeline, DxvkGraphicsPipelineType> DxvkGraphicsPipeline::getPipelineHandle(
    const DxvkGraphicsPipelineStateInfo& state) {
    DxvkGraphicsPipelineInstance* instance = this->findInstance(state);

    if (unlikely(!instance)) {
      // Exit early if the state vector is invalid
      if (!this->validatePipelineState(state, true))
        return std::make_pair(VK_NULL_HANDLE, DxvkGraphicsPipelineType::FastPipeline);

      // Prevent other threads from adding new instances and check again
      std::lock_guard<dxvk::mutex> lock(m_mutex);
      instance = this->findInstance(state);

      if (!instance) {
        // Keep pipeline object locked, at worst we're going to stall
        // a state cache worker and the current thread needs priority.
        bool canCreateBasePipeline = this->canCreateBasePipeline(state);
        instance = this->createInstance(state, canCreateBasePipeline);

        // If necessary, compile an optimized pipeline variant
        if (!instance->fastHandle.load())
          m_workers->compileGraphicsPipeline(this, state);

        // Only store pipelines in the state cache that cannot benefit
        // from pipeline libraries, or if that feature is disabled.
        if (!canCreateBasePipeline)
          this->writePipelineStateToCache(state);
      }
    }

    // Find a pipeline handle to use. If no optimized pipeline has
    // been compiled yet, use the slower base pipeline instead.
    VkPipeline fastHandle = instance->fastHandle.load();

    if (likely(fastHandle != VK_NULL_HANDLE))
      return std::make_pair(fastHandle, DxvkGraphicsPipelineType::FastPipeline);

    return std::make_pair(instance->baseHandle.load(), DxvkGraphicsPipelineType::BasePipeline);
  }


  void DxvkGraphicsPipeline::compilePipeline(
    const DxvkGraphicsPipelineStateInfo& state) {
    if (m_device->config().enableGraphicsPipelineLibrary == Tristate::True)
      return;

    // Try to find an existing instance that contains a base pipeline
    DxvkGraphicsPipelineInstance* instance = this->findInstance(state);

    if (!instance) {
      // Exit early if the state vector is invalid
      if (!this->validatePipelineState(state, false))
        return;

      // Do not compile if this pipeline can be fast linked. This essentially
      // disables the state cache for pipelines that do not benefit from it.
      if (this->canCreateBasePipeline(state))
        return;

      // Prevent other threads from adding new instances and check again
      std::lock_guard<dxvk::mutex> lock(m_mutex);
      instance = this->findInstance(state);

      if (!instance)
        instance = this->createInstance(state, false);
    }

    // Exit if another thread is already compiling
    // an optimized version of this pipeline
    if (instance->isCompiling.load()
     || instance->isCompiling.exchange(VK_TRUE, std::memory_order_acquire))
      return;

    VkPipeline pipeline = this->createOptimizedPipeline(state, 0);
    instance->fastHandle.store(pipeline, std::memory_order_release);

    // Log pipeline state on error
    if (!pipeline)
      this->logPipelineState(LogLevel::Error, state);
  }


  DxvkGraphicsPipelineInstance* DxvkGraphicsPipeline::createInstance(
    const DxvkGraphicsPipelineStateInfo& state,
          bool                           doCreateBasePipeline) {
    VkPipeline baseHandle = VK_NULL_HANDLE;
    VkPipeline fastHandle = VK_NULL_HANDLE;

    if (doCreateBasePipeline) {
      // Try to create an optimized pipeline from the cache
      // first, since this is expected to be the fastest path.
      if (m_device->canUsePipelineCacheControl()) {
        fastHandle = this->createOptimizedPipeline(state,
          VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT);
      }

      if (!fastHandle) {
        // If that didn't succeed, link a pipeline using the
        // pre-compiled fragment and vertex shader libraries.
        DxvkGraphicsPipelineVertexInputState    viState(m_device, state, m_shaders.vs.ptr());
        DxvkGraphicsPipelineFragmentOutputState foState(m_device, state, m_shaders.fs.ptr());

        DxvkGraphicsPipelineBaseInstanceKey key;
        key.viLibrary = m_manager->createVertexInputLibrary(viState);
        key.foLibrary = m_manager->createFragmentOutputLibrary(foState);
        key.args.depthClipEnable = state.rs.depthClipEnable();

        baseHandle = this->createBaseInstance(key)->handle;
      }
    } else {
      // Create optimized variant right away, no choice
      fastHandle = this->createOptimizedPipeline(state, 0);
    }

    // Log pipeline state if requested, or on failure
    if (!fastHandle && !baseHandle)
      this->logPipelineState(LogLevel::Error, state);

    m_stats->numGraphicsPipelines += 1;
    return &(*m_pipelines.emplace(state, baseHandle, fastHandle));
  }
  
  
  DxvkGraphicsPipelineInstance* DxvkGraphicsPipeline::findInstance(
    const DxvkGraphicsPipelineStateInfo& state) {
    for (auto& instance : m_pipelines) {
      if (instance.state == state)
        return &instance;
    }
    
    return nullptr;
  }
  
  
  DxvkGraphicsPipelineBaseInstance* DxvkGraphicsPipeline::createBaseInstance(
    const DxvkGraphicsPipelineBaseInstanceKey& key) {
    for (auto& instance : m_basePipelines) {
      if (instance.key.viLibrary == key.viLibrary
       && instance.key.foLibrary == key.foLibrary
       && instance.key.args == key.args)
        return &instance;
    }

    VkPipeline handle = createBasePipeline(key);
    return &(*m_basePipelines.emplace(key, handle));
  }


  bool DxvkGraphicsPipeline::canCreateBasePipeline(
    const DxvkGraphicsPipelineStateInfo& state) const {
    if (!m_vsLibrary || !m_fsLibrary)
      return false;

    // Certain rasterization states cannot be set dynamically,
    // so we're assuming defaults for them, most notably the
    // polygon mode and conservative rasterization settings
    if (state.rs.polygonMode() != VK_POLYGON_MODE_FILL
     || state.rs.conservativeMode() != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT)
      return false;

    if (m_shaders.fs != nullptr) {
      // If the fragment shader has inputs not produced by the
      // vertex shader, we need to patch the fragment shader
      uint32_t vsIoMask = m_shaders.vs->info().outputMask;
      uint32_t fsIoMask = m_shaders.fs->info().inputMask;

      if ((vsIoMask & fsIoMask) != fsIoMask)
        return false;

      // Dual-source blending requires patching the fragment shader
      if (state.useDualSourceBlending())
        return false;

      // Multisample state must match in this case, and the
      // library assumes that MSAA is disabled in this case.
      if (m_shaders.fs->flags().test(DxvkShaderFlag::HasSampleRateShading)) {
        if (state.ms.sampleCount() != VK_SAMPLE_COUNT_1_BIT
         || state.ms.sampleMask() == 0
         || state.ms.enableAlphaToCoverage())
          return false;
      }
    }

    // Remapping fragment shader outputs would require spec constants
    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      if (writesRenderTarget(state, i) && !util::isIdentityMapping(state.omSwizzle[i].mapping()))
        return false;
    }

    return true;
  }


  VkPipeline DxvkGraphicsPipeline::createBasePipeline(
    const DxvkGraphicsPipelineBaseInstanceKey& key) const {
    auto vk = m_device->vkd();

    std::array<VkPipeline, 4> libraries = {{
      key.viLibrary->getHandle(),
      m_vsLibrary->getPipelineHandle(key.args),
      m_fsLibrary->getPipelineHandle(key.args),
      key.foLibrary->getHandle(),
    }};

		VkPipelineLibraryCreateInfoKHR libInfo = { VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR };
		libInfo.libraryCount    = libraries.size();
		libInfo.pLibraries      = libraries.data();

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &libInfo };
    info.layout             = m_bindings->getPipelineLayout(true);
    info.basePipelineIndex  = -1;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult vr = vk->vkCreateGraphicsPipelines(vk->device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);

    if (vr != VK_SUCCESS)
      Logger::err(str::format("DxvkGraphicsPipeline: Failed to create base pipeline: ", vr));

    return pipeline;
  }

  
  VkPipeline DxvkGraphicsPipeline::createOptimizedPipeline(
    const DxvkGraphicsPipelineStateInfo& state,
          VkPipelineCreateFlags          flags) const {
    auto vk = m_device->vkd();

    // Set up dynamic states as needed
    std::array<VkDynamicState, 9> dynamicStates;
    uint32_t                      dynamicStateCount = 0;
    
    dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;
    dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;

    if (state.useDynamicVertexStrides())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;

    if (state.useDynamicDepthBias())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
    
    if (state.useDynamicDepthBounds())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
    
    if (state.useDynamicBlendConstants())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
    
    if (state.useDynamicStencilRef())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;

    if (!m_flags.test(DxvkGraphicsPipelineFlag::HasRasterizerDiscard)) {
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_CULL_MODE;
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_FRONT_FACE;
    }

    // Set up some specialization constants
    DxvkSpecConstants specData;

    for (uint32_t i = 0; i < MaxNumSpecConstants; i++)
      specData.set(i, state.sc.specConstants[i], 0u);
    
    VkSpecializationInfo specInfo = specData.getSpecInfo();

    // Build stage infos for all provided shaders
    DxvkShaderStageInfo stageInfo(m_device);

    if (flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
      stageInfo.addStage(VK_SHADER_STAGE_VERTEX_BIT, m_vsLibrary->getModuleIdentifier(), &specInfo);

      if (m_shaders.fs != nullptr)
        stageInfo.addStage(VK_SHADER_STAGE_FRAGMENT_BIT, m_fsLibrary->getModuleIdentifier(), &specInfo);
    } else {
      stageInfo.addStage(VK_SHADER_STAGE_VERTEX_BIT, getShaderCode(m_shaders.vs, state), &specInfo);

      if (m_shaders.tcs != nullptr)
        stageInfo.addStage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, getShaderCode(m_shaders.tcs, state), &specInfo);
      if (m_shaders.tes != nullptr)
        stageInfo.addStage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, getShaderCode(m_shaders.tes, state), &specInfo);
      if (m_shaders.gs != nullptr)
        stageInfo.addStage(VK_SHADER_STAGE_GEOMETRY_BIT, getShaderCode(m_shaders.gs, state), &specInfo);
      if (m_shaders.fs != nullptr)
        stageInfo.addStage(VK_SHADER_STAGE_FRAGMENT_BIT, getShaderCode(m_shaders.fs, state), &specInfo);
    }

    DxvkGraphicsPipelineVertexInputState      viState(m_device, state, m_shaders.vs.ptr());
    DxvkGraphicsPipelinePreRasterizationState prState(m_device, state, m_shaders.gs.ptr());
    DxvkGraphicsPipelineFragmentShaderState   fsState(m_device, state);
    DxvkGraphicsPipelineFragmentOutputState   foState(m_device, state, m_shaders.fs.ptr());

    VkPipelineDynamicStateCreateInfo dyInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyInfo.dynamicStateCount      = dynamicStateCount;
    dyInfo.pDynamicStates         = dynamicStates.data();

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &foState.rtInfo };
    info.flags                    = flags;
    info.stageCount               = stageInfo.getStageCount();
    info.pStages                  = stageInfo.getStageInfos();
    info.pVertexInputState        = &viState.viInfo;
    info.pInputAssemblyState      = &viState.iaInfo;
    info.pTessellationState       = &prState.tsInfo;
    info.pViewportState           = &prState.vpInfo;
    info.pRasterizationState      = &prState.rsInfo;
    info.pMultisampleState        = &foState.msInfo;
    info.pDepthStencilState       = &fsState.dsInfo;
    info.pColorBlendState         = &foState.cbInfo;
    info.pDynamicState            = &dyInfo;
    info.layout                   = m_bindings->getPipelineLayout(false);
    info.basePipelineIndex        = -1;
    
    if (!prState.tsInfo.patchControlPoints)
      info.pTessellationState = nullptr;
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult vr = vk->vkCreateGraphicsPipelines(vk->device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);

    if (vr != VK_SUCCESS) {
      // Ignore any error if we're trying to create a cached pipeline. If linking or
      // compiling an optimized pipeline fail later, we'll still be printing errors.
      if (!(flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT))
        Logger::err(str::format("DxvkGraphicsPipeline: Failed to compile pipeline: ", vr));

      return VK_NULL_HANDLE;
    }
    
    return pipeline;
  }
  
  
  void DxvkGraphicsPipeline::destroyPipeline(VkPipeline pipeline) const {
    auto vk = m_device->vkd();

    vk->vkDestroyPipeline(vk->device(), pipeline, nullptr);
  }


  SpirvCodeBuffer DxvkGraphicsPipeline::getShaderCode(
    const Rc<DxvkShader>&                shader,
    const DxvkGraphicsPipelineStateInfo& state) const {
    auto vk = m_device->vkd();

    const DxvkShaderCreateInfo& shaderInfo = shader->info();
    DxvkShaderModuleCreateInfo info;

    // Fix up fragment shader outputs for dual-source blending
    if (shaderInfo.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
      info.fsDualSrcBlend = state.useDualSourceBlending();

      for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
        if (writesRenderTarget(state, i))
          info.rtSwizzles[i] = state.omSwizzle[i].mapping();
      }
    }

    // Deal with undefined shader inputs
    uint32_t consumedInputs = shaderInfo.inputMask;
    uint32_t providedInputs = 0;

    if (shaderInfo.stage == VK_SHADER_STAGE_VERTEX_BIT) {
      for (uint32_t i = 0; i < state.il.attributeCount(); i++)
        providedInputs |= 1u << state.ilAttributes[i].location();
    } else if (shaderInfo.stage != VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
      auto prevStage = getPrevStageShader(shaderInfo.stage);
      providedInputs = prevStage->info().outputMask;
    } else {
      // Technically not correct, but this
      // would need a lot of extra care
      providedInputs = consumedInputs;
    }

    info.undefinedInputs = (providedInputs & consumedInputs) ^ consumedInputs;
    return shader->getCode(m_bindings, info);
  }


  Rc<DxvkShader> DxvkGraphicsPipeline::getPrevStageShader(VkShaderStageFlagBits stage) const {
    if (stage == VK_SHADER_STAGE_VERTEX_BIT)
      return nullptr;

    if (stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
      return m_shaders.tcs;

    Rc<DxvkShader> result = m_shaders.vs;

    if (stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
      return result;

    if (m_shaders.tes != nullptr)
      result = m_shaders.tes;

    if (stage == VK_SHADER_STAGE_GEOMETRY_BIT)
      return result;

    if (m_shaders.gs != nullptr)
      result = m_shaders.gs;

    return result;
  }


  bool DxvkGraphicsPipeline::writesRenderTarget(
    const DxvkGraphicsPipelineStateInfo& state,
          uint32_t                       target) const {
    if (!(m_fsOut & (1u << target)))
      return false;

    if (!state.omBlend[target].colorWriteMask())
      return false;

    VkFormat rtFormat = state.rt.getColorFormat(target);
    return rtFormat != VK_FORMAT_UNDEFINED;
  }


  bool DxvkGraphicsPipeline::validatePipelineState(
    const DxvkGraphicsPipelineStateInfo&  state,
          bool                            trusted) const {
    // Tessellation shaders and patches must be used together
    bool hasPatches = state.ia.primitiveTopology() == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

    bool hasTcs = m_shaders.tcs != nullptr;
    bool hasTes = m_shaders.tes != nullptr;

    if (hasPatches != hasTcs || hasPatches != hasTes)
      return false;
    
    // Filter out undefined primitive topologies
    if (state.ia.primitiveTopology() == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)
      return false;
    
    // Prevent unintended out-of-bounds access to the IL arrays
    if (state.il.attributeCount() > DxvkLimits::MaxNumVertexAttributes
     || state.il.bindingCount()   > DxvkLimits::MaxNumVertexBindings)
      return false;

    // Exit here on the fast path, perform more thorough validation if
    // the state vector comes from an untrusted source (i.e. the cache)
    if (trusted)
      return true;

    // Validate shaders
    if (!m_shaders.validate()) {
      Logger::err("Invalid pipeline: Shader types do not match stage");
      return false;
    }

    // Validate vertex input layout
    uint32_t ilLocationMask = 0;
    uint32_t ilBindingMask = 0;

    for (uint32_t i = 0; i < state.il.bindingCount(); i++)
      ilBindingMask |= 1u << state.ilBindings[i].binding();

    for (uint32_t i = 0; i < state.il.attributeCount(); i++) {
      const DxvkIlAttribute& attribute = state.ilAttributes[i];

      if (ilLocationMask & (1u << attribute.location())) {
        Logger::err(str::format("Invalid pipeline: Vertex location ", attribute.location(), " defined twice"));
        return false;
      }

      if (!(ilBindingMask & (1u << attribute.binding()))) {
        Logger::err(str::format("Invalid pipeline: Vertex binding ", attribute.binding(), " not defined"));
        return false;
      }

      VkFormatProperties formatInfo = m_device->adapter()->formatProperties(attribute.format());

      if (!(formatInfo.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
        Logger::err(str::format("Invalid pipeline: Format ", attribute.format(), " not supported for vertex buffers"));
        return false;
      }

      ilLocationMask |= 1u << attribute.location();
    }

    // Validate rasterization state
    if (state.rs.conservativeMode() != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
      if (!m_device->extensions().extConservativeRasterization) {
        Logger::err("Conservative rasterization not supported by device");
        return false;
      }

      if (state.rs.conservativeMode() == VK_CONSERVATIVE_RASTERIZATION_MODE_UNDERESTIMATE_EXT
       && !m_device->properties().extConservativeRasterization.primitiveUnderestimation) {
        Logger::err("Primitive underestimation not supported by device");
        return false;
      }
    }

    // Validate depth-stencil state
    if (state.ds.enableDepthBoundsTest() && !m_device->features().core.features.depthBounds) {
      Logger::err("Depth bounds not supported by device");
      return false;
    }

    // Validate render target format support
    VkFormat depthFormat = state.rt.getDepthStencilFormat();

    if (depthFormat) {
      VkFormatProperties formatInfo = m_device->adapter()->formatProperties(depthFormat);

      if (!(formatInfo.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        Logger::err(str::format(depthFormat, " not supported as depth-stencil attachment"));
        return false;
      }
    }

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      VkFormat colorFormat = state.rt.getColorFormat(i);

      if (colorFormat) {
        VkFormatProperties formatInfo = m_device->adapter()->formatProperties(colorFormat);

        if (!(formatInfo.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
          Logger::err(str::format(depthFormat, " not supported as color attachment"));
          return false;
        }
      }
    }

    return true;
  }
  
  
  void DxvkGraphicsPipeline::writePipelineStateToCache(
    const DxvkGraphicsPipelineStateInfo& state) const {
    DxvkStateCacheKey key;
    if (m_shaders.vs  != nullptr) key.vs = m_shaders.vs->getShaderKey();
    if (m_shaders.tcs != nullptr) key.tcs = m_shaders.tcs->getShaderKey();
    if (m_shaders.tes != nullptr) key.tes = m_shaders.tes->getShaderKey();
    if (m_shaders.gs  != nullptr) key.gs = m_shaders.gs->getShaderKey();
    if (m_shaders.fs  != nullptr) key.fs = m_shaders.fs->getShaderKey();

    m_stateCache->addGraphicsPipeline(key, state);
  }
  
  
  void DxvkGraphicsPipeline::logPipelineState(
          LogLevel                       level,
    const DxvkGraphicsPipelineStateInfo& state) const {
    std::stringstream sstr;
    sstr << "Shader stages:" << std::endl;
    if (m_shaders.vs  != nullptr) sstr << "  vs  : " << m_shaders.vs ->debugName() << std::endl;
    if (m_shaders.tcs != nullptr) sstr << "  tcs : " << m_shaders.tcs->debugName() << std::endl;
    if (m_shaders.tes != nullptr) sstr << "  tes : " << m_shaders.tes->debugName() << std::endl;
    if (m_shaders.gs  != nullptr) sstr << "  gs  : " << m_shaders.gs ->debugName() << std::endl;
    if (m_shaders.fs  != nullptr) sstr << "  fs  : " << m_shaders.fs ->debugName() << std::endl;

    // Log input assembly state
    VkPrimitiveTopology topology = state.ia.primitiveTopology();
    sstr << std::dec << "Primitive topology: " << topology;

    if (topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST)
      sstr << " [" << state.ia.patchVertexCount() << "]" << std::endl;
    else
      sstr << " [restart: " << (state.ia.primitiveRestart() ? "yes]" : "no]") << std::endl;

    // Log vertex input state
    for (uint32_t i = 0; i < state.il.bindingCount(); i++) {
      const auto& binding = state.ilBindings[i];
      sstr << "Vertex binding " << binding.binding() << " [" << binding.stride() << "]" << std::endl;

      for (uint32_t j = 0; j < state.il.attributeCount(); j++) {
        const auto& attribute = state.ilAttributes[j];

        if (attribute.binding() == binding.binding())
          sstr << "  " << attribute.location() << " [" << attribute.offset() << "]: " << attribute.format() << std::endl;
      }
    }

    // Log rasterizer state
    sstr << "Rasterizer state:" << std::endl
         << "  depth clip:      " << (state.rs.depthClipEnable() ? "yes" : "no") << std::endl
         << "  depth bias:      " << (state.rs.depthBiasEnable() ? "yes" : "no") << std::endl
         << "  polygon mode:    " << state.rs.polygonMode() << std::endl
         << "  conservative:    " << (state.rs.conservativeMode() == VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT ? "no" : "yes") << std::endl;

    // Log multisample state
    VkSampleCountFlags sampleCount = VK_SAMPLE_COUNT_1_BIT;

    if (state.ms.sampleCount())
      sampleCount = state.ms.sampleCount();
    else if (state.rs.sampleCount())
      sampleCount = state.rs.sampleCount();

    sstr << "Sample count: " << sampleCount << " [0x" << std::hex << state.ms.sampleMask() << std::dec << "]" << std::endl
         << "  alphaToCoverage: " << (state.ms.enableAlphaToCoverage() ? "yes" : "no") << std::endl;

    // Log depth-stencil state
    sstr << "Depth test:        ";

    if (state.ds.enableDepthTest())
      sstr << "yes [write: " << (state.ds.enableDepthWrite() ? "yes" : "no") << ", op: " << state.ds.depthCompareOp() << "]" << std::endl;
    else
      sstr << "no" << std::endl;

    sstr << "Depth bounds test: " << (state.ds.enableDepthBoundsTest() ? "yes" : "no") << std::endl
         << "Stencil test:      " << (state.ds.enableStencilTest() ? "yes" : "no") << std::endl;

    if (state.ds.enableStencilTest()) {
      std::array<VkStencilOpState, 2> states = {{
        state.dsFront.state(true),
        state.dsBack.state(true),
      }};

      for (size_t i = 0; i < states.size(); i++) {
        sstr << std::hex << (i ? "  back:  " : "  front: ")
             << "[c=0x" << states[i].compareMask << ",w=0x" << states[i].writeMask << ",op=" << states[i].compareOp << "] "
             << "fail=" << states[i].failOp << ",pass=" << states[i].passOp << ",depthFail=" << states[i].depthFailOp << std::dec << std::endl;
      }
    }

    // Log logic op state
    sstr << "Logic op:          ";

    if (state.om.enableLogicOp())
      sstr << "yes [" << state.om.logicOp() << "]" << std::endl;
    else
      sstr << "no" << std::endl;

    // Log render target and blend state
    auto depthFormat = state.rt.getDepthStencilFormat();
    auto depthFormatInfo = lookupFormatInfo(depthFormat);

    VkImageAspectFlags writableAspects = depthFormat
      ? (depthFormatInfo->aspectMask & ~state.rt.getDepthStencilReadOnlyAspects())
      : 0u;

    sstr << "Depth attachment: " << depthFormat;

    if (depthFormat) {
      sstr << " ["
        << ((writableAspects & VK_IMAGE_ASPECT_DEPTH_BIT) ? "d" : " ")
        << ((writableAspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? "s" : " ")
        << "]" << std::endl;
    } else {
      sstr << std::endl;
    }

    bool hasColorAttachments = false;

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      auto format = state.rt.getColorFormat(i);

      if (format) {
        if (!hasColorAttachments) {
          sstr << "Color attachments:" << std::endl;
          hasColorAttachments = true;
        }

        const char* components = "rgba";
        const auto& blend = state.omBlend[i];
        const auto& swizzle = state.omSwizzle[i];

        VkColorComponentFlags writeMask = blend.colorWriteMask();
        char r = (writeMask & (1u << swizzle.rIndex())) ? components[swizzle.rIndex()] : ' ';
        char g = (writeMask & (1u << swizzle.gIndex())) ? components[swizzle.gIndex()] : ' ';
        char b = (writeMask & (1u << swizzle.bIndex())) ? components[swizzle.bIndex()] : ' ';
        char a = (writeMask & (1u << swizzle.aIndex())) ? components[swizzle.aIndex()] : ' ';

        sstr << "  " << i << ": " << format << " [" << r << g << b << a << "] blend: ";

        if (blend.blendEnable())
          sstr << "yes (c:" << blend.srcColorBlendFactor() << "," << blend.dstColorBlendFactor() << "," << blend.colorBlendOp()
               <<     ";a:" << blend.srcAlphaBlendFactor() << "," << blend.dstAlphaBlendFactor() << "," << blend.alphaBlendOp() << ")" << std::endl;
        else
          sstr << "no" << std::endl;
      }
    }

    // Log spec constants
    bool hasSpecConstants = false;

    for (uint32_t i = 0; i < MaxNumSpecConstants; i++) {
      if (state.sc.specConstants[i]) {
        if (!hasSpecConstants) {
          sstr << "Specialization constants:" << std::endl;
          hasSpecConstants = true;
        }

        sstr << "  " << i << ": 0x" << std::hex << std::setw(8) << std::setfill('0') << state.sc.specConstants[i] << std::dec << std::endl;
      }
    }

    Logger::log(level, sstr.str());
  }
  
}
