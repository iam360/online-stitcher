#include "../common/sink.hpp"
#include "../common/asyncQueueWorker.hpp"
#include "recorderGraphGenerator.hpp"
#include "stereoGenerator.hpp"
#include "imageReselector.hpp"
#include "imageCorrespondenceFinder.hpp"
#include "asyncTolerantRingRecorder.hpp"
#include "imageSelector.hpp"
#include "imageLoader.hpp"
#include "coordinateConverter.hpp"
#include "debugSink.hpp"

#ifndef OPTONAUT_RECORDER_2_HEADER
#define OPTONAUT_RECORDER_2_HEADER

namespace optonaut {
class SelectionInfoToImageSink : public MapSink<SelectionInfo, InputImageP> {
    public: 
        SelectionInfoToImageSink(Sink<InputImageP> &outSink) : 
            MapSink<SelectionInfo, InputImageP>([](SelectionInfo in) {
            return in.image;
        }, outSink) { }
};

class Recorder2 {

    private:
        const Mat zeroWithoutBase;
        const Mat base;
        const Mat intrinsics;

        const RecorderGraphGenerator generator;
        RecorderGraph graph;
        RecorderGraph halfGraph;
        RecorderGraph previewGraph;
        
        std::vector<Mat> allRotations;

        // order of operations, read from bottom to top. 
        // Stitchers for left and right result.
        AsyncRingStitcher leftStitcher;
        AsyncRingStitcher rightStitcher;
        // Creates stereo images
        StereoGenerator stereoGenerator;
        // Re-selects aligned images according to given graph
        ImageReselector reselector;
        // Finds image correspondence and performs exposure adjusting and alignment
        ImageCorrespondenceFinder adjuster;
        // Collects given input images for the center ring and computes a
        // low-resolution preview image.
        AsyncTolerantRingRecorder previewStitcher;
        // Converts SelectionInfo struct to InputImageP
        SelectionInfoToImageSink selectionToImageConverter;
        // Forwards the given image to previewStitcher AND correspondenceFinder
        TeeSink<SelectionInfo> previewTee;
        // Writes debug images, if necassary.
        DebugSink debugger;
        // Decouples slow correspondence finiding process from UI
        AsyncSink<SelectionInfo> decoupler;
        // Selects good images
        FeedbackImageSelector selector;
        // Loads the image from the data ref
        ImageLoader loader;
        // Converts input data to stitcher coord frame
        CoordinateConverter converter;

    public:
        Recorder2(const Mat &_base, const Mat &_zeroWithoutBase, 
                  const Mat &_intrinsics,
                  const int graphConfig = RecorderGraph::ModeCenter, 
                  const double tolerance = 1.0,
                  const std::string debugPath = "") :
            zeroWithoutBase(_zeroWithoutBase),
            base(_base),
            intrinsics(_intrinsics),
            generator(),
            graph(generator.Generate(
                    intrinsics, 
                    graphConfig, 
                    RecorderGraph::DensityNormal, 
                    0, 8)),
            halfGraph(RecorderGraphGenerator::Sparse(graph, 2)),
            previewGraph(generator.Generate(
                    intrinsics, RecorderGraph::ModeCenter,
                    RecorderGraph::DensityHalf, 0, 8)),
            allRotations(fun::map<SelectionPoint*, Mat>(
               halfGraph.GetTargetsById(), 
               [](const SelectionPoint* x) {
                    return x->extrinsics;
               })), 
            leftStitcher(allRotations, 1200, true),
            rightStitcher(allRotations, 1200, true),
            stereoGenerator(leftStitcher, rightStitcher, halfGraph, adjuster), 
            reselector(stereoGenerator, halfGraph),
            adjuster(reselector, graph),
            previewStitcher(previewGraph, 400, false),
            selectionToImageConverter(previewStitcher),
            previewTee(selectionToImageConverter, adjuster),
            debugger(debugPath, debugPath.size() == 0, previewTee), 
            decoupler(debugger, true),
            selector(graph, decoupler,
                Vec3d(
                    M_PI / 64 * tolerance, 
                    M_PI / 128 * tolerance, 
                    M_PI / 16 * tolerance)),
            loader(selector), 
            converter(base, zeroWithoutBase, loader)
        { 
            size_t imagesCount = graph.Size();

            if(debugPath.size() != 0) {
                Log << "Warning, debug mode activated.";
            }

            AssertEQM(graphConfig, RecorderGraph::ModeCenter, "This recorder instance only supports center ring recording")

            AssertEQM(UseSomeMemory(1280, 720, imagesCount), imagesCount, 
                    "Successfully pre-allocate memory");
        } 

        virtual void Push(InputImageP image) {
            Log << "Received Image. ";
            //Log << image->originalExtrinsics;
            Log << image->image.cols << "x" << image->image.rows;
            //Log << image->intrinsics;
            AssertM(!selector.IsFinished(), "Warning: Push after finish - this is probably a racing condition");
            
            converter.Push(image);
        }

        // This has to be called after GetPreviewImage.
        virtual void Finish() {
            AssertM(previewStitcher.GetResult() != nullptr, "GetPreviewImage must be called before calling Finish");
            adjuster.Finish();
        }

        void Cancel() {
            Log << "Cancel, calling converter finish.";
            converter.Finish();
            Log << "Cancel, calling preview finish.";
            previewStitcher.Finish();
            Log << "Cancel, calling adjuster finish.";
            adjuster.Finish();
        }

        StitchingResultP GetPreviewImage() {
            // Calling finish here circumvents the chaining.
            // It is safe because our AsyncSink decoupler intercepts finish.
            converter.Finish();
            previewStitcher.Finish();
            return previewStitcher.Finalize();
        }

        StitchingResultP GetLeftResult() {
            return leftStitcher.Finalize();
        }

        StitchingResultP GetRightResult() {
            return rightStitcher.Finalize();
        }

        bool RecordingIsFinished() {
            return selector.IsFinished();
        }

        const RecorderGraph& GetRecorderGraph() {
            return graph;
        }
    
        // TODO - rather expose selector
        Mat GetBallPosition() const {
            return converter.ConvertFromStitcher(selector.GetBallPosition());
        }
    
        SelectionInfo GetCurrentKeyframe() const {
            return selector.GetCurrent();
        }
        
        double GetDistanceToBall() const {
            return selector.GetError();
        }
        
        const Mat &GetAngularDistanceToBall() const {
            return selector.GetErrorVector();
        }
    
        bool IsIdle() {
            return selector.IsIdle();
        }
        
        bool HasStarted() {
            return selector.HasStarted();
        }
        
        bool IsFinished() {
            return selector.IsFinished();
        }
    
        void SetIdle(bool isIdle) {
            selector.SetIdle(isIdle);
        }
    
        uint32_t GetImagesToRecordCount() {
            return (uint32_t)selector.GetImagesToRecordCount();
        }
        
        uint32_t GetRecordedImagesCount() {
            return (uint32_t)selector.GetRecordedImagesCount();
        }
    
        // TODO - refactor out
        bool AreAdjacent(SelectionPoint a, SelectionPoint b) {
            SelectionEdge dummy;
            return halfGraph.GetEdge(a, b, dummy);
        }
        vector<SelectionPoint> GetSelectionPoints() const {
            vector<SelectionPoint> converted;
            for(auto ring : graph.GetRings()) {
                ring.push_back(ring.front());
                for(auto point : ring) {
                    SelectionPoint n;
                    n.globalId = point.globalId;
                    n.ringId = point.ringId;
                    n.localId = point.localId;
                    n.extrinsics = converter.ConvertFromStitcher(point.extrinsics);
                    
                    converted.push_back(n);
                }
                
            }
            return converted;
        }
};
}


#endif
