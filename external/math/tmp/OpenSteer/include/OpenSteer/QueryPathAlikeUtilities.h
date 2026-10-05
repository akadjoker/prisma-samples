
#ifndef OPENSTEER_QUERYPATHALIKEUTILITIES_H
#define OPENSTEER_QUERYPATHALIKEUTILITIES_H

namespace OpenSteer {

    class ExtractPathDistance {
    public:
        void setDistanceOnPathFlag( float distance ) {
            distanceOnPathFlag_ = distance;
        }

        float distanceOnPathFlag() const {
            return distanceOnPathFlag_;
        }

    protected:
        ExtractPathDistance() : distanceOnPathFlag_( 0.0f ) {

        }

        explicit ExtractPathDistance( float distance ) : distanceOnPathFlag_( distance ) {

        }

        ~ExtractPathDistance() {

        }

    private:
        float distanceOnPathFlag_;
    }; 

    class DontExtractPathDistance {
    public:
        void setDistanceOnPathFlag( float ) {

        }

        float distanceOnPathFlag() const {
            return 0.0f;
        };

    protected:
        ~DontExtractPathDistance() {

        }
    }; 

} 

#endif 