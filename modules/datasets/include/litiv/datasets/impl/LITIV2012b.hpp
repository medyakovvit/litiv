
// This file is part of the LITIV framework; visit the original repository at
// https://github.com/plstcharles/litiv for more information.
//
// Copyright 2015 Pierre-Luc St-Charles; pierre-luc.st-charles<at>polymtl.ca
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

// note: we should already be in the litiv namespace
#ifndef __LITIV_DATASETS_IMPL_H
#error "This file should never be included directly; use litiv/datasets.hpp instead"
#endif //__LITIV_DATASETS_IMPL_H

template<ParallelUtils::eParallelAlgoType eEvalImpl>
struct Dataset_<eDatasetType_VideoRegistr,eDataset_VideoRegistr_LITIV2012b,eEvalImpl> :
        public IDataset_<eDatasetType_VideoRegistr,eDataset_VideoRegistr_LITIV2012b,eEvalImpl> {
protected: // should still be protected, as creation should always be done via datasets::create
    Dataset_(
            const std::string& sOutputDirName, //!< output directory (full) path for debug logs, evaluation reports and results archiving (will be created in LITIV dataset folder)
            bool bSaveOutput=false, //!< defines whether results should be archived or not
            bool bUseEvaluator=true, //!< defines whether results should be fully evaluated, or simply acknowledged
            bool bForce4ByteDataAlign=false, //!< defines whether data packets should be 4-byte aligned (useful for GPU upload)
            double dScaleFactor=1.0 //!< defines the scale factor to use to resize/rescale read packets
    ) :
            IDataset_<eDatasetType_VideoRegistr,eDataset_VideoRegistr_LITIV2012b,eEvalImpl>(
                    "LITIV 2012b (CVPRW2015 update)",
                    PlatformUtils::AddDirSlashIfMissing(EXTERNAL_DATA_ROOT)+"litiv/litiv2012b/",
                    PlatformUtils::AddDirSlashIfMissing(EXTERNAL_DATA_ROOT)+"litiv/litiv2012b/"+PlatformUtils::AddDirSlashIfMissing(sOutputDirName),
                    "homography",
                    ".cvmat",
                    std::vector<std::string>{""},
                    std::vector<std::string>{},
                    std::vector<std::string>{"THERMAL"},
                    0,
                    bSaveOutput,
                    bUseEvaluator,
                    bForce4ByteDataAlign,
                    dScaleFactor
            ) {}
};

template<eDatasetTaskList eDatasetTask>
struct DataProducer_<eDatasetTask,eDatasetSource_Video,eDataset_LITIV2012b> :
        public DataProducer_c<eDatasetTask,eDatasetSource_Video> {
protected:
    virtual void parseData() override final {
        /* @@@@ old bsds500 below
        std::vector<std::string> vsImgPaths;
        PlatformUtils::GetFilesFromDir(m_sDatasetPath,vsImgPaths);
        bool bFoundScript=false, bFoundGTFile=false;
        const std::string sGTFilePrefix("hand_segmented_");
        const size_t nInputFileNbDecimals = 5;
        const std::string sInputFileSuffix(".bmp");
        for(auto iter=vsImgPaths.begin(); iter!=vsImgPaths.end(); ++iter) {
            if(*iter==m_sDatasetPath+"/script.txt")
                bFoundScript = true;
            else if(iter->find(sGTFilePrefix)!=std::string::npos) {
                m_mTestGTIndexes.insert(std::pair<size_t,size_t>(atoi(iter->substr(iter->find(sGTFilePrefix)+sGTFilePrefix.size(),nInputFileNbDecimals).c_str()),m_vsGTFramePaths.size()));
                m_vsGTFramePaths.push_back(*iter);
                bFoundGTFile = true;
            }
            else {
                if(iter->find(sInputFileSuffix)!=iter->size()-sInputFileSuffix.size())
                    throw std::runtime_error(cv::format("Sequence '%s' contained an unknown file ('%s')",sSeqName.c_str(),iter->c_str()));
                m_vsInputFramePaths.push_back(*iter);
            }
        }
        if(!bFoundGTFile || !bFoundScript || m_vsInputFramePaths.empty() || m_vsGTFramePaths.size()!=1)
            throw std::runtime_error(cv::format("Sequence '%s' did not possess the required groundtruth and input files",sSeqName.c_str()));
        cv::Mat oTempImg = cv::imread(m_vsGTFramePaths[0]);
        if(oTempImg.empty())
            throw std::runtime_error(cv::format("Sequence '%s' did not possess a valid GT file",sSeqName.c_str()));
        m_oROI = cv::Mat(oTempImg.size(),CV_8UC1,cv::Scalar_<uchar>(255));
        m_oSize = oTempImg.size();
        m_nTotFrameCount = m_vsInputFramePaths.size();
        CV_Assert(m_nTotFrameCount>0);
        m_dExpectedLoad = (double)m_oSize.height*m_oSize.width*m_nTotFrameCount*(int(!m_bForcingGrayscale)+1);
        m_pEvaluator = std::shared_ptr<EvaluatorBase>(new BinarySegmEvaluator("WALLFLOWER_EVAL"));
        */
        /* @@@@ old default below
        PlatformUtils::GetFilesFromDir(m_sDatasetPath,m_vsInputImagePaths);
        PlatformUtils::FilterFilePaths(m_vsInputImagePaths,{},{".jpg"});
        if(m_vsInputImagePaths.empty())
            throw std::runtime_error(cv::format("Image set '%s' did not possess any jpg image file",sSetName.c_str()));
        for(size_t n=0; n<m_vsInputImagePaths.size(); ++n) {
            cv::Mat oCurrInput = cv::imread(m_vsInputImagePaths[n]);
            if(m_oMaxSize.width<oCurrInput.cols)
                m_oMaxSize.width = oCurrInput.cols;
            if(m_oMaxSize.height<oCurrInput.rows)
                m_oMaxSize.height = oCurrInput.rows;
        }
        m_nTotImageCount = m_vsInputImagePaths.size();
        m_dExpectedLoad = (double)m_oMaxSize.area()*m_nTotImageCount*(int(!m_bForcingGrayscale)+1);
        @@@@@@@ and after all
        m_voOrigImageSizes.resize(m_nTotImageCount);
        m_vsOrigImageNames.resize(m_nTotImageCount);
        for(size_t nImageIdx=0; nImageIdx<m_vsInputImagePaths.size(); ++nImageIdx) {
            const size_t nLastSlashPos = m_vsInputImagePaths[nImageIdx].find_last_of("/\\");
            const std::string sImageFullName = nLastSlashPos==std::string::npos?m_vsInputImagePaths[nImageIdx]:m_vsInputImagePaths[nImageIdx].substr(nLastSlashPos+1);
            const size_t nLastDotPos = sImageFullName.find_last_of(".");
            const std::string sImageName = nLastSlashPos==std::string::npos?sImageFullName:sImageFullName.substr(0,nLastDotPos);
            m_vsOrigImageNames[nImageIdx] = sImageName;
        }
        */
        lvError("Missing impl");
    }
    virtual cv::Mat _getGTPacket_impl(size_t nIdx) override final {
        /*
        cv::Mat litiv::Image::Segm::Set::GetInputFromIndex_external(size_t nImageIdx) {
            cv::Mat oImage;
            oImage = cv::imread(m_vsInputImagePaths[nImageIdx],m_bForcingGrayscale?cv::IMREAD_GRAYSCALE:cv::IMREAD_COLOR);
            CV_Assert(!oImage.empty());
            CV_Assert(m_voOrigImageSizes[nImageIdx]==cv::Size() || m_voOrigImageSizes[nImageIdx]==oImage.size());
            m_voOrigImageSizes[nImageIdx] = oImage.size();
            if(m_eDatasetID==eDataset_BSDS500_segm_train || m_eDatasetID==eDataset_BSDS500_segm_train_valid || m_eDatasetID==eDataset_BSDS500_segm_train_valid_test ||
               m_eDatasetID==eDataset_BSDS500_edge_train || m_eDatasetID==eDataset_BSDS500_edge_train_valid || m_eDatasetID==eDataset_BSDS500_edge_train_valid_test) {
                CV_Assert(oImage.size()==cv::Size(481,321) || oImage.size()==cv::Size(321,481));
                if(oImage.size()==cv::Size(321,481))
                    cv::transpose(oImage,oImage);
            }
            CV_Assert(oImage.cols<=m_oMaxSize.width && oImage.rows<=m_oMaxSize.height);
            if(m_bForcing4ByteDataAlign && oImage.channels()==3)
                cv::cvtColor(oImage,oImage,cv::COLOR_BGR2BGRA);
        #if HARDCODE_FRAME_INDEX
            std::stringstream sstr;
            sstr << "Image #" << nImageIdx;
            WriteOnImage(oImage,sstr.str(),cv::Scalar_<uchar>::all(255);
        #endif //HARDCODE_FRAME_INDEX
            return oImage;
        }
        */
        /*
        cv::Mat litiv::Image::Segm::Set::GetGTFromIndex_external(size_t nImageIdx) {
            cv::Mat oImage;
            if(m_eDatasetID==eDataset_BSDS500_edge_train || m_eDatasetID==eDataset_BSDS500_edge_train_valid || m_eDatasetID==eDataset_BSDS500_edge_train_valid_test) {
                if(m_vsGTImagePaths.size()>nImageIdx) {
                    std::vector<std::string> vsTempPaths;
                    PlatformUtils::GetFilesFromDir(m_vsGTImagePaths[nImageIdx],vsTempPaths);
                    CV_Assert(!vsTempPaths.empty());
                    cv::Mat oTempRefGTImage = cv::imread(vsTempPaths[0],cv::IMREAD_GRAYSCALE);
                    CV_Assert(!oTempRefGTImage.empty());
                    CV_Assert(m_voOrigImageSizes[nImageIdx]==cv::Size() || m_voOrigImageSizes[nImageIdx]==oTempRefGTImage.size());
                    CV_Assert(oTempRefGTImage.size()==cv::Size(481,321) || oTempRefGTImage.size()==cv::Size(321,481));
                    m_voOrigImageSizes[nImageIdx] = oTempRefGTImage.size();
                    if(oTempRefGTImage.size()==cv::Size(321,481))
                        cv::transpose(oTempRefGTImage,oTempRefGTImage);
                    oImage.create(int(oTempRefGTImage.rows*vsTempPaths.size()),oTempRefGTImage.cols,CV_8UC1);
                    for(size_t nGTImageIdx=0; nGTImageIdx<vsTempPaths.size(); ++nGTImageIdx) {
                        cv::Mat oTempGTImage = cv::imread(vsTempPaths[nGTImageIdx],cv::IMREAD_GRAYSCALE);
                        CV_Assert(!oTempGTImage.empty() && (oTempGTImage.size()==cv::Size(481,321) || oTempGTImage.size()==cv::Size(321,481)));
                        if(oTempGTImage.size()==cv::Size(321,481))
                            cv::transpose(oTempGTImage,oTempGTImage);
                        oTempGTImage.copyTo(cv::Mat(oImage,cv::Rect(0,int(oTempGTImage.rows*nGTImageIdx),oTempGTImage.cols,oTempGTImage.rows)));
                    }
                    if(m_bForcing4ByteDataAlign && oImage.channels()==3)
                        cv::cvtColor(oImage,oImage,cv::COLOR_BGR2BGRA);
                }
            }
            if(oImage.empty())
                oImage = cv::Mat(m_oMaxSize,CV_8UC1,cv::Scalar_<uchar>(DATASETUTILS_OUT_OF_SCOPE_DEFAULT_VAL));
        #if HARDCODE_FRAME_INDEX
            std::stringstream sstr;
            sstr << "Image #" << nImageIdx;
            WriteOnImage(oImage,sstr.str(),cv::Scalar_<uchar>::all(255);
        #endif //HARDCODE_FRAME_INDEX
            return oImage;
        }
        */
        /*
        cv::Mat litiv::Image::Segm::Set::ReadResult(size_t nImageIdx) {
            CV_Assert(m_vsOrigImageNames[nImageIdx]!=std::string());
            CV_Assert(!m_sResultNameSuffix.empty());
            std::stringstream sResultFilePath;
            sResultFilePath << m_sResultsPath << m_sResultNamePrefix << m_vsOrigImageNames[nImageIdx] << m_sResultNameSuffix;
            cv::Mat oImage = cv::imread(sResultFilePath.str(),m_bForcingGrayscale?cv::IMREAD_GRAYSCALE:cv::IMREAD_COLOR);
            if(m_eDatasetID==eDataset_BSDS500_segm_train || m_eDatasetID==eDataset_BSDS500_segm_train_valid || m_eDatasetID==eDataset_BSDS500_segm_train_valid_test ||
               m_eDatasetID==eDataset_BSDS500_edge_train || m_eDatasetID==eDataset_BSDS500_edge_train_valid || m_eDatasetID==eDataset_BSDS500_edge_train_valid_test) {
                CV_Assert(oImage.size()==cv::Size(481,321) || oImage.size()==cv::Size(321,481));
                CV_Assert(m_voOrigImageSizes[nImageIdx]==cv::Size() || m_voOrigImageSizes[nImageIdx]==oImage.size());
                m_voOrigImageSizes[nImageIdx] = oImage.size();
                if(oImage.size()==cv::Size(321,481))
                    cv::transpose(oImage,oImage);
            }
            return oImage;
        }
        */
        /*
        void litiv::Image::Segm::Set::WriteResult(size_t nImageIdx, const cv::Mat& oResult) {
            CV_Assert(m_vsOrigImageNames[nImageIdx]!=std::string());
            CV_Assert(!m_sResultNameSuffix.empty());
            cv::Mat oImage = oResult;
            if(m_eDatasetID==eDataset_BSDS500_segm_train || m_eDatasetID==eDataset_BSDS500_segm_train_valid || m_eDatasetID==eDataset_BSDS500_segm_train_valid_test ||
               m_eDatasetID==eDataset_BSDS500_edge_train || m_eDatasetID==eDataset_BSDS500_edge_train_valid || m_eDatasetID==eDataset_BSDS500_edge_train_valid_test) {
                CV_Assert(oImage.size()==cv::Size(481,321) || oImage.size()==cv::Size(321,481));
                CV_Assert(m_voOrigImageSizes[nImageIdx]==cv::Size(481,321) || m_voOrigImageSizes[nImageIdx]==cv::Size(321,481));
                if(m_voOrigImageSizes[nImageIdx]==cv::Size(321,481))
                    cv::transpose(oImage,oImage);
            }
            std::stringstream sResultFilePath;
            sResultFilePath << m_sResultsPath << m_sResultNamePrefix << m_vsOrigImageNames[nImageIdx] << m_sResultNameSuffix;
            const std::vector<int> vnComprParams = {cv::IMWRITE_PNG_COMPRESSION,9};
            cv::imwrite(sResultFilePath.str(),oImage,vnComprParams);
        }
        */
        /*
        bool litiv::Image::Segm::Set::StartPrecaching(bool bUsingGT, size_t nUnused) {
            return WorkBatch::StartPrecaching(bUsingGT,m_oMaxSize.height*m_oMaxSize.width*(m_nTotImageCount+1)*(m_bForcingGrayscale?1:m_bForcing4ByteDataAlign?4:3));
        }
        */
        cv::Mat oFrame;
        auto res = m_mTestGTIndexes.find(nFrameIdx);
        if(res!=m_mTestGTIndexes.end()) {
            oFrame = cv::imread(m_vsGTFramePaths[res->second],cv::IMREAD_GRAYSCALE);
            if(oFrame.size()!=m_oSize)
                cv::resize(oFrame,oFrame,m_oSize,0,0,cv::INTER_NEAREST);
        }
        else
            oFrame = cv::Mat();//cv::Mat(m_oSize,CV_8UC1,cv::Scalar_<uchar>(DATASETUTILS_OUTOFSCOPE_VAL));
        return oFrame;
    }
};
