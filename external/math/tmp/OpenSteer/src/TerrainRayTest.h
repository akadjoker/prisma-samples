
#ifndef __TERRAINRAYTEST__
#define __TERRAINRAYTEST__

#if _MSC_VER > 1000
#pragma once
#endif

#define TRT_DOUBLE_PRECISION

#define TRT_PRECOMPUTE_NORMALS

#include <float.h>
#ifdef TRT_DOUBLE_PRECISION
	typedef double TRTScalar;
	#define	TRT_INFINITY	DBL_MAX
#else
	typedef float TRTScalar;
	#define	TRT_INFINITY	FLT_MAX
#endif

struct RayTestInfo {
	bool hitOccurred;				
	TRTScalar t;					
	TRTScalar pos[3];				
	TRTScalar norm[3];
};

class RayTester{

public:

	RayTester();						
	~RayTester();						

	void LoadData( char *fname,	TRTScalar xMin=0, TRTScalar xMax=0,
								TRTScalar yMin=0, TRTScalar yMax=0,
								TRTScalar zMin=0, TRTScalar zMax=0 );

	void RayCast( RayTestInfo &results, const TRTScalar *eyePos, const TRTScalar *viewNorm, TRTScalar maxt=TRT_INFINITY ) const;

private:

	int width, height;

	struct GridCell {
		TRTScalar maxy;
		TRTScalar pos[3];

		#ifdef TRT_PRECOMPUTE_NORMALS
			TRTScalar upLeftNorm[3];
			TRTScalar lowRightNorm[3];
		#endif
	};

	GridCell *data;

	bool transformData;

	TRTScalar minx,maxx,xrange,xstep;
	TRTScalar miny,maxy,yrange;
	TRTScalar minz,maxz,zrange,zstep;

	#ifndef TRT_TRANSFORM_DATA
		TRTScalar _xMin,_xRange;
		TRTScalar _yMin,_yRange;
		TRTScalar _zMin,_zRange;
	#endif

	void RayCastTriangle( RayTestInfo &results, const TRTScalar *eyePos, const TRTScalar *viewNorm, 
							const TRTScalar *vert0, const TRTScalar *vert1, const TRTScalar *vert2 ) const;

	void RectifyResults( RayTestInfo &results ) const;

	void GetNormal( TRTScalar *r, const TRTScalar *u, const TRTScalar *v, const TRTScalar *w ) const;
	void Normalize( TRTScalar *v ) const;

};

#endif