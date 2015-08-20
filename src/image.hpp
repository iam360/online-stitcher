#include <string>
#include <vector>
#include <memory>
#include <opencv2/opencv.hpp>

#ifndef OPTONAUT_IMAGE_HEADER
#define OPTONAUT_IMAGE_HEADER

namespace optonaut {

    namespace colorspace {
        const int RGBA = 0;
        const int RGB = 1;
    }

    struct ImageRef {
        void* data;
        int width;
        int height;
        int colorSpace;

        ImageRef() : data(NULL), width(0), height(0), colorSpace(colorspace::RGBA) { }
    }

	struct Image {
		cv::Mat img;
        ImageRef dataRef;
		cv::Mat extrinsics;
		cv::Mat intrinsics; 
		int id;
		std::string source;

		std::vector<cv::KeyPoint> features;
		cv::Mat descriptors;

        Image()  : img(0, 0, CV_8UC3), extrinsics(4, 4, CV_64F), intrinsics(3, 3, CV_64F), source("Unknown")

        void IsLoaded() {
            return img.cols != 0 && img.rows != 0;
        }

        void LoadFromDataRef() {
            assert(!IsLoaded);
            assert(imageRef.data != NULL);
            assert(imageRef.colorSpace = colorspace::ARGB); 

            cvtColor(Mat(imageRef.height, imageRef.width, CV_8UC4, imageRef.data), img, COLOR_RGBA2RGB);
        }

        void Unload() {
            img = Mat(0, 0, CV_8UC3);
            //Todo: Is this enough to dispose the image?
        }
	};

	typedef std::shared_ptr<Image> ImageP;
}


#endif
