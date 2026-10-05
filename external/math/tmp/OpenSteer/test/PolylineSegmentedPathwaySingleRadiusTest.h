
#ifndef OPENSTEER_POLYLINESEGMENTEDPATHWAYSINGLERADIUSTEST_H
#define OPENSTEER_POLYLINESEGMENTEDPATHWAYSINGLERADIUSTEST_H

#include <memory>

#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestFixture.h>

#include "OpenSteer/PolylineSegmentedPathwaySingleRadius.h"

#include "OpenSteer/StandardTypes.h"

#include "OpenSteer/Vec3.h"

namespace OpenSteer {

    class PolylineSegmentedPathwaySingleRadiusTest : public CppUnit::TestFixture {
    public:
        PolylineSegmentedPathwaySingleRadiusTest();
        virtual ~PolylineSegmentedPathwaySingleRadiusTest();

        virtual void setUp();
        virtual void tearDown();

        CPPUNIT_TEST_SUITE(PolylineSegmentedPathwaySingleRadiusTest);

        CPPUNIT_TEST(testCompareWithOldPathImplementation);
        CPPUNIT_TEST_SUITE_END();

    private:

        PolylineSegmentedPathwaySingleRadiusTest( PolylineSegmentedPathwaySingleRadiusTest const& );

        PolylineSegmentedPathwaySingleRadiusTest& operator=( PolylineSegmentedPathwaySingleRadiusTest );

    private:

        void testCompareWithOldPathImplementation();

        std::auto_ptr< PolylineSegmentedPathwaySingleRadius > path_;
        std::auto_ptr< PolylineSegmentedPathwaySingleRadius > cyclicPath_;
        static size_t const pointCount_ = 4;
        static size_t const cyclicPointCount_ = 5;
        static size_t const segmentCount_;
        static size_t const cyclicSegmentCount_;
        static Vec3 const points_[ pointCount_ ];
        static float const pathLength_;
        static float const cyclicPathLength_;
        static float const radius_;

    }; 

} 

#endif 