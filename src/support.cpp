#include <algorithm>
#include <string>
#include "support.hpp"
#include "quat.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

namespace optonaut {

	bool MatIs(const Mat &in, int rows, int cols, int type) {
		return in.rows >= rows && in.cols >= cols && in.type() == type;
	}

    int ParseInt(const string &data) {
        return ParseInt(data.c_str());
    }

	int ParseInt(const char* data) {
		int val;
		istringstream text(data);
		text >> val;
		return val;
	}

	void ScaleIntrinsicsToImage(Mat intrinsics, Mat image, Mat &scaled, double fupscaling) {
		assert(MatIs(intrinsics, 3, 3, CV_64F));

		scaled = Mat::zeros(3, 3, CV_64F);
		
		double scaleFactor = image.cols / (intrinsics.at<double>(0, 2) * 2);
		scaled.at<double>(0, 2) = image.cols / 2;
		scaled.at<double>(1, 2) = image.rows / 2;
		//Todo: Remove factor 10 - only for debug. 
		scaled.at<double>(0, 0) = intrinsics.at<double>(0, 0) * scaleFactor * fupscaling;
		scaled.at<double>(1, 1) = intrinsics.at<double>(1, 1) * scaleFactor * fupscaling;
		scaled.at<double>(2, 2) = 1;
	}

	double GetHorizontalFov(const Mat &intrinsics) {
		assert(MatIs(intrinsics, 3, 3, CV_64F));

		double w = intrinsics.at<double>(0, 2);
		double f = intrinsics.at<double>(0, 0);

		return 2 * atan2(w, f);
	}

	double GetVerticalFov(const Mat &intrinsics) {
		assert(MatIs(intrinsics, 3, 3, CV_64F));

		double h = intrinsics.at<double>(1, 2);
		double f = intrinsics.at<double>(0, 0);

		return 2 * atan2(h, f);
	}
    
    double IsPortrait(const Mat &intrinsics) {
        assert(MatIs(intrinsics, 3, 3, CV_64F));
        
        double h = intrinsics.at<double>(1, 2);
        double w = intrinsics.at<double>(0, 2);
        
        return h > w;
    }

	void ExtractRotationVector(const Mat &r, Mat &v) {
		assert(MatIs(r, 3, 3, CV_64F));

		Mat vec = Mat::zeros(3, 1, CV_64F);

		vec.at<double>(0, 0) = atan2(r.at<double>(2, 1), r.at<double>(2, 2));
		vec.at<double>(1, 0) = atan2(-r.at<double>(2, 0), sqrt(r.at<double>(2, 1) * r.at<double>(2, 1) + r.at<double>(2, 2) * r.at<double>(2, 2)));
		vec.at<double>(2, 0) = atan2(r.at<double>(1, 0), r.at<double>(0, 0));

		v = vec.clone();
	}

	void CreateRotationZ(double a, Mat &t) {
		double v[] = {
			cos(a), -sin(a), 0, 0,
			sin(a), cos(a),  0, 0,
			0, 	    0,       1, 0,
			0,      0,       0, 1 
		};
		Mat rot(4, 4, CV_64F, v);

		t = rot.clone();
	}

	void CreateRotationX(double a, Mat &t) {
		double v[] = {
			1, 0,      0,       0,
			0, cos(a), -sin(a), 0,
			0, sin(a), cos(a),  0,
			0, 0,      0,       1 
		};
		Mat rot(4, 4, CV_64F, v);

		t = rot.clone();
	}

	void CreateRotationY(double a, Mat &t) {
		double v[] = {
			cos(a),  0, sin(a), 0,
			0, 	     1, 0,      0,
			-sin(a), 0, cos(a), 0,
			0,       0, 0,      1 
		};
		Mat rot(4, 4, CV_64F, v);

		t = rot.clone();
	}
    
    void Slerp(const Mat &a, const Mat &b, const double t, Mat &out) {
        Mat q(4, 1, CV_64F);
        Mat r(4, 1, CV_64F);
        quat::FromMat(a.inv() * b, q);
        quat::Mult(q, t, r);
        quat::ToMat(r, out);
    }

	double GetAngleOfRotation(const Mat &r) {
		assert(MatIs(r, 3, 3, CV_64F));
		double t = r.at<double>(0, 0) + r.at<double>(1, 1) + r.at<double>(2, 2);
		return acos((t - 1) / 2);
	}
    
    double GetAngleOfRotation(const Mat &a, const Mat &b) {
        return GetAngleOfRotation(a.inv() * b);
    }
    
    void GetDistanceVector(const Mat &a, const Mat &b, Mat &vec) {
        assert(MatIs(vec, 3, 1, CV_64F));
        
        for(int i = 0; i < 3; i++)
            vec.at<double>(i) = GetDistanceByDimension(a, b, i);
    }

	double GetDistanceByDimension(const Mat &a, const Mat &b, int dim) {
		assert(MatIs(a, 4, 4, CV_64F));
		assert(MatIs(b, 4, 4, CV_64F));
        assert(dim < 3 && dim >= 0);
		double dist = 0;

	    double vdata[] = {0, 0, 0, 0};
        vdata[dim] = 1;
	    Mat vec(4, 1, CV_64F, vdata);

	    Mat aproj = a * vec;
	    Mat bproj = b * vec;

	    dist = aproj.at<double>(dim) - bproj.at<double>(dim);
	    dist = asin(dist); 
	    return dist;
	}

	double GetDistanceX(const Mat &a, const Mat &b) {
	    return GetDistanceByDimension(a, b, 0);
	}

	double GetDistanceY(const Mat &a, const Mat &b) {
	    return GetDistanceByDimension(a, b, 1);
	}

	double GetDistanceZ(const Mat &a, const Mat &b) {
	    return GetDistanceByDimension(a, b, 2);
	}

	//TODO: Cleanup conversion mess. 

	void From4DoubleTo3Float(const Mat &in, Mat &out) {
		assert(MatIs(in, 4, 4, CV_64F));

		out = Mat::zeros(3, 3, CV_32F);
		for(int i = 0; i < 3; i++) {
			for(int j = 0; j < 3; j++) {
				out.at<float>(i, j) = (float)in.at<double>(i, j);
			}
		}
	}
	void From4DoubleTo3Double(const Mat &in, Mat &out) {
		assert(MatIs(in, 4, 4, CV_64F));

		out = Mat::zeros(3, 3, CV_64F);
		for(int i = 0; i < 3; i++) {
			for(int j = 0; j < 3; j++) {
				out.at<double>(i, j) = in.at<double>(i, j);
			}
		}
	}
	void From3DoubleTo3Float(const Mat &in, Mat &out) {
		assert(MatIs(in, 3, 3, CV_64F));

		out = Mat::zeros(3, 3, CV_32F);
		for(int i = 0; i < 3; i++) {
			for(int j = 0; j < 3; j++) {
				out.at<float>(i, j) = (float)in.at<double>(i, j);
			}
		}
	}
	void From3FloatTo4Double(const Mat &in, Mat &out) {
		assert(MatIs(in, 3, 3, CV_32F));

		out = Mat::zeros(4, 4, CV_64F);
		for(int i = 0; i < 3; i++) {
			for(int j = 0; j < 3; j++) {
				out.at<double>(i, j) = (double)in.at<float>(i, j);
			}
		}
		out.at<double>(3, 3) = 1;
	}

	bool ContainsNaN(const Mat &in) {
		assert(in.type() == CV_64F);
		for(int i = 0; i < in.rows; i++) {
			for(int j = 0; j < in.cols; j++) {
				if(in.at<double>(i, j) != in.at<double>(i, j)) {
					return true;
				}
			}
		}
		return false;
	}


	void From3DoubleTo4Double(const Mat &in, Mat &out) {
		assert(MatIs(in, 3, 3, CV_64F));

		out = Mat::zeros(4, 4, CV_64F);
		for(int i = 0; i < 3; i++) {
			for(int j = 0; j < 3; j++) {
				out.at<double>(i, j) = in.at<double>(i, j);
			}
		}
		out.at<double>(3, 3) = 1;
	}

	double min2(double a, double b) {
		return a > b ? b : a;
	}

	double max2(double a, double b) {
		return a < b ? b : a;
	}

	double min4(double a, double b, double c, double d) {
		return min2(min2(a, b), min2(c, d));
	}

	double max4(double a, double b, double c, double d) {
		return max2(max2(a, b), max2(c, d));
	}

	double angleAvg(double x, double y) {
		double r = (((x + 2 * M_PI) + (y + 2 * M_PI)) / 2);

		while(r >= 2 * M_PI) {
			r -= 2 * M_PI;
		}

		return r;
	}

	double interpolate(double x, double x1, double x2, double y1, double y2) {
		return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
	}
}
