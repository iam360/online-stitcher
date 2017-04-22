#include <opencv2/opencv.hpp>
#include "../common/image.hpp"
#include "../common/static_timer.hpp"
#include "../common/progressCallback.hpp"
#include "../io/checkpointStore.hpp"
#include "../io/inputImage.hpp"
#include "../io/io.hpp"
#include "../stereo/monoStitcher.hpp"
#include "../common/ringProcessor.hpp"
#include "../common/functional.hpp"
#include "../recorder/recorderGraph.hpp"


#include "multiringStitcher.hpp"
#include "stitcher.hpp"
#include "../recorder/recorderGraph.hpp"
#include "../recorder/ringCloser.hpp"
#include "../recorder/recorderGraphGenerator.hpp"
#include "../recorder/iterativeBundleAligner.hpp"
#include "../minimal/imagePreperation.hpp"

#include <chrono>
#include <string>


#ifndef OPTONAUT_CONVERT_TO_STEREO_HEADER
#define OPTONAUT_CONVERT_TO_STEREO_HEADER
using namespace cv;
using namespace std;

namespace optonaut {

    /*
     * Wrapper for the optonaut stitching part.
     */
    class ConvertToStereo {

    private:
        static const bool debug = false;
        //static const bool fillMissingImages = true;
        static const bool fillMissingImages = false;

        CheckpointStore &imageStore;
        CheckpointStore &leftStore;
        CheckpointStore &rightStore;
        RecorderGraphGenerator generator;
    public:

        ConvertToStereo(CheckpointStore &imageStore, CheckpointStore &leftStore,
                        CheckpointStore &rightStore ) :
            imageStore(imageStore), leftStore(leftStore), rightStore(rightStore), generator() {
                Log << "Global Alignment init";
                AssertFalseInProduction(debug);
                Log << "Global Alignment init2";

                // If you want to fill missing images, please configure a dynamic path
                // suitable for production in the AddDummyImages method.
                // Then remove this assert.
                AssertFalseInProduction(fillMissingImages);
                Log << "Global Alignment init3";
        }

        void minifyImages(vector<InputImageP> &images, int downsample = 2) {
           AssertGT(downsample, 0);

           int  counter = 0;
           for(auto img : images) {
                std::string source = img->image.source;
                if(!img->image.IsLoaded()) {
                    img->image.Load();
                    AssertM(img->image.data.cols != 0, "Image loaded successfully");
                }

                cv::Mat small;

                pyrDown(img->image.data, small);

                for(int i = 1; i < downsample; i++) {
                    pyrDown(small, small);
                }

                counter++;
                img->image = Image(small);
                img->image.source = source;

            }
        }

        // this is a copy of getVerticalFov
        double GetVerticalFov ( const Mat &intrinsics ) {
             double h = intrinsics.at<double>(1,2);
             double f = intrinsics.at<double>(0,0);
             return 2 * atan2(h,f);
        }


        void Finish() {

            STimer timer;

            Log << "Finish";
            vector<std::vector<InputImageP>> loadedRings;
            //vector<std::vector<InputImageP>> fullImages;
            BiMap<size_t, uint32_t> imagesToTargets, d;
            map<size_t, double> gains;
            MonoStitcher stereoConverter;
            vector<vector<StereoImage>> stereoRings;
            imageStore.LoadStitcherInput(loadedRings, gains);

            Log << "before loaded rings " << loadedRings.size();
            vector<InputImageP> inputImages = fun::flat(loadedRings);
            Log << "after loaded rings" << sizeof(inputImages);
            cv::Size originalSize = inputImages[0]->image.size();
            Log << "after originalSize";

            Mat intrinsics;
            intrinsics = inputImages[0]->intrinsics;
            int graphConfiguration = 0;
            Log << "after graphConfiguration";


            /*
             * check the number of rings and based use the correct graph configuration
             */
            if (loadedRings.size() == 1) {
                Log << "Find center mode";
                graphConfiguration =  RecorderGraph::ModeCenter;
                Log << "Creating centered reorder graph";
            } else if (loadedRings.size() == 3)  {
                Log << "Find truncated mode";
                graphConfiguration =  RecorderGraph::ModeTruncated;
                Log << "Creating truncated reorder graph";
            }

            Log << "Using Intrinsics " << intrinsics;

            RecorderGraph recorderGraph = generator.Generate(intrinsics, graphConfiguration, RecorderGraph::DensityHalf, 0, 8);

            vector<InputImageP> best = recorderGraph.SelectBestMatches(inputImages, imagesToTargets, false);
            Log << "Pre-Alignment, found " << best.size() << "/" << recorderGraph.Size() << "/" << inputImages.size();

            if(debug) {
                SimpleSphereStitcher debugger;
                minimal::ImagePreperation::LoadAllImages(best);
                imwrite("dbg/aligner_input.jpg", debugger.Stitch(best, false, true)->image.data);
            }

            timer.Tick("Init'ed recorder graph and found best matches");

            // preLoad
            static int count = 0;
            auto loadFullImage =
            	[] (const SelectionInfo &img) {
                   // unload the last minified image
                    // this is a cheat. need to find the bug
                    std::string str2 ("debug");
                    std::string str3 ("post");
                    std::size_t found = img.image->image.source.find(str2);
                    if (found!=std::string::npos) {
                        std::cout << "found 'debug' at: " << found << '\n';
                        img.image->image.source.replace(found,5,str3);
                        cout << "image source replace " << img.image->image.source << endl;
                    }



                   if (img.image->image.IsLoaded())
                   		img.image->image.Unload();
                };

            auto ForwardToStereoProcess =
                [&] (const SelectionInfo &a, const SelectionInfo &b) {

         	    StereoImage stereo;
                // TODO: Only enable this if we don't do stereo amplify.
                //SelectionEdge dummy;
                //
                //bool hasEdge = recorderGraph.GetEdge(a.closestPoint, b.closestPoint, dummy);
                //
           	    //AssertWM(hasEdge, "Pair is correctly ordered");
                //
                //if(!hasEdge)
                //    return;

                /*
                 *  Load the original image ( not the minified one )
                 */
                if (!a.image->image.IsLoaded())
                    a.image->image.Load();
                if (!b.image->image.IsLoaded())
                    b.image->image.Load();

                stereoConverter.CreateStereo(a, b, stereo);

                while(stereoRings.size() <= a.closestPoint.ringId) {
                    stereoRings.push_back(vector<StereoImage>());
                }
                stereoRings[a.closestPoint.ringId].push_back(stereo);
                /*
                 * Unload image to save memory
                 */
                if (a.image->image.IsLoaded())
                    a.image->image.Unload();
                if (b.image->image.IsLoaded())
                    b.image->image.Unload();

                leftStore.SaveRectifiedImage(stereo.A);
              	rightStore.SaveRectifiedImage(stereo.B);

                /*
                 * Unload image to save memory
                 */
                stereo.A->image.Unload();
                stereo.B->image.Unload();

                count++;
                if(count % 100 == 0) {
                    cout << "Warning: Input Queue overflow: " <<  count << endl;
                    /*
                     *  Pause for 1 second every 100 images to minimize memory outage
                     */
                    this_thread::sleep_for(chrono::seconds(1));
                }
            };

            Log << "before ring buffer";
            auto FinishImage = [] (const SelectionInfo) { };

            RingProcessor<SelectionInfo> stereoRingBuffer(1, 1, loadFullImage, ForwardToStereoProcess, FinishImage);


       	    int lastRingId = -1;
            for(auto img : best) {
            	SelectionPoint target;
            	//Reassign points
            	uint32_t pointId = 0;
            	Assert(imagesToTargets.GetValue(img->id, pointId));
            	Assert(recorderGraph.GetPointById(pointId, target));
                double maxVFov = GetVerticalFov(img->intrinsics);
                target.vFov = maxVFov;
            	SelectionInfo info;
            	info.isValid = true;
            	info.closestPoint = target;
            	info.image = img;

            	if(lastRingId != -1 && lastRingId != (int)target.ringId) {
                    stereoRingBuffer.Flush();
            	}

            	stereoRingBuffer.Push(info);
            	lastRingId = target.ringId;
            }
            stereoRingBuffer.Flush();

            timer.Tick("Stereo Process");

        	/*
             * push the images to the stores , this will be used for the stitching
             */
            vector<InputImageP> rightImages;
            vector<InputImageP> leftImages;

            for(vector<StereoImage> rings : stereoRings) {
                for(StereoImage stereo : rings) {
                    leftImages.push_back(stereo.A);
                    rightImages.push_back(stereo.B);
                }

                vector<vector<InputImageP>> rightRings =
                    recorderGraph.SplitIntoRings(rightImages);
                vector<vector<InputImageP>> leftRings =
                    recorderGraph.SplitIntoRings(leftImages);

                leftStore.SaveStitcherInput(leftRings, gains );
                rightStore.SaveStitcherInput(rightRings, gains );
       		}

            timer.Tick("Save stitcher input");

        }
   };
}

#endif
