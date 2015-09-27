#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#include "image.hpp"
#include "support.hpp"
#include "aligner.hpp"
#include "recorderGraph.hpp"
#include "SequenceStreamAligner.hpp"
#include "asyncStreamWrapper.hpp"

using namespace cv;
using namespace std;

#ifndef OPTONAUT_ASYNC_ALIGNMENT_HEADER
#define OPTONAUT_ASYNC_ALIGNMENT_HEADER

namespace optonaut {
	
    class AsyncAlignerCore {    
        private: 
        SequenceStreamAligner core;
        public:
        
        AsyncAlignerCore(RecorderGraph) : core() { }

        Mat operator()(ImageP in) {
            core.Push(in);
            return core.GetCurrentRotation().clone();
        }
        
        void Postprocess(vector<ImageP> imgs) const { core.Postprocess(imgs); };
        
        void Finish() { core.Finish(); };
    };

	class AsyncAligner : public Aligner {
	private:
        AsyncAlignerCore core;
        AsyncStream<ImageP, Mat> worker;

        Mat sensorDiff;
        Mat lastSensor;
        Mat current;

        bool isInitialized;

	public:
		AsyncAligner(RecorderGraph &graph) : core(graph), worker(ref(core)), sensorDiff(Mat::eye(4, 4, CV_64F)), isInitialized(false){ }
       
        bool NeedsImageData() {
            return worker.Finished();
        }

        void Push(ImageP image) {
            if(!isInitialized) {
                lastSensor = image->originalExtrinsics;
                current = image->originalExtrinsics;
                worker.Push(image);
            }

            if(isInitialized && worker.Finished() && image->IsLoaded()) {
                current = worker.Result() * sensorDiff;
                sensorDiff = Mat::eye(4, 4, CV_64F);
                
                worker.Push(image);
            } else {
                Mat sensorStep = lastSensor.inv() * image->originalExtrinsics;
                sensorDiff = sensorDiff * sensorStep;
                current = current * sensorStep;
                lastSensor = image->originalExtrinsics;
            }
                
            isInitialized = true;
        }

        void Dispose() {
            worker.Dispose();
        }

        Mat GetCurrentRotation() const {
            return current;
        }
        
        void Postprocess(vector<ImageP> imgs) const { core.Postprocess(imgs); };
        void Finish() { core.Finish(); };
    };
}
#endif
