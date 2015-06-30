#include "GLImageProcUtils.h"

GLImageProcAlgo::GLImageProcAlgo( int nLevels, int nLayers, int nComputeStages,
                                  int nOutputType, int nDebugType, int nInputType,
                                  bool bUseOutputPBOs, bool bUseDebugPBOs, bool bUseInputPBOs,
                                  bool bUseTexArrays, bool bUseDisplay, bool bUseTimers, bool bUseIntegralFormat)
    :    m_nLevels(nLevels)
        ,m_nLayers(nLayers)
        ,m_nSxSDisplayCount(int(nOutputType>=0)+int(nDebugType>=0)+int(nInputType>=0))
        ,m_nComputeStages(nComputeStages)
        ,m_nOutputType(nOutputType)
        ,m_nDebugType(nDebugType)
        ,m_nInputType(nInputType)
        ,m_bUsingOutputPBOs(bUseOutputPBOs&&nOutputType>=0)
        ,m_bUsingDebugPBOs(bUseDebugPBOs&&nDebugType>=0)
        ,m_bUsingInputPBOs(bUseInputPBOs&&nInputType>=0)
        ,m_bUsingOutput(nOutputType>=0)
        ,m_bUsingDebug(nDebugType>=0)
        ,m_bUsingInput(nInputType>=0)
        ,m_bUsingTexArrays(bUseTexArrays&&nLevels==1)
        ,m_bUsingTimers(bUseTimers)
        ,m_bUsingIntegralFormat(bUseIntegralFormat)
        ,m_vDefaultWorkGroupSize(GLUTILS_IMGPROC_DEFAULT_WORKGROUP)
        ,m_bUsingDisplay(bUseDisplay)
        ,m_bGLInitialized(false)
        ,m_nInternalFrameIdx(-1)
        ,m_nLastOutputInternalIdx(-1)
        ,m_nLastDebugInternalIdx(-1)
        ,m_bFetchingOutput(false)
        ,m_bFetchingDebug(false)
        ,m_nNextLayer(1)
        ,m_nCurrLayer(0)
        ,m_nLastLayer(nLayers-1)
        ,m_nCurrPBO(0)
        ,m_nNextPBO(1) {
    glAssert(m_nLevels>0 && m_nLayers>1);
    if(m_bUsingTexArrays && !glGetTextureSubImage && (m_bUsingDebugPBOs || m_bUsingOutputPBOs))
        glError("missing impl for texture arrays pbo fetch when glGetTextureSubImage is not available");
    int nMaxComputeInvocs;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,&nMaxComputeInvocs);
    const int nCurrComputeStageInvocs = m_vDefaultWorkGroupSize.x*m_vDefaultWorkGroupSize.y;
    glAssert(nCurrComputeStageInvocs>0 && nCurrComputeStageInvocs<nMaxComputeInvocs);
    GLint nMaxImageUnits;
    glGetIntegerv(GL_MAX_IMAGE_UNITS,&nMaxImageUnits);
    if(nMaxImageUnits<GLImageProcAlgo::eImageBindingsCount)
        glError("image units limit is too small for the current impl");
    GLint nMaxTextureUnits;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS,&nMaxTextureUnits);
    if(nMaxTextureUnits<GLImageProcAlgo::eTextureBindingsCount)
        glError("texture units limit is too small for the current impl");
    if(m_bUsingTimers)
        glGenQueries(GLImageProcAlgo::eGLTimersCount,m_nGLTimers);
    glGenBuffers(GLImageProcAlgo::eBufferBindingsCount,m_anSSBO);
}

GLImageProcAlgo::~GLImageProcAlgo() {
    if(m_bUsingTimers)
        glDeleteQueries(GLImageProcAlgo::eGLTimersCount,m_nGLTimers);
    glDeleteBuffers(GLImageProcAlgo::eBufferBindingsCount,m_anSSBO);
}

std::string GLImageProcAlgo::getVertexShaderSource() const {
    return GLShader::getPassThroughVertexShaderSource(false,false,true);
}

std::string GLImageProcAlgo::getFragmentShaderSource() const {
    return getFragmentShaderSource_internal(m_nLayers,m_nOutputType,m_nDebugType,m_nInputType,m_bUsingOutput,m_bUsingDebug,m_bUsingInput,m_bUsingTexArrays,m_bUsingIntegralFormat);
}

void GLImageProcAlgo::initialize(const cv::Mat& oInitInput, const cv::Mat& oROI) {
    glAssert(!oROI.empty() && oROI.isContinuous() && oROI.type()==CV_8UC1);
    glAssert(oInitInput.type()==m_nInputType && oInitInput.size()==oROI.size() && oInitInput.isContinuous());
    m_bGLInitialized = false;
    m_oFrameSize = oROI.size();
    for(int nPBOIter=0; nPBOIter<2; ++nPBOIter) {
        if(m_bUsingOutputPBOs)
            m_apOutputPBOs[nPBOIter] = std::unique_ptr<GLPixelBufferObject>(new GLPixelBufferObject(cv::Mat(m_oFrameSize,m_nOutputType),GL_PIXEL_PACK_BUFFER,GL_STREAM_READ));
        if(m_bUsingDebugPBOs)
            m_apDebugPBOs[nPBOIter] = std::unique_ptr<GLPixelBufferObject>(new GLPixelBufferObject(cv::Mat(m_oFrameSize,m_nDebugType),GL_PIXEL_PACK_BUFFER,GL_STREAM_READ));
        if(m_bUsingInputPBOs)
            m_apInputPBOs[nPBOIter] = std::unique_ptr<GLPixelBufferObject>(new GLPixelBufferObject(oInitInput,GL_PIXEL_UNPACK_BUFFER,GL_STREAM_DRAW));
    }
    if(m_bUsingTexArrays) {
        if(m_bUsingOutput) {
            m_pOutputArray = std::unique_ptr<GLDynamicTexture2DArray>(new GLDynamicTexture2DArray(1,std::vector<cv::Mat>(m_nLayers,cv::Mat(m_oFrameSize,m_nOutputType)),m_bUsingIntegralFormat));
            m_pOutputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_OutputBinding);
        }
        if(m_bUsingDebug) {
            m_pDebugArray = std::unique_ptr<GLDynamicTexture2DArray>(new GLDynamicTexture2DArray(1,std::vector<cv::Mat>(m_nLayers,cv::Mat(m_oFrameSize,m_nDebugType)),m_bUsingIntegralFormat));
            m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
        }
        if(m_bUsingInput) {
            m_pInputArray = std::unique_ptr<GLDynamicTexture2DArray>(new GLDynamicTexture2DArray(1,std::vector<cv::Mat>(m_nLayers,cv::Mat(m_oFrameSize,m_nInputType)),m_bUsingIntegralFormat));
            m_pInputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_InputBinding);
            if(m_bUsingInputPBOs) {
                m_pInputArray->updateTexture(*m_apInputPBOs[m_nCurrPBO],m_nCurrLayer,true);
                m_pInputArray->updateTexture(*m_apInputPBOs[m_nCurrPBO],m_nNextLayer,true);
            }
            else {
                m_pInputArray->updateTexture(oInitInput,m_nCurrLayer,true);
                m_pInputArray->updateTexture(oInitInput,m_nNextLayer,true);
            }
        }
    }
    else {
        m_vpOutputArray.resize(m_nLayers);
        m_vpInputArray.resize(m_nLayers);
        m_vpDebugArray.resize(m_nLayers);
        for(int nLayerIter=0; nLayerIter<m_nLayers; ++nLayerIter) {
            if(m_bUsingOutput) {
                m_vpOutputArray[nLayerIter] = std::unique_ptr<GLDynamicTexture2D>(new GLDynamicTexture2D(1,cv::Mat(m_oFrameSize,m_nOutputType),m_bUsingIntegralFormat));
                m_vpOutputArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_OutputBinding);
            }
            if(m_bUsingDebug) {
                m_vpDebugArray[nLayerIter] = std::unique_ptr<GLDynamicTexture2D>(new GLDynamicTexture2D(1,cv::Mat(m_oFrameSize,m_nDebugType),m_bUsingIntegralFormat));
                m_vpDebugArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
            }
            if(m_bUsingInput) {
                m_vpInputArray[nLayerIter] = std::unique_ptr<GLDynamicTexture2D>(new GLDynamicTexture2D(m_nLevels,cv::Mat(m_oFrameSize,m_nInputType),m_bUsingIntegralFormat));
                m_vpInputArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_InputBinding);
                if(m_bUsingInputPBOs) {
                    if(nLayerIter==m_nCurrLayer)
                        m_vpInputArray[m_nCurrLayer]->updateTexture(*m_apInputPBOs[m_nCurrPBO],true);
                    else if(nLayerIter==m_nNextLayer)
                        m_vpInputArray[m_nNextLayer]->updateTexture(*m_apInputPBOs[m_nCurrPBO],true);
                }
                else {
                    if(nLayerIter==m_nCurrLayer)
                        m_vpInputArray[m_nCurrLayer]->updateTexture(oInitInput,true);
                    else if(nLayerIter==m_nNextLayer)
                        m_vpInputArray[m_nNextLayer]->updateTexture(oInitInput,true);
                }
            }
        }
    }
    m_pROITexture = std::unique_ptr<GLTexture2D>(new GLTexture2D(1,oROI,m_bUsingIntegralFormat));
    m_pROITexture->bindToImage(GLImageProcAlgo::eImage_ROIBinding,0,GL_READ_ONLY);
    if(!m_bUsingOutputPBOs && m_bUsingOutput)
        m_oLastOutput = cv::Mat(m_oFrameSize,m_nOutputType);
    if(!m_bUsingDebugPBOs && m_bUsingDebug)
        m_oLastDebug = cv::Mat(m_oFrameSize,m_nDebugType);
    m_vpImgProcShaders.resize(m_nComputeStages);
    for(int nCurrStageIter=0; nCurrStageIter<m_nComputeStages; ++nCurrStageIter) {
        m_vpImgProcShaders[nCurrStageIter] = std::unique_ptr<GLShader>(new GLShader());
        m_vpImgProcShaders[nCurrStageIter]->addSource(getComputeShaderSource(nCurrStageIter),GL_COMPUTE_SHADER);
        if(!m_vpImgProcShaders[nCurrStageIter]->link())
            glError("Could not link image processing shader");
    }
    m_oDisplayShader.clear();
    m_oDisplayShader.addSource(this->getVertexShaderSource(),GL_VERTEX_SHADER);
    m_oDisplayShader.addSource(this->getFragmentShaderSource(),GL_FRAGMENT_SHADER);
    if(!m_oDisplayShader.link())
        glError("Could not link display shader");
    glErrorCheck;
    m_nInternalFrameIdx = 0;
    m_bGLInitialized = true;
}

void GLImageProcAlgo::apply(const cv::Mat& oNextInput, bool bRebindAll) {
    glAssert(m_bGLInitialized && (oNextInput.empty() || (oNextInput.type()==m_nInputType && oNextInput.size()==m_oFrameSize && oNextInput.isContinuous())));
    m_nLastLayer = m_nCurrLayer;
    m_nCurrLayer = m_nNextLayer;
    ++m_nNextLayer %= m_nLayers;
    m_nCurrPBO = m_nNextPBO;
    ++m_nNextPBO %= 2;
    if(bRebindAll) {
        for(int nBufferBindingIter=0; nBufferBindingIter<eBufferBindingsCount; ++nBufferBindingIter) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,m_anSSBO[nBufferBindingIter]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,(GLImageProcAlgo::eBufferBindingList)nBufferBindingIter,m_anSSBO[nBufferBindingIter]);
        }
    }
    if(m_bUsingTimers)
        glBeginQuery(GL_TIME_ELAPSED,m_nGLTimers[GLImageProcAlgo::eGLTimer_TextureUpdate]);
    if(m_bUsingInputPBOs && !oNextInput.empty())
        m_apInputPBOs[m_nNextPBO]->updateBuffer(oNextInput,false,bRebindAll);
    if(m_bUsingTexArrays) {
        if(m_bUsingOutput) {
            if(bRebindAll)
                m_pOutputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_OutputBinding);
            m_pOutputArray->bindToImage(GLImageProcAlgo::eImage_OutputBinding,0,m_nCurrLayer,GL_READ_WRITE);
        }
        if(m_bUsingDebug) {
            if(bRebindAll)
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
            m_pDebugArray->bindToImage(GLImageProcAlgo::eImage_DebugBinding,0,m_nCurrLayer,GL_WRITE_ONLY);
        }
        if(m_bUsingInput) {
            if(bRebindAll || !m_bUsingInputPBOs) {
                m_pInputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_InputBinding);
                if(!m_bUsingInputPBOs && !oNextInput.empty())
                    m_pInputArray->updateTexture(oNextInput,m_nNextLayer,bRebindAll);
            }
            m_pInputArray->bindToImage(GLImageProcAlgo::eImage_InputBinding,0,m_nCurrLayer,GL_READ_ONLY);
        }
    }
    else {
        for(int nLayerIter=0; nLayerIter<m_nLayers; ++nLayerIter) {
            if(bRebindAll) {
                if(m_bUsingOutput)
                    m_vpOutputArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_OutputBinding);
                if(m_bUsingDebug)
                    m_vpDebugArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
                if(m_bUsingInput)
                    m_vpInputArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_InputBinding);
            }
            if(nLayerIter==m_nNextLayer && !m_bUsingInputPBOs) {
                if(!bRebindAll)
                    m_vpInputArray[m_nNextLayer]->bindToSampler((m_nNextLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_InputBinding);
                if(!oNextInput.empty())
                    m_vpInputArray[m_nNextLayer]->updateTexture(oNextInput,bRebindAll);
            }
            else if(nLayerIter==m_nCurrLayer) {
                if(m_bUsingOutput)
                    m_vpOutputArray[m_nCurrLayer]->bindToImage(GLImageProcAlgo::eImage_OutputBinding,0,GL_READ_WRITE);
                if(m_bUsingDebug)
                    m_vpDebugArray[m_nCurrLayer]->bindToImage(GLImageProcAlgo::eImage_DebugBinding,0,GL_WRITE_ONLY);
                if(m_bUsingInput)
                    m_vpInputArray[m_nCurrLayer]->bindToImage(GLImageProcAlgo::eImage_InputBinding,0,GL_READ_ONLY);
            }
        }
    }
    if(bRebindAll)
        m_pROITexture->bindToImage(GLImageProcAlgo::eImage_ROIBinding,0,GL_READ_ONLY);
    if(m_bUsingTimers) {
        glEndQuery(GL_TIME_ELAPSED);
        glBeginQuery(GL_TIME_ELAPSED,m_nGLTimers[GLImageProcAlgo::eGLTimer_ComputeDispatch]);
    }
    for(int nCurrStageIter=0; nCurrStageIter<m_nComputeStages; ++nCurrStageIter) {
        glAssert(m_vpImgProcShaders[nCurrStageIter]->activate());
        m_vpImgProcShaders[nCurrStageIter]->setUniform1ui(getCurrTextureLayerUniformName(),m_nCurrLayer);
        m_vpImgProcShaders[nCurrStageIter]->setUniform1ui(getLastTextureLayerUniformName(),m_nLastLayer);
        m_vpImgProcShaders[nCurrStageIter]->setUniform1ui(getFrameIndexUniformName(),m_nInternalFrameIdx);
        dispatch(nCurrStageIter,*m_vpImgProcShaders[nCurrStageIter]); // add timer for stage? can reuse the same @@@@@@@@@@@@@@@
    }
    if(m_bUsingTimers)
        glEndQuery(GL_TIME_ELAPSED);
    if(m_bUsingInputPBOs) {
        if(m_bUsingTexArrays) {
            m_pInputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_InputBinding);
            m_pInputArray->updateTexture(*m_apInputPBOs[m_nNextPBO],m_nNextLayer,bRebindAll);
        }
        else {
            m_vpInputArray[m_nNextLayer]->bindToSampler((m_nNextLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_InputBinding);
            m_vpInputArray[m_nNextLayer]->updateTexture(*m_apInputPBOs[m_nNextPBO],bRebindAll);
        }
    }
    if(m_bFetchingDebug) {
        m_nLastDebugInternalIdx = m_nInternalFrameIdx;
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
        if(m_bUsingDebugPBOs) {
            if(m_bUsingTexArrays) {
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
                m_pDebugArray->fetchTexture(*m_apDebugPBOs[m_nNextPBO],m_nCurrLayer,bRebindAll);
            }
            else {
                m_vpDebugArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
                m_vpDebugArray[m_nCurrLayer]->fetchTexture(*m_apDebugPBOs[m_nNextPBO],bRebindAll);
            }
        }
        else {
            if(m_bUsingTexArrays) {
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
                m_pDebugArray->fetchTexture(m_oLastDebug,m_nCurrLayer);
            }
            else {
                m_vpDebugArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
                m_vpDebugArray[m_nCurrLayer]->fetchTexture(m_oLastDebug);
            }
        }
    }
    if(m_bFetchingOutput) {
        m_nLastOutputInternalIdx = m_nInternalFrameIdx;
        if(!m_bFetchingDebug)
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
        if(m_bUsingOutputPBOs) {
            if(m_bUsingTexArrays) {
                m_pOutputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_OutputBinding);
                m_pOutputArray->fetchTexture(*m_apOutputPBOs[m_nNextPBO],m_nCurrLayer,bRebindAll);
            }
            else {
                m_vpOutputArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_OutputBinding);
                m_vpOutputArray[m_nCurrLayer]->fetchTexture(*m_apOutputPBOs[m_nNextPBO],bRebindAll);
            }
        }
        else {
            if(m_bUsingTexArrays) {
                m_pOutputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_OutputBinding);
                m_pOutputArray->fetchTexture(m_oLastOutput,m_nCurrLayer);
            }
            else {
                m_vpOutputArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_OutputBinding);
                m_vpOutputArray[m_nCurrLayer]->fetchTexture(m_oLastOutput);
            }
        }
    }
    if(m_bUsingDisplay) {
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        if(m_bUsingDebug) {
            if(m_bUsingTexArrays)
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
            else
                m_vpDebugArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
        }
        if(m_bUsingOutput) {
            if(m_bUsingTexArrays)
                m_pOutputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_OutputBinding);
            else
                m_vpOutputArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_OutputBinding);
        }
        if(m_bUsingTimers)
            glBeginQuery(GL_TIME_ELAPSED,m_nGLTimers[GLImageProcAlgo::eGLTimer_DisplayUpdate]);
        glAssert(m_oDisplayShader.activate());
        m_oDisplayShader.setUniform1ui(getCurrTextureLayerUniformName(),m_nCurrLayer);
        m_oDisplayBillboard.render();
        if(m_bUsingTimers)
            glEndQuery(GL_TIME_ELAPSED);
    }
    if(m_bUsingTimers) {
        GLuint64 nGLTimerValTot = 0;
        std::cout << "\t\tGPU: ";
        glGetQueryObjectui64v(m_nGLTimers[GLImageProcAlgo::eGLTimer_TextureUpdate],GL_QUERY_RESULT,&m_nGLTimerVals[GLImageProcAlgo::eGLTimer_TextureUpdate]);
        std::cout << "TextureUpdate=" << m_nGLTimerVals[GLImageProcAlgo::eGLTimer_TextureUpdate]*1.e-6 << "ms,  ";
        nGLTimerValTot += m_nGLTimerVals[GLImageProcAlgo::eGLTimer_TextureUpdate];
        glGetQueryObjectui64v(m_nGLTimers[GLImageProcAlgo::eGLTimer_ComputeDispatch],GL_QUERY_RESULT,&m_nGLTimerVals[GLImageProcAlgo::eGLTimer_ComputeDispatch]);
        std::cout << "ComputeDispatch=" << m_nGLTimerVals[GLImageProcAlgo::eGLTimer_ComputeDispatch]*1.e-6 << "ms,  ";
        nGLTimerValTot += m_nGLTimerVals[GLImageProcAlgo::eGLTimer_ComputeDispatch];
        if(m_bUsingDisplay) {
            glGetQueryObjectui64v(m_nGLTimers[GLImageProcAlgo::eGLTimer_DisplayUpdate],GL_QUERY_RESULT,&m_nGLTimerVals[GLImageProcAlgo::eGLTimer_DisplayUpdate]);
            std::cout << "DisplayUpdate=" << m_nGLTimerVals[GLImageProcAlgo::eGLTimer_DisplayUpdate]*1.e-6 << "ms,  ";
            nGLTimerValTot += m_nGLTimerVals[GLImageProcAlgo::eGLTimer_DisplayUpdate];
        }
        std::cout << " tot=" << nGLTimerValTot*1.e-6 << "ms" << std::endl;
    }
    ++m_nInternalFrameIdx;
}

int GLImageProcAlgo::fetchLastOutput(cv::Mat& oOutput) const {
    glAssert(oOutput.size()==m_oFrameSize && oOutput.type()==m_nOutputType && oOutput.isContinuous());
    glAssert(m_bFetchingOutput);
    if(m_bUsingOutputPBOs)
        m_apOutputPBOs[m_nNextPBO]->fetchBuffer(oOutput,true);
    else
        m_oLastOutput.copyTo(oOutput);
    return m_nLastOutputInternalIdx;
}

int GLImageProcAlgo::fetchLastDebug(cv::Mat& oDebug) const {
    glAssert(oDebug.size()==m_oFrameSize && oDebug.type()==m_nDebugType && oDebug.isContinuous());
    glAssert(m_bFetchingDebug);
    if(m_bUsingDebugPBOs)
        m_apDebugPBOs[m_nNextPBO]->fetchBuffer(oDebug,true);
    else
        m_oLastDebug.copyTo(oDebug);
    return m_nLastDebugInternalIdx;
}

const char* GLImageProcAlgo::getCurrTextureLayerUniformName() {
    return "nCurrLayerIdx";
}

const char* GLImageProcAlgo::getLastTextureLayerUniformName() {
    return "nLastLayerIdx";
}

const char* GLImageProcAlgo::getFrameIndexUniformName() {
    return "nFrameIdx";
}

std::string GLImageProcAlgo::getFragmentShaderSource_internal( int nLayers, int nOutputType, int nDebugType, int nInputType,
                                                               bool bUsingOutput, bool bUsingDebug, bool bUsingInput,
                                                               bool bUsingTexArrays, bool bUsingIntegralFormat) {
    // @@@ replace else-if ladders by switch statements?
    std::stringstream ssSrc;
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ssSrc << "#version 430\n";
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ssSrc << "layout(location=0) out vec4 out_color;\n"
             "layout(location=" << GLVertex::eVertexAttrib_TexCoordIdx << ") in vec4 texCoord2D;\n";
    if(bUsingTexArrays) {
        if(bUsingOutput) ssSrc <<
             "layout(binding=" << GLImageProcAlgo::eTexture_OutputBinding << ") uniform" << (bUsingIntegralFormat?" u":" ") << "sampler2DArray texArrayOutput;\n";
        if(bUsingDebug) ssSrc <<
             "layout(binding=" << GLImageProcAlgo::eTexture_DebugBinding << ") uniform" << (bUsingIntegralFormat?" u":" ") << "sampler2DArray texArrayDebug;\n";
        if(bUsingInput) ssSrc <<
             "layout(binding=" << GLImageProcAlgo::eTexture_InputBinding << ") uniform" << (bUsingIntegralFormat?" u":" ") << "sampler2DArray texArrayInput;\n";
    }
    else
        for(int nLayerIter=0; nLayerIter<nLayers; ++nLayerIter) {
            if(bUsingOutput) ssSrc <<
             "layout(binding=" << (nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_OutputBinding << ") uniform" << (bUsingIntegralFormat?" u":" ") << "sampler2D texOutput" << nLayerIter << ";\n";
            if(bUsingDebug) ssSrc <<
             "layout(binding=" << (nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding << ") uniform" << (bUsingIntegralFormat?" u":" ") << "sampler2D texDebug" << nLayerIter << ";\n";
            if(bUsingInput) ssSrc <<
             "layout(binding=" << (nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_InputBinding << ") uniform" << (bUsingIntegralFormat?" u":" ") << "sampler2D texInput" << nLayerIter << ";\n";
        }
    ssSrc << "uniform uint nCurrLayerIdx;\n";
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ssSrc << "void main() {\n"
             "    float texCoord2DArrayIdx = 1;\n"
             "    vec3 texCoord3D = vec3(modf(texCoord2D.x*" << int(bUsingInput)+int(bUsingDebug)+int(bUsingOutput)<< ",texCoord2DArrayIdx),texCoord2D.y,texCoord2DArrayIdx);\n"
             "    vec2 texCoord2D_dPdx = dFdx(texCoord3D.xy);\n"
             "    vec2 texCoord2D_dPdy = dFdy(texCoord3D.xy);\n";
    if(bUsingTexArrays) {
        if(bUsingInput) { ssSrc <<
             "    if(texCoord2DArrayIdx==0) {\n";
            if(getChannelsFromMatType(nInputType)==1) ssSrc <<
             "        out_color = vec4(textureGrad(texArrayInput,vec3(texCoord3D.xy,nCurrLayerIdx),texCoord2D_dPdx,texCoord2D_dPdy).xxx," << (bUsingIntegralFormat?"255":"1") << ");\n";
            else ssSrc <<
             "        out_color = textureGrad(texArrayInput,vec3(texCoord3D.xy,nCurrLayerIdx),texCoord2D_dPdx,texCoord2D_dPdy);\n";
            ssSrc <<
             "    }\n";
        }
        if(bUsingDebug) { ssSrc <<
             "    " << (bUsingInput?"else if":"if") << "(texCoord2DArrayIdx==" << int(bUsingInput) << ") {\n";
            if(getChannelsFromMatType(nDebugType)==1) ssSrc <<
             "        out_color = vec4(textureGrad(texArrayDebug,vec3(texCoord3D.xy,nCurrLayerIdx),texCoord2D_dPdx,texCoord2D_dPdy).xxx," << (bUsingIntegralFormat?"255":"1") << ");\n";
            else ssSrc <<
             "        out_color = textureGrad(texArrayDebug,vec3(texCoord3D.xy,nCurrLayerIdx),texCoord2D_dPdx,texCoord2D_dPdy);\n";
            ssSrc <<
             "    }\n";
        }
        if(bUsingOutput) { ssSrc <<
             "    " << ((bUsingInput||bUsingDebug)?"else if":"if") << "(texCoord2DArrayIdx==" << int(bUsingInput)+int(bUsingDebug) << ") {\n";
            if(getChannelsFromMatType(nOutputType)==1) ssSrc <<
             "        out_color = vec4(textureGrad(texArrayOutput,vec3(texCoord3D.xy,nCurrLayerIdx),texCoord2D_dPdx,texCoord2D_dPdy).xxx," << (bUsingIntegralFormat?"255":"1") << ");\n";
            else ssSrc <<
             "        out_color = textureGrad(texArrayOutput,vec3(texCoord3D.xy,nCurrLayerIdx),texCoord2D_dPdx,texCoord2D_dPdy);\n";
            ssSrc <<
             "    }\n";
        }
        ssSrc <<
             "    " << ((bUsingInput||bUsingDebug||bUsingOutput)?"else {":"{") << "\n"
             "        out_color = vec4(" << (bUsingIntegralFormat?"255":"1") << ");\n"
             "    }\n";
    }
    else {
        if(bUsingInput) { ssSrc <<
             "    if(texCoord2DArrayIdx==0) {\n";
            for(int nLayerIter=0; nLayerIter<nLayers; ++nLayerIter) { ssSrc <<
             "        " << ((nLayerIter>0)?"else if":"if") << "(nCurrLayerIdx==" << nLayerIter << ") {\n";
                if(getChannelsFromMatType(nInputType)==1) ssSrc <<
             "            out_color = vec4(textureGrad(texInput" << nLayerIter << ",texCoord3D.xy,texCoord2D_dPdx,texCoord2D_dPdy).xxx," << (bUsingIntegralFormat?"255":"1") << ");\n";
                else ssSrc <<
             "            out_color = textureGrad(texInput" << nLayerIter << ",texCoord3D.xy,texCoord2D_dPdx,texCoord2D_dPdy);\n";
                ssSrc <<
             "        }\n";
            }
            ssSrc <<
             "        else {\n"
             "            out_color = vec4(" << (bUsingIntegralFormat?"255":"1") << ");\n"
             "        }\n"
             "    }\n";
        }
        if(bUsingDebug) { ssSrc <<
             "    " << (bUsingInput?"else if":"if") << "(texCoord2DArrayIdx==" << int(bUsingInput) << ") {\n";
            for(int nLayerIter=0; nLayerIter<nLayers; ++nLayerIter) { ssSrc <<
             "        " << ((nLayerIter>0)?"else if":"if") << "(nCurrLayerIdx==" << nLayerIter << ") {\n";
                if(getChannelsFromMatType(nDebugType)==1) ssSrc <<
             "            out_color = vec4(textureGrad(texDebug" << nLayerIter << ",texCoord3D.xy,texCoord2D_dPdx,texCoord2D_dPdy).xxx," << (bUsingIntegralFormat?"255":"1") << ");\n";
                else ssSrc <<
             "            out_color = textureGrad(texDebug" << nLayerIter << ",texCoord3D.xy,texCoord2D_dPdx,texCoord2D_dPdy);\n";
                ssSrc <<
             "        }\n";
            }
            ssSrc <<
             "        else {\n"
             "            out_color = vec4(" << (bUsingIntegralFormat?"255":"1") << ");\n"
             "        }\n"
             "    }\n";
        }
        if(bUsingOutput) { ssSrc <<
             "    " << ((bUsingInput||bUsingDebug)?"else if":"if") << "(texCoord2DArrayIdx==" << int(bUsingInput)+int(bUsingDebug) << ") {\n";
            for(int nLayerIter=0; nLayerIter<nLayers; ++nLayerIter) { ssSrc <<
             "        " << ((nLayerIter>0)?"else if":"if") << "(nCurrLayerIdx==" << nLayerIter << ") {\n";
                if(getChannelsFromMatType(nOutputType)==1) ssSrc <<
             "            out_color = vec4(textureGrad(texOutput" << nLayerIter << ",texCoord3D.xy,texCoord2D_dPdx,texCoord2D_dPdy).xxx," << (bUsingIntegralFormat?"255":"1") << ");\n";
                else ssSrc <<
             "            out_color = textureGrad(texOutput" << nLayerIter << ",texCoord3D.xy,texCoord2D_dPdx,texCoord2D_dPdy);\n";
                ssSrc <<
             "        }\n";
            }
            ssSrc <<
             "        else {\n"
             "            out_color = vec4(" << (bUsingIntegralFormat?"255":"1") << ");\n"
             "        }\n"
             "    }\n";
        }
        ssSrc <<
             "    " << ((bUsingInput||bUsingDebug||bUsingOutput)?"else {":"{") << "\n"
             "        out_color = vec4(" << (bUsingIntegralFormat?"255":"1") << ");\n"
             "    }\n";
    }
    if(bUsingIntegralFormat) ssSrc <<
             "    out_color = out_color/255;\n";
    ssSrc << "}\n";
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    return ssSrc.str();
}

GLEvaluatorAlgo::GLEvaluatorAlgo(const std::shared_ptr<GLImageProcAlgo>& pParent, int nTotFrameCount, int nCountersPerFrame, int nComputeStages, int nDebugType, int nGroundtruthType)
    :    GLImageProcAlgo(1,pParent->m_nLayers,nComputeStages,-1,nDebugType,nGroundtruthType,false,pParent->m_bUsingOutputPBOs,pParent->m_bUsingInputPBOs,pParent->m_bUsingTexArrays,pParent->m_bUsingDisplay,false,pParent->m_bUsingIntegralFormat)
        ,m_nTotFrameCount(nTotFrameCount)
        ,m_nAtomicBufferRangeSize(nCountersPerFrame*4)
        ,m_nAtomicBufferSize(nTotFrameCount*nCountersPerFrame*4)
        ,m_nCurrAtomicBufferOffset(0)
        ,m_pParent(pParent) {
    glAssert(m_bUsingInput && m_pParent->m_bUsingOutput);
    glAssert(m_nInputType==m_pParent->m_nOutputType);
    glAssert(!dynamic_cast<GLEvaluatorAlgo*>(m_pParent.get()));
    glAssert(nTotFrameCount>0 && nCountersPerFrame>0);
    GLint nMaxAtomicCounterBufferSize;
    glGetIntegerv(GL_MAX_ATOMIC_COUNTER_BUFFER_SIZE,&nMaxAtomicCounterBufferSize);
    if(nMaxAtomicCounterBufferSize<(int)m_nAtomicBufferSize)
        glError("atomic counter buffer size limit is too small for the current impl");
    glGenBuffers(1,&m_nAtomicBuffer);
    m_pParent->m_bUsingDisplay = false;
}

GLEvaluatorAlgo::~GLEvaluatorAlgo() {
    glDeleteBuffers(1,&m_nAtomicBuffer);
}

const cv::Mat& GLEvaluatorAlgo::getAtomicCounterBuffer() {
    glAssert(m_bGLInitialized);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER,getAtomicBufferId());
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER,0,m_nAtomicBufferSize,m_oAtomicCountersQueryBuffer.data);
    glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER,0,getAtomicBufferId(),m_nCurrAtomicBufferOffset,m_nAtomicBufferRangeSize);
    glErrorCheck;
    return m_oAtomicCountersQueryBuffer;
}

std::string GLEvaluatorAlgo::getFragmentShaderSource() const {
    return GLImageProcAlgo::getFragmentShaderSource_internal(m_nLayers,-1,m_nDebugType,m_pParent->m_nInputType,false,m_bUsingDebug,m_pParent->m_bUsingInput,m_bUsingTexArrays,m_bUsingIntegralFormat);
}

void GLEvaluatorAlgo::initialize(const cv::Mat& oInitInput, const cv::Mat& oInitGT, const cv::Mat& oROI) {
    m_pParent->initialize(oInitInput,oROI);
    this->initialize(oInitGT,oROI);
}

void GLEvaluatorAlgo::initialize(const cv::Mat& oInitGT, const cv::Mat& oROI) {
    glAssert(!oROI.empty() && oROI.isContinuous() && oROI.type()==CV_8UC1);
    glAssert(oROI.size()==m_pParent->m_oFrameSize);
    glAssert(oInitGT.type()==m_nInputType && oInitGT.size()==oROI.size() && oInitGT.isContinuous());
    m_bGLInitialized = false;
    m_oFrameSize = oROI.size();
    for(int nPBOIter=0; nPBOIter<2; ++nPBOIter) {
        if(m_bUsingDebugPBOs)
            m_apDebugPBOs[nPBOIter] = std::unique_ptr<GLPixelBufferObject>(new GLPixelBufferObject(cv::Mat(m_oFrameSize,m_nDebugType),GL_PIXEL_PACK_BUFFER,GL_STREAM_READ));
        if(m_bUsingInputPBOs)
            m_apInputPBOs[nPBOIter] = std::unique_ptr<GLPixelBufferObject>(new GLPixelBufferObject(oInitGT,GL_PIXEL_UNPACK_BUFFER,GL_STREAM_DRAW));
    }
    if(m_bUsingTexArrays) {
        if(m_bUsingDebug) {
            m_pDebugArray = std::unique_ptr<GLDynamicTexture2DArray>(new GLDynamicTexture2DArray(1,std::vector<cv::Mat>(m_nLayers,cv::Mat(m_oFrameSize,m_nDebugType)),m_bUsingIntegralFormat));
            m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
        }
        m_pInputArray = std::unique_ptr<GLDynamicTexture2DArray>(new GLDynamicTexture2DArray(1,std::vector<cv::Mat>(m_nLayers,cv::Mat(m_oFrameSize,m_nInputType)),m_bUsingIntegralFormat));
        m_pInputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_GTBinding);
        if(m_bUsingInputPBOs) {
            m_pInputArray->updateTexture(*m_apInputPBOs[m_nCurrPBO],m_nCurrLayer,true);
            m_pInputArray->updateTexture(*m_apInputPBOs[m_nCurrPBO],m_nNextLayer,true);
        }
        else {
            m_pInputArray->updateTexture(oInitGT,m_nCurrLayer,true);
            m_pInputArray->updateTexture(oInitGT,m_nNextLayer,true);
        }
    }
    else {
        m_vpDebugArray.resize(m_nLayers);
        m_vpInputArray.resize(m_nLayers);
        for(int nLayerIter=0; nLayerIter<m_nLayers; ++nLayerIter) {
            if(m_bUsingDebug) {
                m_vpDebugArray[nLayerIter] = std::unique_ptr<GLDynamicTexture2D>(new GLDynamicTexture2D(1,cv::Mat(m_oFrameSize,m_nDebugType),m_bUsingIntegralFormat));
                m_vpDebugArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
            }
            m_vpInputArray[nLayerIter] = std::unique_ptr<GLDynamicTexture2D>(new GLDynamicTexture2D(m_nLevels,cv::Mat(m_oFrameSize,m_nInputType),m_bUsingIntegralFormat));
            m_vpInputArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_GTBinding);
            if(m_bUsingInputPBOs) {
                if(nLayerIter==m_nCurrLayer)
                    m_vpInputArray[m_nCurrLayer]->updateTexture(*m_apInputPBOs[m_nCurrPBO],true);
                else if(nLayerIter==m_nNextLayer)
                    m_vpInputArray[m_nNextLayer]->updateTexture(*m_apInputPBOs[m_nCurrPBO],true);
            }
            else {
                if(nLayerIter==m_nCurrLayer)
                    m_vpInputArray[m_nCurrLayer]->updateTexture(oInitGT,true);
                else if(nLayerIter==m_nNextLayer)
                    m_vpInputArray[m_nNextLayer]->updateTexture(oInitGT,true);
            }
        }
    }
    m_pROITexture = std::unique_ptr<GLTexture2D>(new GLTexture2D(1,oROI,m_bUsingIntegralFormat));
    m_pROITexture->bindToImage(GLImageProcAlgo::eImage_ROIBinding,0,GL_READ_ONLY);
    if(!m_bUsingDebugPBOs && m_bUsingDebug)
        m_oLastDebug = cv::Mat(m_oFrameSize,m_nDebugType);
    m_vpImgProcShaders.resize(m_nComputeStages);
    for(int nCurrStageIter=0; nCurrStageIter<m_nComputeStages; ++nCurrStageIter) {
        m_vpImgProcShaders[nCurrStageIter] = std::unique_ptr<GLShader>(new GLShader());
        m_vpImgProcShaders[nCurrStageIter]->addSource(getComputeShaderSource(nCurrStageIter),GL_COMPUTE_SHADER);
        if(!m_vpImgProcShaders[nCurrStageIter]->link())
            glError("Could not link image processing shader");
    }
    m_oDisplayShader.clear();
    m_oDisplayShader.addSource(this->getVertexShaderSource(),GL_VERTEX_SHADER);
    m_oDisplayShader.addSource(this->getFragmentShaderSource(),GL_FRAGMENT_SHADER);
    if(!m_oDisplayShader.link())
        glError("Could not link display shader");
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER,getAtomicBufferId());
    glBufferData(GL_ATOMIC_COUNTER_BUFFER,m_nAtomicBufferSize,NULL,GL_DYNAMIC_DRAW);
    GLuint* pAtomicCountersPtr = (GLuint*)glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER,0,m_nAtomicBufferSize,GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT|GL_MAP_UNSYNCHRONIZED_BIT);
    if(!pAtomicCountersPtr)
        glError("Could not init atomic counters");
    memset(pAtomicCountersPtr,0,m_nAtomicBufferSize);
    glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
    m_nCurrAtomicBufferOffset = 0;
    m_oAtomicCountersQueryBuffer.create(m_nTotFrameCount,m_nAtomicBufferRangeSize/4,CV_32SC1);
    glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER,0,getAtomicBufferId(),m_nCurrAtomicBufferOffset,m_nAtomicBufferRangeSize);
    glErrorCheck;
    m_nInternalFrameIdx = 0;
    m_bGLInitialized = true;
}

void GLEvaluatorAlgo::apply(const cv::Mat& oNextInput, const cv::Mat& oNextGT, bool bRebindAll) {
    m_pParent->apply(oNextInput,bRebindAll);
    this->apply(oNextGT,bRebindAll);
}

void GLEvaluatorAlgo::apply(const cv::Mat& oNextGT, bool bRebindAll) {
    glAssert(m_bGLInitialized && (oNextGT.empty() || (oNextGT.type()==m_nInputType && oNextGT.size()==m_oFrameSize && oNextGT.isContinuous())));
    CV_Assert(m_nInternalFrameIdx<m_nTotFrameCount);
    m_nLastLayer = m_nCurrLayer;
    m_nCurrLayer = m_nNextLayer;
    ++m_nNextLayer %= m_nLayers;
    m_nCurrPBO = m_nNextPBO;
    ++m_nNextPBO %= 2;
    glAssert(m_nCurrAtomicBufferOffset+m_nAtomicBufferRangeSize<=m_nAtomicBufferSize)
    glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER,0,m_nAtomicBuffer,m_nCurrAtomicBufferOffset,m_nAtomicBufferRangeSize);
    m_nCurrAtomicBufferOffset += m_nAtomicBufferRangeSize;
    if(bRebindAll) {
        for(int nBufferBindingIter=0; nBufferBindingIter<eBufferBindingsCount; ++nBufferBindingIter) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,m_anSSBO[nBufferBindingIter]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,(GLImageProcAlgo::eBufferBindingList)nBufferBindingIter,m_anSSBO[nBufferBindingIter]);
        }
    }
    if(m_bUsingInputPBOs && !oNextGT.empty())
        m_apInputPBOs[m_nNextPBO]->updateBuffer(oNextGT,false,bRebindAll);
    if(m_bUsingTexArrays) {
        if(m_bUsingDebug) {
            if(bRebindAll || (m_pParent->m_bFetchingDebug && (m_bUsingDisplay||m_bFetchingDebug)))
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
            m_pDebugArray->bindToImage(GLImageProcAlgo::eImage_DebugBinding,0,m_nCurrLayer,GL_READ_WRITE);
        }
        if(!m_bUsingInputPBOs && !oNextGT.empty()) {
            m_pInputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_GTBinding);
            m_pInputArray->updateTexture(oNextGT,m_nNextLayer,bRebindAll);
        }
        m_pInputArray->bindToImage(GLImageProcAlgo::eImage_GTBinding,0,m_nCurrLayer,GL_READ_ONLY);
        m_pParent->m_pOutputArray->bindToImage(GLImageProcAlgo::eImage_OutputBinding,0,m_pParent->m_nCurrLayer,GL_READ_ONLY);
    }
    else {
        for(int nLayerIter=0; nLayerIter<m_nLayers; ++nLayerIter) {
            if(bRebindAll || (m_pParent->m_bFetchingDebug && (m_bUsingDisplay||m_bFetchingDebug))) {
                if(m_bUsingDebug)
                    m_vpDebugArray[nLayerIter]->bindToSampler((nLayerIter*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
            }
            if(nLayerIter==m_nNextLayer && !m_bUsingInputPBOs && !oNextGT.empty()) {
                m_vpInputArray[m_nNextLayer]->bindToSampler((m_nNextLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_GTBinding);
                m_vpInputArray[m_nNextLayer]->updateTexture(oNextGT,bRebindAll);
            }
            else if(nLayerIter==m_nCurrLayer) {
                if(m_bUsingDebug)
                    m_vpDebugArray[m_nCurrLayer]->bindToImage(GLImageProcAlgo::eImage_DebugBinding,0,GL_READ_WRITE);
                m_vpInputArray[m_nCurrLayer]->bindToImage(GLImageProcAlgo::eImage_GTBinding,0,GL_READ_ONLY);
            }
        }
        m_pParent->m_vpOutputArray[m_pParent->m_nCurrLayer]->bindToImage(GLImageProcAlgo::eImage_OutputBinding,0,GL_READ_ONLY);
    }
    m_pROITexture->bindToImage(GLImageProcAlgo::eImage_ROIBinding,0,GL_READ_ONLY);
    for(int nCurrStageIter=0; nCurrStageIter<m_nComputeStages; ++nCurrStageIter) {
        glAssert(m_vpImgProcShaders[nCurrStageIter]->activate());
        m_vpImgProcShaders[nCurrStageIter]->setUniform1ui(getCurrTextureLayerUniformName(),m_nCurrLayer);
        m_vpImgProcShaders[nCurrStageIter]->setUniform1ui(getLastTextureLayerUniformName(),m_nLastLayer);
        m_vpImgProcShaders[nCurrStageIter]->setUniform1ui(getFrameIndexUniformName(),m_nInternalFrameIdx);
        dispatch(nCurrStageIter,*m_vpImgProcShaders[nCurrStageIter]);
    }
    if(m_bUsingInputPBOs) {
        if(m_bUsingTexArrays) {
            m_pInputArray->bindToSamplerArray(GLImageProcAlgo::eTexture_GTBinding);
            m_pInputArray->updateTexture(*m_apInputPBOs[m_nNextPBO],m_nNextLayer,bRebindAll);
        }
        else {
            m_vpInputArray[m_nNextLayer]->bindToSampler((m_nNextLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_GTBinding);
            m_vpInputArray[m_nNextLayer]->updateTexture(*m_apInputPBOs[m_nNextPBO],bRebindAll);
        }
    }
    if(m_bFetchingDebug) {
        m_nLastDebugInternalIdx = m_nInternalFrameIdx;
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
        if(m_bUsingDebugPBOs) {
            if(m_bUsingTexArrays) {
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
                m_pDebugArray->fetchTexture(*m_apDebugPBOs[m_nNextPBO],m_nCurrLayer,bRebindAll);
            }
            else {
                m_vpDebugArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
                m_vpDebugArray[m_nCurrLayer]->fetchTexture(*m_apDebugPBOs[m_nNextPBO],bRebindAll);
            }
        }
        else {
            if(m_bUsingTexArrays) {
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
                m_pDebugArray->fetchTexture(m_oLastDebug,m_nCurrLayer);
            }
            else {
                m_vpDebugArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
                m_vpDebugArray[m_nCurrLayer]->fetchTexture(m_oLastDebug);
            }
        }
    }
    if(m_bUsingDisplay) {
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        if(m_bUsingDebug) {
            if(m_bUsingTexArrays)
                m_pDebugArray->bindToSamplerArray(GLImageProcAlgo::eTexture_DebugBinding);
            else
                m_vpDebugArray[m_nCurrLayer]->bindToSampler((m_nCurrLayer*GLImageProcAlgo::eTextureBindingsCount)+GLImageProcAlgo::eTexture_DebugBinding);
        }
        glAssert(m_oDisplayShader.activate());
        m_oDisplayShader.setUniform1ui(getCurrTextureLayerUniformName(),m_nCurrLayer);
        m_oDisplayBillboard.render();
    }
    m_pParent->m_pROITexture->bindToImage(GLImageProcAlgo::eImage_ROIBinding,0,GL_READ_ONLY);
    ++m_nInternalFrameIdx;
}

GLImagePassThroughAlgo::GLImagePassThroughAlgo( int nLayers, int nFrameType, bool bUseOutputPBOs, bool bUseInputPBOs,
                                                bool bUseTexArrays, bool bUseDisplay, bool bUseTimers, bool bUseIntegralFormat)
    :    GLImageProcAlgo(1,nLayers,1,nFrameType,-1,nFrameType,bUseOutputPBOs,false,bUseInputPBOs,bUseTexArrays,bUseDisplay,bUseTimers,bUseIntegralFormat) {
    glAssert(nFrameType>=0);
}

std::string GLImagePassThroughAlgo::getComputeShaderSource(int nStage) const {
    glAssert(nStage>=0 && nStage<m_nComputeStages);
    return GLShader::getPassThroughComputeShaderSource_ImgLoadCopy(m_vDefaultWorkGroupSize,getInternalFormatFromMatType(m_nInputType,m_bUsingIntegralFormat),GLImageProcAlgo::eImage_InputBinding,GLImageProcAlgo::eImage_OutputBinding,m_bUsingIntegralFormat);
}

void GLImagePassThroughAlgo::dispatch(int nStage, GLShader&) {
    glAssert(nStage>=0 && nStage<m_nComputeStages);
    glDispatchCompute((GLuint)ceil((float)m_oFrameSize.width/m_vDefaultWorkGroupSize.x), (GLuint)ceil((float)m_oFrameSize.height/m_vDefaultWorkGroupSize.y), 1);
}
/*
const int BinaryMedianFilter::m_nPPSMaxRowSize = 512;
const int BinaryMedianFilter::m_nTransposeBlockSize = 32;
const GLenum BinaryMedianFilter::eImage_PPSAccumulator = GLImageProcAlgo::eImage_CustomBinding1;
const GLenum BinaryMedianFilter::eImage_PPSAccumulator_T = GLImageProcAlgo::eImage_CustomBinding2;

BinaryMedianFilter::BinaryMedianFilter( int nKernelSize, int nBorderSize, int nLayers, const cv::Mat& oROI,
                                        bool bUseOutputPBOs, bool bUseInputPBOs, bool bUseTexArrays,
                                        bool bUseDisplay, bool bUseTimers, bool bUseIntegralFormat)
    :    GLImageProcAlgo(1,nLayers,4+bool(oROI.cols>m_nPPSMaxRowSize)+bool(oROI.rows>m_nPPSMaxRowSize),CV_8UC1,-1,CV_8UC1,bUseOutputPBOs,false,bUseInputPBOs,bUseTexArrays,bUseDisplay,bUseTimers,bUseIntegralFormat)
        ,m_nKernelSize(nKernelSize)
        ,m_nBorderSize(nBorderSize) {
    glAssert((m_nKernelSize%2)==1 && m_nKernelSize>1 && m_nKernelSize<m_oFrameSize.width && m_nKernelSize<m_oFrameSize.height);
    glAssert(m_nBorderSize>=0 && m_nBorderSize<(m_oFrameSize.width-m_nKernelSize) && m_nBorderSize<(m_oFrameSize.height-m_nKernelSize));
    int nMaxComputeInvocs;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,&nMaxComputeInvocs);
    const int nCurrComputeStageInvocs = m_vDefaultWorkGroupSize.x*m_vDefaultWorkGroupSize.y;
    glAssert(nCurrComputeStageInvocs>0 && nCurrComputeStageInvocs<nMaxComputeInvocs);
    glAssert(m_nTransposeBlockSize*m_nTransposeBlockSize>0 && m_nTransposeBlockSize*m_nTransposeBlockSize<nMaxComputeInvocs);
    int nMaxWorkGroupCount_X, nMaxWorkGroupCount_Y;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,0,&nMaxWorkGroupCount_X);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,1,&nMaxWorkGroupCount_Y);
    glAssert(m_oFrameSize.width<nMaxWorkGroupCount_X && m_oFrameSize.width<nMaxWorkGroupCount_Y);
    glAssert(m_oFrameSize.height<nMaxWorkGroupCount_X && m_oFrameSize.height<nMaxWorkGroupCount_Y);
    const GLenum eInputInternalFormat = getIntegralFormatFromInternalFormat(getInternalFormatFromMatType(m_nInputType,m_bUsingIntegralFormat));
    const GLenum eAccumInternalFormat = getIntegralFormatFromInternalFormat(getInternalFormatFromMatType(CV_32SC1));
    if(m_oFrameSize.width>m_nPPSMaxRowSize) {
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_ParallelPrefixSum(m_nPPSMaxRowSize,true,eInputInternalFormat,GLImageProcAlgo::eImage_InputBinding,BinaryMedianFilter::eImage_PPSAccumulator));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.width/m_nPPSMaxRowSize),m_oFrameSize.height,1));
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_ParallelPrefixSum_BlockMerge(m_oFrameSize.width,m_nPPSMaxRowSize,m_oFrameSize.height,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3(1,1,1));
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_Transpose(m_nTransposeBlockSize,eAccumInternalFormat,BinaryMedianFilter::eImage_PPSAccumulator,BinaryMedianFilter::eImage_PPSAccumulator_T));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.width/m_nTransposeBlockSize), (GLuint)ceil((float)m_oFrameSize.height/m_nTransposeBlockSize), 1));
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_ParallelPrefixSum(m_nPPSMaxRowSize,false,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator_T,BinaryMedianFilter::eImage_PPSAccumulator_T));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.height/m_nPPSMaxRowSize),m_oFrameSize.width,1));
        if(m_oFrameSize.height>m_nPPSMaxRowSize) {
            m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_ParallelPrefixSum_BlockMerge(m_oFrameSize.height,m_nPPSMaxRowSize,m_oFrameSize.width,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator_T));
            m_vvComputeShaderDispatchSizes.push_back(glm::uvec3(1,1,1));
        }
        //m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_Transpose(m_nTransposeBlockSize,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator_T,ImageProcShaderAlgo::eImage_OutputBinding));
        //m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.height/m_nTransposeBlockSize),(GLuint)ceil((float)m_oFrameSize.width/m_nTransposeBlockSize),1));
    }
    else {
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_ParallelPrefixSum(m_nPPSMaxRowSize,true,eInputInternalFormat,GLImageProcAlgo::eImage_InputBinding,BinaryMedianFilter::eImage_PPSAccumulator));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.width/m_nPPSMaxRowSize),m_oFrameSize.height,1));
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_Transpose(m_nTransposeBlockSize,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator,BinaryMedianFilter::eImage_PPSAccumulator_T));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.width/m_nTransposeBlockSize),(GLuint)ceil((float)m_oFrameSize.height/m_nTransposeBlockSize),1));
        m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_ParallelPrefixSum(m_nPPSMaxRowSize,false,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator_T,BinaryMedianFilter::eImage_PPSAccumulator_T));
        m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.height/m_nPPSMaxRowSize),m_oFrameSize.width,1));
        //m_vsComputeShaderSources.push_back(ComputeShaderUtils::getComputeShaderSource_Transpose(m_nTransposeBlockSize,getInternalFormatFromMatType(CV_32SC1),BinaryMedianFilter::eImage_PPSAccumulator_T,ImageProcShaderAlgo::eImage_OutputBinding));
        //m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.height/m_nTransposeBlockSize),(GLuint)ceil((float)m_oFrameSize.width/m_nTransposeBlockSize),1));
    }
    // area lookup with clamped coords & final output write
    const char* acAccumInternalFormatName = getGLSLFormatNameFromInternalFormat(eAccumInternalFormat);
    std::stringstream ssSrc;
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ssSrc << "#version 430\n"
             "#define radius " << m_nKernelSize/2 << "\n"
             "#define imgWidth " << m_oFrameSize.width << "\n"
             "#define imgHeight " << m_oFrameSize.height << "\n"
             "#define halfkernelarea " << (m_nKernelSize*m_nKernelSize)/2 << "\n";
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ssSrc << "layout(local_size_x=" << m_vDefaultWorkGroupSize.x << ",local_size_y=" << m_vDefaultWorkGroupSize.y << ") in;\n"
             "layout(binding=" << BinaryMedianFilter::eImage_PPSAccumulator_T << ", " << acAccumInternalFormatName << ") readonly uniform uimage2D imgInput;\n"
             "layout(binding=" << GLImageProcAlgo::eImage_OutputBinding << ") writeonly uniform uimage2D imgOutput;\n";
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ssSrc << "void main() {\n"
            // @@@@@@@@@@ could also benefit from shared mem, fetch area sums in and around local work group
             "    ivec2 center = ivec2(gl_GlobalInvocationID.xy);\n"
             "    // area sum = D - C - B + A\n"
             "    ivec2 D = min(center+ivec2(radius),ivec2(imgWidth-1,imgHeight-1));\n"
             "    ivec2 C = ivec2(max(center.x-radius,0),min(center.y+radius,imgHeight-1));\n"
             "    ivec2 B = ivec2(min(center.x+radius,imgWidth-1),max(center.y-radius,0));\n"
             "    ivec2 A = max(center-ivec2(radius),ivec2(0));\n"
             "    uint areasum = imageLoad(imgInput,D.yx).r-imageLoad(imgInput,C.yx).r-imageLoad(imgInput,B.yx).r+imageLoad(imgInput,A.yx).r;\n"
             "    imageStore(imgOutput,center,uvec4(255*uint(areasum>halfkernelarea)));\n"
             "}\n";
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    m_vsComputeShaderSources.push_back(ssSrc.str());
    m_vvComputeShaderDispatchSizes.push_back(glm::uvec3((GLuint)ceil((float)m_oFrameSize.width/m_vDefaultWorkGroupSize.x),(GLuint)ceil((float)m_oFrameSize.height/m_vDefaultWorkGroupSize.y),1));
    glAssert((int)m_vsComputeShaderSources.size()==m_nComputeStages && (int)m_vvComputeShaderDispatchSizes.size()==m_nComputeStages);
}

std::string BinaryMedianFilter::getComputeShaderSource(int nStage) const {
    // @@@@ go check how opencv handles borders (sets as 0...?)
    glAssert(nStage>=0 && nStage<m_nComputeStages);
    return m_vsComputeShaderSources[nStage];
}

void BinaryMedianFilter::dispatchCompute(int nStage, GLShader*) {
    glAssert(nStage>=0 && nStage<m_nComputeStages);
    if(nStage>0)
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glDispatchCompute(m_vvComputeShaderDispatchSizes[nStage].x,m_vvComputeShaderDispatchSizes[nStage].y,m_vvComputeShaderDispatchSizes[nStage].z);
}

void BinaryMedianFilter::postInitialize(const cv::Mat&) {
    m_apCustomTextures[0] = std::unique_ptr<GLTexture2D>(new GLTexture2D(1,cv::Mat(m_oFrameSize,CV_32SC1),m_bUsingIntegralFormat));
    m_apCustomTextures[0]->bindToImage(BinaryMedianFilter::eImage_PPSAccumulator,0,GL_READ_WRITE);
    m_apCustomTextures[1] = std::unique_ptr<GLTexture2D>(new GLTexture2D(1,cv::Mat(cv::Size(m_oFrameSize.height,m_oFrameSize.width),CV_32SC1),m_bUsingIntegralFormat));
    m_apCustomTextures[1]->bindToImage(BinaryMedianFilter::eImage_PPSAccumulator_T,0,GL_READ_WRITE);
    glErrorCheck;
}

void BinaryMedianFilter::preProcess(bool bRebindAll) {
    if(bRebindAll) {
        m_apCustomTextures[0]->bindToImage(BinaryMedianFilter::eImage_PPSAccumulator,0,GL_READ_WRITE);
        m_apCustomTextures[1]->bindToImage(BinaryMedianFilter::eImage_PPSAccumulator_T,0,GL_READ_WRITE);
        glErrorCheck;
    }
}
*/