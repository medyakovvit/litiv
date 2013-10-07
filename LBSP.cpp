#include "LBSP.h"

LBSP::LBSP(uchar nThreshold)
	:	 m_bUseRelativeThreshold(false)
		,m_fThreshold(0) // unused
		,m_nThreshold(nThreshold)
		,m_oRefImage() {}

LBSP::LBSP(float fThreshold)
	:	 m_bUseRelativeThreshold(true)
		,m_fThreshold(fThreshold)
		,m_nThreshold(0) // unused
		,m_oRefImage() {
	CV_Assert(m_fThreshold>=0);
}

LBSP::~LBSP() {}

void LBSP::read(const cv::FileNode& /*fn*/) {
    // ... = fn["..."];
}

void LBSP::write(cv::FileStorage& /*fs*/) const {
    //fs << "..." << ...;
}

void LBSP::setReference(const cv::Mat& img) {
	CV_DbgAssert(img.empty() || img.type()==CV_8UC1 || img.type()==CV_8UC3);
	m_oRefImage = img;
}

int LBSP::descriptorSize() const {
	return DESC_SIZE;
}

int LBSP::descriptorType() const {
	return CV_16U;
}

bool LBSP::isUsingRelThreshold() const {
	return m_bUseRelativeThreshold;
}

float LBSP::getRelThreshold() const {
	return m_fThreshold;
}

uchar LBSP::getAbsThreshold() const {
	return m_nThreshold;
}

static inline void lbsp_computeImpl(	const cv::Mat& oInputImg,
										const cv::Mat& oRefImg,
										const std::vector<cv::KeyPoint>& voKeyPoints,
										cv::Mat& oDesc,
										uchar _t) {
	CV_DbgAssert(oRefImg.empty() || (oRefImg.size==oInputImg.size && oRefImg.type()==oInputImg.type()));
	CV_DbgAssert(oInputImg.type()==CV_8UC1 || oInputImg.type()==CV_8UC3);
	CV_DbgAssert(LBSP::DESC_SIZE==2); // @@@ also relies on a constant desc size
	const int nChannels = oInputImg.channels();
	const int _step_row = oInputImg.step.p[0];
	const uchar* _data = oInputImg.data;
	const uchar* _refdata = oRefImg.empty()?oInputImg.data:oRefImg.data;
	const int nKeyPoints = (int)voKeyPoints.size();
	if(nChannels==1) {
		oDesc.create(nKeyPoints,1,CV_16UC1);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar _ref = _refdata[_step_row*(_y)+_x];
			ushort& _res = oDesc.at<ushort>(k);
			#include "LBSP_16bits_dbcross_1ch.i"
		}
	}
	else { //nChannels==3
		oDesc.create(nKeyPoints,1,CV_16UC3);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar* _ref = _refdata+_step_row*(_y)+3*(_x);
			ushort* _res = ((ushort*)(oDesc.data + oDesc.step.p[0]*k));
			#include "LBSP_16bits_dbcross_3ch1t.i"
		}
	}
}

static inline void lbsp_computeImpl(	const cv::Mat& oInputImg,
										const cv::Mat& oRefImg,
										const std::vector<cv::KeyPoint>& voKeyPoints,
										cv::Mat& oDesc,
										float fThreshold) {
	CV_DbgAssert(oRefImg.empty() || (oRefImg.size==oInputImg.size && oRefImg.type()==oInputImg.type()));
	CV_DbgAssert(oInputImg.type()==CV_8UC1 || oInputImg.type()==CV_8UC3);
	CV_DbgAssert(LBSP::DESC_SIZE==2); // @@@ also relies on a constant desc size
	CV_DbgAssert(fThreshold>=0);
	const int nChannels = oInputImg.channels();
	const int _step_row = oInputImg.step.p[0];
	const uchar* _data = oInputImg.data;
	const uchar* _refdata = oRefImg.empty()?oInputImg.data:oRefImg.data;
	const int nKeyPoints = (int)voKeyPoints.size();
	if(nChannels==1) {
		oDesc.create(nKeyPoints,1,CV_16UC1);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar _ref = _refdata[_step_row*(_y)+_x];
			ushort& _res = oDesc.at<ushort>(k);
			const uchar _t = (uchar)(_ref*fThreshold);
			#include "LBSP_16bits_dbcross_1ch.i"
		}
	}
	else { //nChannels==3
		oDesc.create(nKeyPoints,1,CV_16UC3);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar* _ref = _refdata+_step_row*(_y)+3*(_x);
			ushort* _res = ((ushort*)(oDesc.data + oDesc.step.p[0]*k));
			const uchar _t[3] = {(uchar)(_ref[0]*fThreshold),(uchar)(_ref[1]*fThreshold),(uchar)(_ref[2]*fThreshold)};
			#include "LBSP_16bits_dbcross_3ch3t.i"
		}
	}
}

static inline void lbsp_computeImpl2(	const cv::Mat& oInputImg,
										const cv::Mat& oRefImg,
										const std::vector<cv::KeyPoint>& voKeyPoints,
										cv::Mat& oDesc,
										uchar _t) {
	CV_DbgAssert(oRefImg.empty() || (oRefImg.size==oInputImg.size && oRefImg.type()==oInputImg.type()));
	CV_DbgAssert(oInputImg.type()==CV_8UC1 || oInputImg.type()==CV_8UC3);
	CV_DbgAssert(LBSP::DESC_SIZE==2); // @@@ also relies on a constant desc size
	const int nChannels = oInputImg.channels();
	const int _step_row = oInputImg.step.p[0];
	const uchar* _data = oInputImg.data;
	const uchar* _refdata = oRefImg.empty()?oInputImg.data:oRefImg.data;
	const int nKeyPoints = (int)voKeyPoints.size();
	if(nChannels==1) {
		oDesc.create(oInputImg.size(),CV_16UC1);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar _ref = _refdata[_step_row*(_y)+_x];
			ushort& _res = oDesc.at<ushort>(_y,_x);
			#include "LBSP_16bits_dbcross_1ch.i"
		}
	}
	else { //nChannels==3
		oDesc.create(oInputImg.size(),CV_16UC3);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar* _ref = _refdata+_step_row*(_y)+3*(_x);
			ushort* _res = ((ushort*)(oDesc.data + oDesc.step.p[0]*_y + oDesc.step.p[1]*_x));
			#include "LBSP_16bits_dbcross_3ch1t.i"
		}
	}
}

static inline void lbsp_computeImpl2(	const cv::Mat& oInputImg,
										const cv::Mat& oRefImg,
										const std::vector<cv::KeyPoint>& voKeyPoints,
										cv::Mat& oDesc,
										float fThreshold) {
	CV_DbgAssert(oRefImg.empty() || (oRefImg.size==oInputImg.size && oRefImg.type()==oInputImg.type()));
	CV_DbgAssert(oInputImg.type()==CV_8UC1 || oInputImg.type()==CV_8UC3);
	CV_DbgAssert(LBSP::DESC_SIZE==2); // @@@ also relies on a constant desc size
	CV_DbgAssert(fThreshold>=0);
	const int nChannels = oInputImg.channels();
	const int _step_row = oInputImg.step.p[0];
	const uchar* _data = oInputImg.data;
	const uchar* _refdata = oRefImg.empty()?oInputImg.data:oRefImg.data;
	const int nKeyPoints = (int)voKeyPoints.size();
	if(nChannels==1) {
		oDesc.create(oInputImg.size(),CV_16UC1);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar _ref = _refdata[_step_row*(_y)+_x];
			ushort& _res = oDesc.at<ushort>(_y,_x);
			const uchar _t = (uchar)(_ref*fThreshold);
			#include "LBSP_16bits_dbcross_1ch.i"
		}
	}
	else { //nChannels==3
		oDesc.create(oInputImg.size(),CV_16UC3);
		for(int k=0; k<nKeyPoints; ++k) {
			const int _x = (int)voKeyPoints[k].pt.x;
			const int _y = (int)voKeyPoints[k].pt.y;
			const uchar* _ref = _refdata+_step_row*(_y)+3*(_x);
			ushort* _res = ((ushort*)(oDesc.data + oDesc.step.p[0]*_y + oDesc.step.p[1]*_x));
			const uchar _t[3] = {(uchar)(_ref[0]*fThreshold),(uchar)(_ref[1]*fThreshold),(uchar)(_ref[2]*fThreshold)};
			#include "LBSP_16bits_dbcross_3ch3t.i"
		}
	}
}

void LBSP::compute2(const cv::Mat& oImage, std::vector<cv::KeyPoint>& voKeypoints, cv::Mat& oDescriptors) const {
    if(oImage.empty() || voKeypoints.empty()) {
        oDescriptors.release();
        return;
    }
#if LBSP_VALIDATE_KEYPOINTS_INTERNALLY
    cv::KeyPointsFilter::runByImageBorder(keypoints,image.size(),PATCH_SIZE/2);
    cv::KeyPointsFilter::runByKeypointSize(keypoints,std::numeric_limits<float>::epsilon());
	CV_DbgAssert(!keypoints.empty());
#endif //LBSP_VALIDATE_KEYPOINTS_INTERNALLY
	if(m_bUseRelativeThreshold)
		lbsp_computeImpl2(oImage,m_oRefImage,voKeypoints,oDescriptors,m_fThreshold);
	else
		lbsp_computeImpl2(oImage,m_oRefImage,voKeypoints,oDescriptors,m_nThreshold);
}

void LBSP::compute2(const std::vector<cv::Mat>& voImageCollection, std::vector<std::vector<cv::KeyPoint> >& vvoPointCollection, std::vector<cv::Mat>& voDescCollection) const {
    CV_Assert(voImageCollection.size() == vvoPointCollection.size());
    voDescCollection.resize(voImageCollection.size());
    for(size_t i=0; i<voImageCollection.size(); i++)
        compute2(voImageCollection[i], vvoPointCollection[i], voDescCollection[i]);
}

void LBSP::computeImpl(const cv::Mat& oImage, std::vector<cv::KeyPoint>& voKeypoints, cv::Mat& oDescriptors) const {
	if(oImage.empty() || voKeypoints.empty()) {
		oDescriptors.release();
		return;
	}
#if LBSP_VALIDATE_KEYPOINTS_INTERNALLY
	cv::KeyPointsFilter::runByImageBorder(voKeypoints,oImage.size(),PATCH_SIZE/2);
	CV_DbgAssert(!voKeypoints.empty());
#endif //LBSP_VALIDATE_KEYPOINTS_INTERNALLY
	if(m_bUseRelativeThreshold)
		lbsp_computeImpl(oImage,m_oRefImage,voKeypoints,oDescriptors,m_fThreshold);
	else
		lbsp_computeImpl(oImage,m_oRefImage,voKeypoints,oDescriptors,m_nThreshold);
}

void LBSP::reshapeDesc(cv::Size oSize, const std::vector<cv::KeyPoint>& voKeypoints, const cv::Mat& oDescriptors, cv::Mat& oOutput) {
	CV_DbgAssert(!voKeypoints.empty());
	CV_DbgAssert(!oDescriptors.empty() && oDescriptors.cols==1);
	CV_DbgAssert(oSize.width>0 && oSize.height>0);
	CV_DbgAssert(DESC_SIZE==2); // @@@ also relies on a constant desc size
	CV_DbgAssert(oDescriptors.type()==CV_16UC1 || oDescriptors.type()==CV_16UC3);
	const int nChannels = oDescriptors.channels();
	const int nKeyPoints = (int)voKeypoints.size();
	if(nChannels==1) {
		oOutput.create(oSize,CV_16UC1);
		oOutput = cv::Scalar_<ushort>(0);
		for(int k=0; k<nKeyPoints; ++k)
			oOutput.at<ushort>(voKeypoints[k].pt) = oDescriptors.at<ushort>(k);
	}
	else { //nChannels==3
		oOutput.create(oSize,CV_16UC3);
		oOutput = cv::Scalar_<ushort>(0,0,0);
		for(int k=0; k<nKeyPoints; ++k) {
			ushort* output_ptr = (ushort*)(oOutput.data + oOutput.step.p[0]*(int)voKeypoints[k].pt.y);
			const ushort* desc_ptr = (ushort*)(oDescriptors.data + oDescriptors.step.p[0]*k);
			const int idx = 3*(int)voKeypoints[k].pt.x;
			for(int n=0; n<3; ++n)
				output_ptr[idx+n] = desc_ptr[n];
		}
	}
}

void LBSP::calcDescImgDiff(const cv::Mat& oDesc1, const cv::Mat& oDesc2, cv::Mat& oOutput) {
	CV_DbgAssert(oDesc1.size()==oDesc2.size() && oDesc1.type()==oDesc2.type());
	CV_DbgAssert(DESC_SIZE==2); // @@@ also relies on a constant desc size
	CV_DbgAssert(oDesc1.type()==CV_16UC1 || oDesc1.type()==CV_16UC3);
	CV_DbgAssert(CV_MAT_DEPTH(oDesc1.type())==CV_16U);
	CV_DbgAssert(DESC_SIZE*8<=UCHAR_MAX);
	CV_DbgAssert(oDesc1.step.p[0]==oDesc2.step.p[0] && oDesc1.step.p[1]==oDesc2.step.p[1]);
	const int nScaleFactor = UCHAR_MAX/(DESC_SIZE*8);
	const int nChannels = CV_MAT_CN(oDesc1.type());
	const int _step_row = oDesc1.step.p[0];
	if(nChannels==1) {
		oOutput.create(oDesc1.size(),CV_8UC1);
		for(int i=0; i<oDesc1.rows; ++i) {
			const int idx = _step_row*i;
			const ushort* desc1_ptr = (ushort*)(oDesc1.data+idx);
			const ushort* desc2_ptr = (ushort*)(oDesc2.data+idx);
			for(int j=0; j<oDesc1.cols; ++j)
				oOutput.at<uchar>(i,j) = nScaleFactor*hdist_ushort_8bitLUT(desc1_ptr[j],desc2_ptr[j]);
		}
	}
	else { //nChannels==3
		oOutput.create(oDesc1.size(),CV_8UC3);
		for(int i=0; i<oDesc1.rows; ++i) {
			const int idx =  _step_row*i;
			const ushort* desc1_ptr = (ushort*)(oDesc1.data+idx);
			const ushort* desc2_ptr = (ushort*)(oDesc2.data+idx);
			uchar* output_ptr = oOutput.data + oOutput.step.p[0]*i;
			for(int j=0; j<oDesc1.cols; ++j) {
				for(int n=0;n<3; ++n) {
					const int idx2 = 3*j+n;
					output_ptr[idx2] = nScaleFactor*hdist_ushort_8bitLUT(desc1_ptr[idx2],desc2_ptr[idx2]);
				}
			}
		}
	}
}

void LBSP::validateKeyPoints(std::vector<cv::KeyPoint>& voKeypoints, cv::Size oImgSize) {
	cv::KeyPointsFilter::runByImageBorder(voKeypoints,oImgSize,PATCH_SIZE/2);
}
