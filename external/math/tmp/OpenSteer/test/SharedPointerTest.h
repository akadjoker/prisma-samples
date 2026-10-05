
#ifndef OPENSTEER_SHAREDPOINTERTEST_H
#define OPENSTEER_SHAREDPOINTERTEST_H

#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestFixture.h>

#include "OpenSteer/SharedPointer.h"

namespace OpenSteer {

    class SharedPointerTest : public CppUnit::TestFixture {
    public:
        SharedPointerTest();
        virtual ~SharedPointerTest();

        virtual void setUp();
        virtual void tearDown();

        CPPUNIT_TEST_SUITE(SharedPointerTest);
        CPPUNIT_TEST(testConstruction);
        CPPUNIT_TEST(testAssignment);
        CPPUNIT_TEST(testReset);
        CPPUNIT_TEST(testInheritance);
        CPPUNIT_TEST(testComparisons);
        CPPUNIT_TEST(testImplicitBoolCast);
        CPPUNIT_TEST(testSwap);
        CPPUNIT_TEST_SUITE_END();

    private:

        SharedPointerTest( SharedPointerTest const& );

        SharedPointerTest& operator=( SharedPointerTest const& );

    private:

        void testConstruction();

        void testAssignment();

        void testReset();

        void testInheritance();

        void testComparisons();

        void testImplicitBoolCast();

        void testSwap();

    }; 

} 

#endif 