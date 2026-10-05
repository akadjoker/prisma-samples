
#ifndef OPENSTEER_PLUGIN_H
#define OPENSTEER_PLUGIN_H

#include <iostream>
#include "OpenSteer/AbstractVehicle.h"

namespace OpenSteer {

    class AbstractPlugIn
    {
    public:

        virtual ~AbstractPlugIn() {  }

        virtual void open (void) = 0;
        virtual void update (const float currentTime, const float elapsedTime) = 0;
        virtual void redraw (const float currentTime, const float elapsedTime) = 0;
        virtual void close (void) = 0;
        virtual void reset (void) = 0;

        virtual const char* name (void) = 0;

        virtual float selectionOrderSortKey (void) = 0;

        virtual bool requestInitialSelection (void) = 0;

        virtual void handleFunctionKeys (int keyNumber) = 0;

        virtual void printMiniHelpForFunctionKeys (void) = 0;

        virtual const AVGroup& allVehicles (void) = 0;
    };

    class PlugIn : public AbstractPlugIn
    {
    public:

        typedef void (* plugInCallBackFunction) (PlugIn& clientObject);
        typedef void (* voidCallBackFunction) (void);
        typedef void (* timestepCallBackFunction) (const float currentTime,
                                                   const float elapsedTime);

        PlugIn (void);

        virtual ~PlugIn();

        void reset (void) {close (); open ();}

        float selectionOrderSortKey (void) {return 1.0f;}

        bool requestInitialSelection (void) {return false;}

        void handleFunctionKeys (int ) {}

        void printMiniHelpForFunctionKeys (void) {}

        PlugIn* next (void);

        friend std::ostream& operator<< (std::ostream& os, PlugIn& pi)
        {
            os << "<PlugIn " << '"' << pi.name() << '"' << ">";
            return os;
        }

        static PlugIn* findByName (const char* string);

        static void applyToAll (plugInCallBackFunction f);

        static void sortBySelectionOrder (void);

        static PlugIn* findDefault (void);

    private:

        void addToRegistry (void);

        static const int totalSizeOfRegistry;
        static int itemsInRegistry;
        static PlugIn* registry[];
    };

} 

#endif 