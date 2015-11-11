#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include "../common/image.hpp"
#include "../common/drawing.hpp"
#include "../math/support.hpp"
#include "../math/projection.hpp"
#include "../math/stat.hpp"
#include "../recorder/exposureCompensator.hpp"
#include "correlation.hpp"

using namespace cv;
using namespace std;
using namespace cv::detail;

#ifndef OPTONAUT_PAIRWISE_CORRELATOR_HEADER
#define OPTONAUT_PAIRWISE_CORRELATOR_HEADER

namespace optonaut {

class CorrelationDiff {
public:
	bool valid;
    Point2f offset;

	CorrelationDiff() : valid(false), offset(0, 0) {}

};

class PairwiseCorrelator {

private:
    static const bool debug = false;
public:
    PairwiseCorrelator(ExposureCompensator&) { }

    CorrelationDiff Match(const InputImageP a, const InputImageP b, double bias, int biasX, double biasWidth) {

        const bool useGradient = false;
        const bool useReduce = true;
        
        CorrelationDiff result;

        Mat ga, gb;

        cvtColor(a->image.data, ga, CV_BGR2GRAY);
        cvtColor(b->image.data, gb, CV_BGR2GRAY); 

        if(useGradient || useReduce) {
            Mat fa(ga.rows / 2, ga.cols / 2, CV_8U);
            Mat fb(gb.rows / 2, gb.cols / 2, CV_8U);
            pyrDown(ga, fa);
            pyrDown(gb, fb);
            //GetGradient(fa, ga, 1, 0);
            //GetGradient(fb, gb, 1, 0);
            ga = fa;
            gb = fb;
        }
       
        //Todo: Vertical Overlap factor of 0.09 is probably related to FOV
        assert(false); //TODO - new correlator model
        GetOverlappingRegion(a, b, ga, gb, ga, gb);
        
        //If those asserts fire, we've fed the aligner two non-overlapping 
        //images probably. 
        if(ga.cols < 1 || ga.rows < 1) {
            return result;
        }
        
        Mat corr(0, 0, CV_8U); 
        Mat ca(0, 0, CV_8U);
        Mat cb(0, 0, CV_8U);
        double var = 0;
        
        if(useReduce) {
            //ReduceForCorrelation(ga, ca);
            //ReduceForCorrelation(gb, cb);
            int dx; 
            var = CorrelateX(ga, gb, 0.25, dx, corr, bias, biasX, biasWidth, debug);
            result.offset = Point2f(dx, 0);
            result.valid = var > 50;
            //ga = ca;
            //gb = cb;
        } else {
            const int warp = MOTION_AFFINE;
            Mat affine = Mat::eye(2, 3, CV_32F);

            const int iterations = 100;
            const double eps = 1e-5;

            TermCriteria termination(TermCriteria::COUNT + TermCriteria::EPS, iterations, eps);
            try {
                findTransformECC(ga, gb, affine, warp, termination);
                result.offset = Point2f(affine.at<float>(0, 2), affine.at<float>(1, 2));
                result.valid = true;
            } catch (Exception ex) {
                result.valid = false;
            }
        }


        //debug - draw resulting image. 
        if(debug) {
            Mat eye = Mat::eye(3, 3, CV_64F);
            Mat hom = Mat::eye(3, 3, CV_64F);
            hom.at<double>(0, 2) = result.offset.x;
            hom.at<double>(1, 2) = result.offset.y;
            Mat target(ga.rows, ga.cols + gb.cols, CV_8UC3);

            vector<DMatch> dummy;

            DrawMatchingResults(hom, eye, dummy, ga, ImageFeatures(), gb, ImageFeatures(), target);
            std::string filename =  
                    "dbg/ecc_result" + ToString(a->id) + 
                    " " + ToString(b->id) + 
                    " x-corr " + ToString(result.offset.x) + 
                    " var " + ToString(var) + 
                    " .jpg";
            if(useReduce) {
                cvtColor(corr, corr, CV_GRAY2BGR);
                corr = corr(cv::Rect(0, 0, corr.cols, std::min<int>(corr.rows, target.rows)));
                corr.copyTo(target(cv::Rect((target.cols - corr.cols) / 2, 0, corr.cols, corr.rows)));
                //cvtColor(ca, ca, CV_GRAY2BGR);
                //ca.copyTo(target(Rect(0, 0, ca.cols, ca.rows))); 
                //cvtColor(cb, cb, CV_GRAY2BGR);
                //cb.copyTo(target(Rect(ca.cols, 0, ca.cols, ca.rows))); 
            }
            imwrite(filename, target);
        }

        return result;
    }
};
}

#endif
