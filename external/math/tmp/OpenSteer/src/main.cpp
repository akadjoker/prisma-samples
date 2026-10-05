
#include "OpenSteer/OpenSteerDemo.h"        
#include "OpenSteer/Draw.h"                 

#include <cstdlib>

int main (int argc, char **argv) 
{

    OpenSteer::OpenSteerDemo::initialize ();

    OpenSteer::initializeGraphics (argc, argv);

    OpenSteer::runGraphics ();  
    return EXIT_SUCCESS;
}