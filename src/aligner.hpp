#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <deque>

#include "image.hpp"
#include "support.hpp"

#ifndef OPTONAUT_ALIGNMENT_HEADER
#define OPTONAUT_ALIGNMENT_HEADER

namespace optonaut {
	class Aligner {
    public:
		virtual void Push(ImageP next) = 0;
		virtual cv::Mat GetCurrentRotation() const = 0;
        virtual void Dispose() = 0; 
        virtual bool NeedsImageData() = 0;
	};
}

#endif
