
#ifndef OPENSTEER_POLYLINESEGMENTEDPATHTEST_H
#define OPENSTEER_POLYLINESEGMENTEDPATHTEST_H

#include <memory>

#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestFixture.h>

#include "OpenSteer/PolylineSegmentedPath.h"

#include "OpenSteer/StandardTypes.h"

namespace OpenSteer {

    class PolylineSegmentedPathTest : public CppUnit::TestFixture {
    public:
        PolylineSegmentedPathTest();
        virtual ~PolylineSegmentedPathTest();

        virtual void setUp();
        virtual void tearDown();

        CPPUNIT_TEST_SUITE(PolylineSegmentedPathTest);
        CPPUNIT_TEST(testConstruction);
        CPPUNIT_TEST(testAssignment);
        CPPUNIT_TEST(testSegmentData);
        CPPUNIT_TEST(testMovePoints);
        CPPUNIT_TEST(testMovePointsCyclicPath);
        CPPUNIT_TEST(testSegmentMappings);
        CPPUNIT_TEST(testPointToPathMappings);
        CPPUNIT_TEST(testDistanceToPathMappings);
        CPPUNIT_TEST(testCompareWithOldPathImplementation);
        CPPUNIT_TEST_SUITE_END();

    private:

        PolylineSegmentedPathTest( PolylineSegmentedPathTest const& );

        PolylineSegmentedPathTest& operator=( PolylineSegmentedPathTest );

    private:
        void testConstruction();
        void testAssignment();
        void testSegmentData();
        void testMovePoints();
        void testMovePointsCyclicPath();
        void testSegmentMappings();        
        void testPointToPathMappings();
        void testDistanceToPathMappings();
        void testCompareWithOldPathImplementation();

        std::auto_ptr< PolylineSegmentedPath > path_;
        std::auto_ptr< PolylineSegmentedPath > cyclicPath_;
        static size_t const pointCount_ = 4;
        static size_t const cyclicPointCount_ = 5;
        static size_t const segmentCount_;
        static size_t const cyclicSegmentCount_;
        static Vec3 const points_[ pointCount_ ];
        static float const pathLength_;
        static float const cyclicPathLength_;

    }; 

} 

#endif 