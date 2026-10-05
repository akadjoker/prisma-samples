
#ifndef OPENSTEER_SEGMENTEDPATHALIKEUTILITIES_H
#define OPENSTEER_SEGMENTEDPATHALIKEUTILITIES_H

#include <cassert>

#include "OpenSteer/StandardTypes.h"

namespace OpenSteer {

    template< typename SegmentedPathAlike >
    size_t nextSegment( SegmentedPathAlike const& pathAlike, size_t segmentIndex ) {
        assert( pathAlike.isValid() && "pathAlike isn't valid." );
        assert( segmentIndex < pathAlike.segmentCount() && "segmentIndex out of range." );

        ++segmentIndex;

        if ( segmentIndex == pathAlike.segmentCount() ) {

            if ( pathAlike.isCyclic() ) {
                segmentIndex = 0;
            } else {
                --segmentIndex;
            }
        } 

        return segmentIndex;
    }

    template< typename SegmentedPathAlike >
    size_t previousSegment( SegmentedPathAlike const& pathAlike, size_t segmentIndex ) {
        assert( pathAlike.isValid() && "pathAlike isn't valid." );
        assert( segmentIndex < pathAlike.segmentCount() && "segmentIndex out of range." );

        if ( 0 != segmentIndex ) {
            --segmentIndex;

        } else if ( pathAlike.isCyclic() ) {
            segmentIndex = pathAlike.segmentCount() - 1;
        }

        return segmentIndex;
    }  

} 

#endif 