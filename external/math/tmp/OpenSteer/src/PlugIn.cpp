
#include "OpenSteer/PlugIn.h"
#include <cstring>

int OpenSteer::PlugIn::itemsInRegistry = 0;
const int OpenSteer::PlugIn::totalSizeOfRegistry = 1000;
OpenSteer::PlugIn* OpenSteer::PlugIn::registry [totalSizeOfRegistry];

OpenSteer::PlugIn::PlugIn (void)
{

    addToRegistry ();
}

OpenSteer::PlugIn::~PlugIn() {}

OpenSteer::PlugIn* 
OpenSteer::PlugIn::next (void)
{
    for (int i = 0; i < itemsInRegistry; i++)
    {
        if (this == registry[i])
        {
            const bool atEnd = (i == (itemsInRegistry - 1));
            return registry [atEnd ? 0 : i + 1];
        }
    }
    return NULL;
}

OpenSteer::PlugIn* 
OpenSteer::PlugIn::findByName (const char* string)
{
    if (string)
    {
        for (int i = 0; i < itemsInRegistry; i++)
        {
            PlugIn& pi = *registry[i];
            const char* s = pi.name();
            if (s && (std::strcmp (string, s) == 0)) return &pi;
        }
    }
    return NULL;
}

void 
OpenSteer::PlugIn::applyToAll (plugInCallBackFunction f)
{
    for (int i = 0; i < itemsInRegistry; i++)
    {
        f (*registry[i]);
    }
}

void 
OpenSteer::PlugIn::sortBySelectionOrder (void)
{

    for (int i = 0; i < itemsInRegistry-1; i++)
    {

        for (int j = i+1; j < itemsInRegistry; j++)
        {
            const float iKey = registry[i]->selectionOrderSortKey ();
            const float jKey = registry[j]->selectionOrderSortKey ();

            if (iKey > jKey)
            {
                PlugIn* temporary = registry[i];
                registry[i] = registry[j];
                registry[j] = temporary;
            }
        }
    }
}

OpenSteer::PlugIn* 
OpenSteer::PlugIn::findDefault (void)
{

    if (itemsInRegistry == 0) return NULL;

    for (int i = 0; i < itemsInRegistry; i++)
    {
        if (registry[i]->requestInitialSelection ()) return registry[i];
    }

    return registry[0];
}

void 
OpenSteer::PlugIn::addToRegistry (void)
{

    registry[itemsInRegistry++] = this;
}