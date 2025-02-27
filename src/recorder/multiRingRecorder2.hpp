#include "../common/sink.hpp"
#include "../common/asyncQueueWorker.hpp"
#include "recorderGraphGenerator.hpp"
#include "stereoGenerator.hpp"
#include "imageReselector.hpp"
#include "imageCorrespondenceFinderMultiWrapper.hpp"
#include "asyncTolerantRingRecorder.hpp"
#include "imageSelector.hpp"
#include "imageLoader.hpp"
#include "recorder2.hpp"
#include "coordinateConverter.hpp"
#include "debugSink.hpp"
#include "storageImageSink.hpp"
#include "recorderParamInfo.hpp"

#ifndef OPTONAUT_MOTOR_CONTROL_RECORDER_HEADER
#define OPTONAUT_MOTOR_CONTROL_RECORDER_HEADER

namespace optonaut {

class MultiRingRecorder {

    private:
        const Mat zeroWithoutBase;
        const Mat base;
        const Mat intrinsics;

        const RecorderGraphGenerator generator;
        RecorderGraph graph;
    
        // order of operations, read from bottom to top.
        // Storage of final results
        StorageImageSink &leftSink;
        StorageImageSink &rightSink;
        // Creates stereo images
        StereoGenerator stereoGenerator;
        // Decoupling 
        AsyncSink<SelectionInfo> asyncQueue;
        // Re-Selection
        TrivialSelector reselector;
        // Adjust intrinsics by measuring center ring
        ImageCorrespondenceFinderWrapper adjuster;
        // Decouples slow correspondence finiding process from UI
        AsyncSink<SelectionInfo> decoupler;
        // Selects good images
        FeedbackImageSelector selector;
        // Writes debug images, if necassary.
        DebugSink debugger;
        // Loads the image from the data ref
        ImageLoader loader;
        // Converts input data to stitcher coord frame
        CoordinateConverter converter;

    public:
        MultiRingRecorder(const Mat &_base, const Mat &_zeroWithoutBase,
                  const Mat &_intrinsics,
                  StorageImageSink &_leftSink,
                  StorageImageSink &_rightSink,
                  const int graphConfig = RecorderGraph::ModeAll,
                  const double tolerance = 1.0,
                  const std::string debugPath = "",
                  const RecorderParamInfo paramInfo = RecorderParamInfo()) :
            zeroWithoutBase(_zeroWithoutBase),
            base(_base),
            intrinsics(_intrinsics),
            generator(),
            graph(generator.Generate(
                 intrinsics,
                 graphConfig,
                 paramInfo.halfGraph
                 ? RecorderGraph::DensityNormal
                 : RecorderGraph::DensityDouble,
                 0, 8, paramInfo.graphHOverlap, paramInfo.graphVOverlap)),
            leftSink(_leftSink),
            rightSink(_rightSink),
            stereoGenerator(leftSink, rightSink, graph, paramInfo.stereoHBuffer, paramInfo.stereoVBuffer), 
            asyncQueue(stereoGenerator, false),
            reselector(asyncQueue, graph),
            adjuster(reselector, graph),
            decoupler(adjuster, true),
            selector(graph, decoupler,
                Vec3d(
                    M_PI / 64 * tolerance,
                    M_PI / 128 * tolerance,
                      M_PI / 16 * tolerance)),
            debugger(debugPath, debugPath.size() == 0, selector),
            loader(debugger),
            converter(base, zeroWithoutBase, loader)
        {
            // We only need the center ring (and a bit more)
            size_t imagesCount = graph.GetRings()[1].size();

            AssertNEQM(graphConfig, RecorderGraph::ModeCenter, "Using multi-ring recorder for center ring only. Thats not efficient.");

            AssertEQM(UseSomeMemory(1280, 720, imagesCount), imagesCount,
                    "Successfully pre-allocate memory");
        }

        virtual void Push(InputImageP image) {
            Log << "Received Image. ";
            AssertM(!selector.IsFinished(), "Warning: Push after finish - this is probably a racing condition");

            converter.Push(image);
        }

        virtual void Finish() {
            converter.Finish(); // Two calls to finish, because decoupler intercepts finish.
            adjuster.Finish();
            leftSink.SaveInputSummary(graph);
            rightSink.SaveInputSummary(graph);
        }

        void Cancel() {
            Log << "Cancel, calling converter finish.";
            converter.Finish();
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
            //return halfGraph.GetEdge(a, b, dummy);
            return graph.GetEdge(a, b, dummy);
        }
        vector<SelectionPoint> GetSelectionPoints() const {
            vector<SelectionPoint> converted;
            vector<vector<SelectionPoint>> rings = graph.GetRings();
            vector<vector<SelectionPoint>> orderedRings(rings.size());
            if(rings.size() == 1) {
                orderedRings = rings;
            } else {
                // Hard coded ordering, so we bring all selection points in order.
                // TODO: Careful, this is not compatible with iOS at the moment. We would have
                // to remove the re-ordering there.
                orderedRings[0] = rings[1];
                orderedRings[1] = rings[0];
                orderedRings[2] = rings[2];
            }
            for(auto ring : orderedRings) {
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
