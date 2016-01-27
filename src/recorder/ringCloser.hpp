#include "../io/inputImage.hpp"
#include "../imgproc/planarCorrelator.hpp"
#include "../math/support.hpp"

#ifndef OPTONAUT_RING_CLOSER_HEADER
#define OPTONAUT_RING_CLOSER_HEADER

namespace optonaut {
    class RingCloser {

        public: 

        static inline bool CloseRing(std::vector<InputImageP> ring) {
            PairwiseCorrelator corr;

            auto result = corr.Match(ring.front(), ring.back(), 4, 4, true); 

            if(!result.valid) {
                cout << "Ring closure: Rejected." << endl;
                return false;
            }

            if(result.angularOffset.y > 0.2) {
                cout << "Ring closure: Rejected because it would lead to black stripes"<< endl;
                return false;
            }

            cout << "Ring closure: Adjusting by: " << result.angularOffset.y << endl;

            size_t n = ring.size();

            for(size_t i = 0; i < n; i++) {
                double ydiff = result.angularOffset.y * 
                    (1.0 - ((double)i) / ((double)n));
                Mat correction;
                CreateRotationY(ydiff, correction);
                ring[i]->adjustedExtrinsics = correction * 
                    ring[i]->adjustedExtrinsics;
            }
            return true;
        }
    };
}

#endif
