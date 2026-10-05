
#ifndef OPENSTEER_PROXIMITY_H
#define OPENSTEER_PROXIMITY_H

#include <algorithm>
#include <vector>
#include "OpenSteer/Vec3.h"
#include "OpenSteer/lq.h"   

namespace OpenSteer {

    template <class ContentType>
    class AbstractTokenForProximityDatabase
    {
    public:

        virtual ~AbstractTokenForProximityDatabase () {}

        virtual void updateForNewPosition (const Vec3& position) = 0;

        virtual void findNeighbors (const Vec3& center,
                                    const float radius,
                                    std::vector<ContentType>& results) = 0;

#ifndef NO_LQ_BIN_STATS

        virtual void getBinPopulationStats (int& min, int& max, float& average)
        {min=max=0; average=0.0;}
#endif 
    };

    template <class ContentType>
    class AbstractProximityDatabase
    {
    public:

        typedef AbstractTokenForProximityDatabase<ContentType> tokenType;

        virtual ~AbstractProximityDatabase() {  }

        virtual tokenType* allocateToken (ContentType parentObject) = 0;

        virtual int getPopulation (void) = 0;
    };

    template <class ContentType>
    class BruteForceProximityDatabase
        : public AbstractProximityDatabase<ContentType>
    {
    public:

        BruteForceProximityDatabase (void)
        {
        }

        virtual ~BruteForceProximityDatabase ()
        {
        }

        class tokenType : public AbstractTokenForProximityDatabase<ContentType>
        {
        public:

            tokenType (ContentType parentObject, BruteForceProximityDatabase& pd)
            {

                bfpd = &pd;
                object = parentObject;
                bfpd->group.push_back (this);
            }

            virtual ~tokenType ()
            {

                bfpd->group.erase (std::find (bfpd->group.begin(),
                                              bfpd->group.end(),
                                              this));
            }

            void updateForNewPosition (const Vec3& newPosition)
            {
                position = newPosition;
            }

            void findNeighbors (const Vec3& center,
                                const float radius,
                                std::vector<ContentType>& results)
            {

                const float r2 = radius * radius;
                for (tokenIterator i = bfpd->group.begin();
                     i != bfpd->group.end();
                     i++)
                {
                    const Vec3 offset = center - (**i).position;
                    const float d2 = offset.lengthSquared();

                    if (d2 < r2) results.push_back ((**i).object);
                }
            }

        private:
            BruteForceProximityDatabase* bfpd;
            ContentType object;
            Vec3 position;
        };

        typedef std::vector<tokenType*> tokenVector;
        typedef typename tokenVector::const_iterator tokenIterator;    

        tokenType* allocateToken (ContentType parentObject)
        {
            return new tokenType (parentObject, *this);
        }

        int getPopulation (void)
        {
            return (int) group.size();
        }

    private:

        tokenVector group;
    };

    template <class ContentType>
    class LQProximityDatabase : public AbstractProximityDatabase<ContentType>
    {
    public:

        LQProximityDatabase (const Vec3& center,
                             const Vec3& dimensions,
                             const Vec3& divisions)
        {
            const Vec3 halfsize (dimensions * 0.5f);
            const Vec3 origin (center - halfsize);

            lq = lqCreateDatabase (origin.x, origin.y, origin.z, 
                                   dimensions.x, dimensions.y, dimensions.z,  
                                   (int) round (divisions.x),
                                   (int) round (divisions.y),
                                   (int) round (divisions.z));
        }

        virtual ~LQProximityDatabase ()
        {
            lqDeleteDatabase (lq);
            lq = NULL;
        }

        class tokenType : public AbstractTokenForProximityDatabase<ContentType>
        {
        public:

            tokenType (ContentType parentObject, LQProximityDatabase& lqsd)
            {
                lqInitClientProxy (&proxy, parentObject);
                lq = lqsd.lq;
            }

            virtual ~tokenType (void)
            {
                lqRemoveFromBin (&proxy);
            }

            void updateForNewPosition (const Vec3& p)
            {
                lqUpdateForNewLocation (lq, &proxy, p.x, p.y, p.z);
            }

            void findNeighbors (const Vec3& center,
                                const float radius,
                                std::vector<ContentType>& results)
            {
                lqMapOverAllObjectsInLocality (lq, 
                                               center.x, center.y, center.z,
                                               radius,
                                               perNeighborCallBackFunction,
                                               (void*)&results);
            }

            static void perNeighborCallBackFunction  (void* clientObject,
                                                      float ,
                                                      void* clientQueryState)
            {
                typedef std::vector<ContentType> ctv;
                ctv& results = *((ctv*) clientQueryState);
                results.push_back ((ContentType) clientObject);
            }

#ifndef NO_LQ_BIN_STATS

            void getBinPopulationStats (int& min, int& max, float& average)
            {
                lqGetBinPopulationStats (lq, &min, &max, &average);
            }
#endif 

        private:
            lqClientProxy proxy;
            lqDB* lq;
        };

        tokenType* allocateToken (ContentType parentObject)
        {
            return new tokenType (parentObject, *this);
        }

        int getPopulation (void)
        {
            int count = 0;
            lqMapOverAllObjects (lq, counterCallBackFunction, &count);
            return count;
        }

        static void counterCallBackFunction  (void* ,
                                              float ,
                                              void* clientQueryState)
        {
            int& counter = *(int*)clientQueryState;
            counter++;
        }

    private:
        lqDB* lq;
    };

} 

#endif 