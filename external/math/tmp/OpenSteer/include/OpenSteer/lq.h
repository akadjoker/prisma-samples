
#ifndef	_lq_h
#define	_lq_h

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct lqInternalDB lqDB;

typedef struct lqClientProxy
{

    struct lqClientProxy*  prev;

    struct lqClientProxy*  next;

    struct lqClientProxy** bin;

    void* object;

    float x;
    float y;
    float z;
} lqClientProxy;

lqDB* lqCreateDatabase (float originx, float originy, float originz,
			float sizex,   float sizey,   float sizez,
			int   divx,    int   divy,    int   divz);

void lqDeleteDatabase (lqDB*);

void lqInitClientProxy (lqClientProxy* proxy, void* clientObject);

void lqUpdateForNewLocation (lqDB* lq, 
			     lqClientProxy* object, 
			     float x, float y, float z);

typedef void (* lqCallBackFunction)  (void* clientObject,
				      float distanceSquared,
				      void* clientQueryState);

void lqMapOverAllObjectsInLocality (lqDB* lq, 
				    float x, float y, float z,
				    float radius,
				    lqCallBackFunction func,
				    void* clientQueryState);

void* lqFindNearestNeighborWithinRadius (lqDB* lq, 
					 float x, float y, float z,
					 float radius,
					 void* ignoreObject);

void lqAddToBin (lqClientProxy* object, lqClientProxy** bin);

void lqRemoveFromBin (lqClientProxy* object);

void lqInitDatabase (lqDB* lq,
		     float originx, float originy, float originz,
		     float sizex, float sizey, float sizez,
		     int divx, int divy, int divz);

lqClientProxy** lqBinForLocation (lqDB* lq, float x, float y, float z);

void lqMapOverAllObjects (lqDB* lq, 
			  lqCallBackFunction func,
			  void* clientQueryState);

void lqRemoveAllObjects (lqDB* lq);

#ifndef NO_LQ_BIN_STATS
void lqGetBinPopulationStats (lqDB* lq,
                              int* min,
                              int* max,
                              float* average);
#endif 

#ifndef	NULL
#define NULL 0
#endif

#ifdef __cplusplus
}
#endif

#endif 