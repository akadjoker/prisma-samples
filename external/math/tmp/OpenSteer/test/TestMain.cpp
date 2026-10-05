
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include "OpenSteer/UnusedParameter.h"

int main( int argc, char* argv[] ) 
{
    OPENSTEER_UNUSED_PARAMETER(argc);
    OPENSTEER_UNUSED_PARAMETER(argv);

    CppUnit::TextUi::TestRunner test_runner;
    CppUnit::TestFactoryRegistry& test_factory_registry = CppUnit::TestFactoryRegistry::getRegistry();

    test_runner.addTest( test_factory_registry.makeTest() );
    bool successful_test = test_runner.run();

    return successful_test;
}