#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <deque>

#include "image.hpp"
#include "support.hpp"
#include "aligner.hpp"
#include "pairwiseVisualAligner.hpp"
#include "stat.hpp"
#include "simpleSphereStitcher.hpp"

using namespace cv;
using namespace std;

#ifndef OPTONAUT_RINGWISE_STREAM_ALIGNMENT_HEADER
#define OPTONAUT_RINGWISE_STREAM_ALIGNMENT_HEADER

namespace optonaut {
	class RingwiseStreamAligner : public Aligner {
	private:
		PairwiseVisualAligner visual;
        vector<vector<ImageP>> rings;
        ImageP last;
        Mat compassDrift;
        double lasty;
        vector<deque<double>> anglesX;
        vector<deque<double>> anglesY;
        
        deque<ImageP> pasts;
        deque<double> sangles;

        int GetRingForImage(const Mat &extrinsics) const {
            return GetRingForImage(extrinsics, rings);
        }
	public:
		RingwiseStreamAligner() : visual(PairwiseVisualAligner::ModeECCAffine), compassDrift(Mat::eye(4, 4, CV_64F)), lasty(0) { }

        bool NeedsImageData() {
            return true;
        }

        void Dispose() {

        }

        static int GetRingForImage(const Mat &extrinsics, const vector<vector<ImageP>> &rings) {
            size_t r = 0;

            for(auto ring : rings) {
                if(abs(GetDistanceY(ring[0]->adjustedExtrinsics, extrinsics)) < M_PI / 8) {
                    break;
                }
                r++;
            } 

            return r;
        }

        static vector<vector<ImageP>> SplitIntoRings(vector<ImageP> &imgs) {
            
            vector<vector<ImageP>> rings;

            for(auto img : imgs) {
                size_t r = GetRingForImage(img->originalExtrinsics, rings);
                if(r >= rings.size()) {
                    rings.push_back(vector<ImageP>());
                }
                rings[r].push_back(img);
            }

            for(size_t i = 0; i < rings.size(); i++) {
                //Remove tiny rings. 
                if(rings[i].size() < 4) {
                    rings.erase(rings.begin() + i);

                    i--;
                }
            }
            return rings;
        }

		void Push(ImageP next) {

            last = next;

            if(next->id % 5 != 0)
                return;

            visual.FindKeyPoints(next);
            //TODO - adjust incoming with memorized compass drift. 
            
            size_t r = GetRingForImage(next->originalExtrinsics);

            cout << "Ring " << r << endl;

            if(r >= rings.size()) {
                rings.push_back(vector<ImageP>());
                anglesX.push_back(deque<double>());
                anglesY.push_back(deque<double>());
            }
            rings[r].push_back(next);

            ImageP closest = NULL;
            double minDist = 100;


            if(0 != r) {
                for(size_t j = 0; j < rings[0].size(); j++) {
                    //TOODO: SELECT BEST IMAGE - SMTH IS WRONG 
                    double dist = abs(GetAngleOfRotation(next->originalExtrinsics, rings[0][j]->adjustedExtrinsics));

                    if(closest == NULL || dist < minDist) {
                        minDist = dist;
                        closest = rings[0][j];
                        cout << next->id << " Selecting closest: " << closest->id << " width dist: " << minDist << endl; 
                        //if(dist < M_PI / 4) {
                        //    MatchInfo* corr = visual.FindCorrespondence(next, closest);
                        //    cout << "Found " << corr->rotation << endl;
                        //}
                    } 
                }
            }
            //lasty = lasty / 2; //exp backoff.  
            if(closest != NULL) {
                MatchInfo* corr = visual.FindCorrespondence(next, closest);
               
                if(corr->valid) { 
                    double dx = corr->homography.at<double>(0, 2);
                    double dy = corr->homography.at<double>(1, 2);
                    double width = next->img.cols;
                    double height = next->img.rows;
                    
                    cout << "dx: " << dx << ", width: " << width << endl; 

                    double hy = next->intrinsics.at<double>(1, 1) / (next->intrinsics.at<double>(1, 2) * 2); 
                    double hx = next->intrinsics.at<double>(0, 0) / (next->intrinsics.at<double>(0, 2) * 2); 

                    assert(dx <= width);
                    assert(dy <= height);

                    double angleY = asin((dx / width) / hx);
                    double angleX = asin((dy / height) / hy);

                    Mat rveca(3, 1, CV_64F);
                    Mat rvecb(3, 1, CV_64F);
                    Mat ror(4, 4, CV_64F);
                    ExtractRotationVector(closest->adjustedExtrinsics, rveca);
                    ExtractRotationVector(next->originalExtrinsics, rvecb);

                    angleX = -(rveca.at<double>(0) - rvecb.at<double>(0) - angleX);
                    angleY = -(rveca.at<double>(1) - rvecb.at<double>(1) - angleY);

                    if(angleX < -M_PI)
                        angleX = M_PI * 2 + angleX;
                    
                    if(angleY < -M_PI)
                        angleY = M_PI * 2 + angleY;


                    Mat rotY(4, 4, CV_64F);

                    anglesX[r].push_back(angleX);
                    anglesY[r].push_back(angleY);


                    cout << "Pushing for correspondance x: " << angleX << ", y: " << angleY << endl;

                    lasty = angleY;
                }
            }
           
            pasts.push_back(next); 
            sangles.push_back(lasty);
            if(sangles.size() > 10) {
                sangles.pop_front();
            }
            if(pasts.size() > 10) {
                pasts.pop_front();
            }
            if(sangles.size() == 10) {
                double avg = Average(sangles, 1.0 / 3.0);
                CreateRotationY(avg, compassDrift);
                pasts.front()->adjustedExtrinsics = compassDrift * pasts.front()->originalExtrinsics;
                pasts.front()->vtag = avg;
            }
        }

		Mat GetCurrentRotation() const {
			return last->originalExtrinsics;
		}

        void Finish() {

        }

        void Postprocess(vector<ImageP> imgs) const {
            for(auto next : imgs) {
                DrawBar(next->img, next->vtag);
            }
            /*for(auto img : imgs) {
                int r = GetRingForImage(img->adjustedExtrinsics);
                if(r != 0) {
                    Mat rx;
                    CreateRotationX(r == 1 ? -0.1 : 0.1, rx);
                    img->adjustedExtrinsics = img->adjustedExtrinsics * rx;
                }
            }*/
            return;
            vector<Mat> avgsX(anglesX.size()); 
            vector<Mat> avgsY(anglesY.size()); 
            
            for(size_t i = 1; i < anglesX.size(); i++) {
                double ax = Average(anglesX[i], 1.0/3.0);
                double ay = Average(anglesY[i], 1.0/3.0);
                cout << "Adjusting ring " << i << " with x: " << ax << ", y: " << ay << endl;
                cout << "Deviations with x: " << Deviation(anglesX[i], 1.0/3.0) << ", y: " << Deviation(anglesY[i], 1.0/3.0) << endl;
                CreateRotationY(ay, avgsY[i]);
                CreateRotationX(ax, avgsX[i]);
            }

            for(auto img : imgs) {
                int r = GetRingForImage(img->adjustedExtrinsics);
                if(r != 0) {
                    img->adjustedExtrinsics = /*avgsX[r] */ avgsY[r] * img->adjustedExtrinsics;
                }
            }
        };
	};
}

#endif
