
#include "SharedPointerTest.h"

CPPUNIT_TEST_SUITE_REGISTRATION( OpenSteer::SharedPointerTest );

OpenSteer::SharedPointerTest::SharedPointerTest()
{

}

OpenSteer::SharedPointerTest::~SharedPointerTest()
{

}

void 
OpenSteer::SharedPointerTest::setUp()
{
    TestFixture::setUp();
}

void 
OpenSteer::SharedPointerTest::tearDown()
{
    TestFixture::tearDown();
}

namespace {

    template< int i >
    struct SharedPointerTester {

        SharedPointerTester() : counter_( i ) {
            ++static_counter_;
        }

        ~SharedPointerTester() {
            --static_counter_;
        }

        int const counter_;
        static int static_counter_;
    };

    template< int i > int SharedPointerTester<i>::static_counter_ = 0;

} 

void 
OpenSteer::SharedPointerTest::testConstruction()
{

    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    SharedPointerTester< 0 >* rawPointer = new SharedPointerTester< 0 >();

    CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointer< SharedPointerTester< 0 > > sp( rawPointer );
    }

    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    rawPointer = new SharedPointerTester< 0 >();

    {
        SharedPointer< SharedPointerTester< 0 > > sp0( rawPointer );
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT_EQUAL( static_cast<size_t>( 1 ), sp0.useCount() );
        {
                SharedPointer< SharedPointerTester< 0 > > sp1( sp0 );
                CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );
                CPPUNIT_ASSERT_EQUAL( static_cast<size_t>( 2 ), sp0.useCount() );
                CPPUNIT_ASSERT_EQUAL( static_cast<size_t>( 2 ), sp1.useCount() );
                CPPUNIT_ASSERT_EQUAL( rawPointer, sp0.get() );
                CPPUNIT_ASSERT_EQUAL( rawPointer, sp1.get() );
        }
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT_EQUAL( static_cast<size_t>( 1 ), sp0.useCount() );
        CPPUNIT_ASSERT_EQUAL( rawPointer, sp0.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointer< SharedPointerTester< 0 > > sp0;
        CPPUNIT_ASSERT( 0 == sp0.get() );
    }

    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );
}

void 
OpenSteer::SharedPointerTest::testAssignment()
{

    {
        SharedPointer< SharedPointerTester< 0 > > sp0;
        SharedPointer< SharedPointerTester< 0 > > sp1( sp0 );
        CPPUNIT_ASSERT( 0 == sp0.get() );
        CPPUNIT_ASSERT( 0 == sp1.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointerTester< 0 >* rawPointer = new SharedPointerTester< 0 >();
        SharedPointer< SharedPointerTester< 0 > > sp0;
        SharedPointer< SharedPointerTester< 0 > > sp1( rawPointer );
        SharedPointer< SharedPointerTester< 0 > > sp2( sp0 );

        sp0 = sp1;
        CPPUNIT_ASSERT( 2 == sp0.useCount() );
        CPPUNIT_ASSERT( 2 == sp1.useCount() );
        CPPUNIT_ASSERT_EQUAL( rawPointer, sp0.get() );
        CPPUNIT_ASSERT_EQUAL( rawPointer, sp1.get() );
        CPPUNIT_ASSERT( 0 == sp2.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointerTester< 0 >* rawPointer0 = new SharedPointerTester< 0 >();
        SharedPointerTester< 0 >* rawPointer1 = new SharedPointerTester< 0 >();
        CPPUNIT_ASSERT_EQUAL( 2, SharedPointerTester< 0 >::static_counter_ );

        SharedPointer< SharedPointerTester< 0 > > sp0( rawPointer0 );
        SharedPointer< SharedPointerTester< 0 > > sp1( rawPointer1 );
        sp1 = sp0;

        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( 2 == sp0.useCount() );
        CPPUNIT_ASSERT( 2 == sp1.useCount() );
        CPPUNIT_ASSERT_EQUAL( rawPointer0, sp0.get() );
        CPPUNIT_ASSERT_EQUAL( rawPointer0, sp1.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

}

void 
OpenSteer::SharedPointerTest::testReset()
{

    {
        SharedPointer< SharedPointerTester< 0 > > sp0;
        sp0.reset();
        CPPUNIT_ASSERT( 0 == sp0.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointerTester< 0 >* rawPointer0 = new SharedPointerTester< 0 >();
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );

        SharedPointer< SharedPointerTester< 0 > > sp0( rawPointer0 );
        sp0.reset();
        CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( 0 == sp0.get() );

    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointerTester< 0 >* rawPointer0 = new SharedPointerTester< 0 >();
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );

        SharedPointer< SharedPointerTester< 0 > > sp0( rawPointer0 );
        sp0.reset();
        CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( 0 == sp0.get() );

        sp0.reset();
        CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( 0 == sp0.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointerTester< 0 >* rawPointer0 = new SharedPointerTester< 0 >();
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );

        SharedPointer< SharedPointerTester< 0 > > sp0;
        sp0.reset( rawPointer0 );
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( rawPointer0 == sp0.get() );

        sp0.reset();
        CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( 0 == sp0.get() );

    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

    {
        SharedPointerTester< 0 >* rawPointer0 = new SharedPointerTester< 0 >();
        SharedPointerTester< 0 >* rawPointer1 = new SharedPointerTester< 0 >();
        CPPUNIT_ASSERT_EQUAL( 2, SharedPointerTester< 0 >::static_counter_ );

        SharedPointer< SharedPointerTester< 0 > > sp0( rawPointer0 );
        sp0.reset( rawPointer1 );
        CPPUNIT_ASSERT_EQUAL( 1, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( rawPointer1 == sp0.get() );

        sp0.reset();
        CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );
        CPPUNIT_ASSERT( 0 == sp0.get() );
    }
    CPPUNIT_ASSERT_EQUAL( 0, SharedPointerTester< 0 >::static_counter_ );

}

namespace {

    class Super {
    public:

        Super() {
            ++superCount_;
        }

        virtual ~Super() {
            --superCount_;
        }

        static int superCount_; 
    }; 

    int Super::superCount_ = 0;

    class Sub: public Super {
    public:
        Sub(): Super() {
            ++subCount_;
        }

        virtual ~Sub() {
            --subCount_;
        }

        static int subCount_;
    }; 

    int Sub::subCount_ = 0;

} 

void
OpenSteer::SharedPointerTest::testInheritance()
{
    {
        Sub* rawSubPointer0 = new Sub();
        SharedPointer< Super > sp0( rawSubPointer0 );
        CPPUNIT_ASSERT_EQUAL( 1, Super::superCount_ );
    }
    CPPUNIT_ASSERT_EQUAL( 0, Super::superCount_ );

    {
        Super* rawSuperPointer0 = new Super();
        SharedPointer< Super > sp0( rawSuperPointer0 );
        CPPUNIT_ASSERT_EQUAL( 1, Super::superCount_ );
        CPPUNIT_ASSERT_EQUAL( 0, Sub::subCount_ );

        Sub* rawSubPointer0 = new Sub();
        SharedPointer< Sub > sp1( rawSubPointer0 );
        CPPUNIT_ASSERT_EQUAL( 2, Super::superCount_ );
        CPPUNIT_ASSERT_EQUAL( 1, Sub::subCount_ );

        sp0 = sp1;
        CPPUNIT_ASSERT_EQUAL( 1, Super::superCount_ );
        CPPUNIT_ASSERT_EQUAL( 1, Sub::subCount_ );
    }
    CPPUNIT_ASSERT_EQUAL( 0, Super::superCount_ );
    CPPUNIT_ASSERT_EQUAL( 0, Sub::subCount_ );
}

void 
OpenSteer::SharedPointerTest::testComparisons()
{
    Super* rawSuperPointer0 = new Super();
    SharedPointer< Super > sp0( rawSuperPointer0 );

    Sub* rawSubPointer1 = new Sub();
    SharedPointer< Super > sp1( rawSubPointer1 );

    CPPUNIT_ASSERT( sp0 != sp1 );
    CPPUNIT_ASSERT( ( sp0 < sp1 ) || ( sp1 < sp0 ) );

    sp1 = sp0;

    CPPUNIT_ASSERT( sp0 == sp1 );
    CPPUNIT_ASSERT( !( sp0 < sp1 ) && !( sp1 < sp0 ) );

    sp0.reset();
    sp1.reset();

    CPPUNIT_ASSERT( sp0 == sp1 );

    CPPUNIT_ASSERT( ( sp0 < sp1 ) || ( sp1 < sp0 ) );

}

void 
OpenSteer::SharedPointerTest::testImplicitBoolCast()
{
    Super* rawSuperPointer0 = new Super();
    SharedPointer< Super > sp0;

    if ( !sp0 ) {
        CPPUNIT_ASSERT( true );
    } else {
        CPPUNIT_ASSERT( false );
    }

    sp0.reset( rawSuperPointer0 );
    if ( sp0 ) {
        CPPUNIT_ASSERT( true );
    } else {
        CPPUNIT_ASSERT( false );
    }

}

void
OpenSteer::SharedPointerTest::testSwap()
{
    Super* rawSuperPointer0 = new Super();
    SharedPointer< Super > sp0( rawSuperPointer0 );

    Super* rawSuperPointer1 = new Super();
    SharedPointer< Super > sp1( rawSuperPointer1 );

    CPPUNIT_ASSERT_EQUAL( 2, Super::superCount_ );

    sp0.swap( sp1 );

    CPPUNIT_ASSERT( rawSuperPointer0 == sp1.get() );
    CPPUNIT_ASSERT( rawSuperPointer1 == sp0.get() );
    CPPUNIT_ASSERT_EQUAL( 2, Super::superCount_ );

}