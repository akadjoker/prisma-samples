
#ifndef OPENSTEER_UTILITIES_H
#define OPENSTEER_UTILITIES_H

#include <iostream>  
#include <cstdlib>   
#include <cfloat>    
#include <cmath>     
#include <vector>    
#include <cassert>   
#include <limits>    

#ifndef OPENSTEER_M_PI
#define OPENSTEER_M_PI 3.14159265358979323846f
#endif

#ifdef _MSC_VER
#undef min
#undef max
#endif

namespace OpenSteer {

    template<class T> inline T interpolate (float alpha, const T& x0, const T& x1)
    {
        return x0 + ((x1 - x0) * alpha);
    }

    inline float frandom01 (void)
    {
        return (((float) rand ()) / ((float) RAND_MAX));
    }

    inline float frandom2 (float lowerBound, float upperBound)
    {
        return lowerBound + (frandom01 () * (upperBound - lowerBound));
    }

    inline float clip (const float x, const float min, const float max)
    {
        if (x < min) return min;
        if (x > max) return max;
        return x;
    }

    inline float remapInterval (float x,
                                float in0, float in1,
                                float out0, float out1)
    {

        float relative = (x - in0) / (in1 - in0);

        return interpolate (relative, out0, out1);
    }

    inline float remapIntervalClip (float x,
                                    float in0, float in1,
                                    float out0, float out1)
    {

        float relative = (x - in0) / (in1 - in0);

        return interpolate (clip (relative, 0, 1), out0, out1);
    }

    inline int intervalComparison (float x, float lowerBound, float upperBound)
    {
        if (x < lowerBound) return -1;
        if (x > upperBound) return +1;
        return 0;
    }

    inline float scalarRandomWalk (const float initial, 
                                   const float walkspeed,
                                   const float min,
                                   const float max)
    {
        const float next = initial + (((frandom01() * 2) - 1) * walkspeed);
        if (next < min) return min;
        if (next > max) return max;
        return next;
    }

    inline float square (float x)
    {
        return x * x;
    }

    #define debugPrint(e) (std::cout << #e" = " << (e) << std::endl << std::flush)

    template<class T>
    inline void blendIntoAccumulator (const float smoothRate,
                                      const T& newValue,
                                      T& smoothedAccumulator)
    {
        smoothedAccumulator = interpolate (clip (smoothRate, 0, 1),
                                           smoothedAccumulator,
                                           newValue);
    }

    #ifdef _WIN32

    inline float floorXXX (float x)          {return ::floor (x);}
    inline float  sqrtXXX (float x)          {return ::sqrt (x);}
    inline float   sinXXX (float x)          {return ::sin (x);}
    inline float   cosXXX (float x)          {return ::cos (x);}
    inline float   absXXX (float x)          {return ::abs (x);}
    inline int     absXXX (int x)            {return ::abs (x);}
    inline float   maxXXX (float x, float y) {if (x > y) return x; else return y;}
    inline float   minXXX (float x, float y) {if (x < y) return x; else return y;}

    #else

    inline float floorXXX (float x)          {return std::floor (x);}
    inline float  sqrtXXX (float x)          {return std::sqrt (x);}
    inline float   sinXXX (float x)          {return std::sin (x);}
    inline float   cosXXX (float x)          {return std::cos (x);}
    inline float   absXXX (float x)          {return std::abs (x);}
    inline int     absXXX (int x)            {return std::abs (x);}
    inline float   maxXXX (float x, float y) {return std::max (x, y);}
    inline float   minXXX (float x, float y) {return std::min (x, y);}

    #endif

    #ifdef _WIN32

    inline float round (float x)
    {
      if (x < 0)
          return -floorXXX (0.5f - x);
      else
          return  floorXXX (0.5f + x);
    }

    #else 

    inline float round( float x )
    {
        return ::round( x );
    }

    #endif

    template< typename T >
    T
    clamp( T const& valueToClamp, T const& minValue, T const& maxValue) {
        assert( minValue <= maxValue && "minValue must be lesser or equal to maxValue."  );

        if ( valueToClamp < minValue ) {
            return minValue;
        } else if ( valueToClamp > maxValue ) {
            return maxValue;
        }

        return valueToClamp;
    }

    inline float modulo( float x, float y ) {
        assert( 0.0f != y && "Division by zero." );
        return std::fmod( x, y );
    }

    inline double modulo( double x, double y ) {
        assert( 0.0 != y && "Division by zero." );
        return std::fmod( x, y );
    }    

    inline long double modulo( long double x, long double y ) {
        assert( 0.0 != y && "Division by zero." );
        return std::fmod( x, y );
    }

    inline short modulo( short x, short y ) {
        assert( 0 != y && "Division by zero." );
        return x % y;
    }

    inline int modulo( int x, int y ) {
        assert( 0 != y && "Division by zero." );
        return x % y;
    }

    inline long modulo( long x, long y ) {
        assert( 0 != y && "Division by zero." );
        return x % y;
    }

    template< typename T >
    T abs( T const& value ) {
        return absXXX( value );
    }

    template< typename T >
    T
    max( T const& v0, T const& v1, T const& v2 ) {
        return maxXXX( v0, maxXXX( v1, v2 ) );
    }

    template< typename T >
    T
    min( T const& v0, T const& v1, T const& v2 ) {
        return minXXX( v0, minXXX( v1, v2 ) );
    }

    template< typename T >
    bool
    isZero( T const& v, T const& tolerance = std::numeric_limits< T >::epsilon() ) {
        return abs( v ) <= tolerance;
    }

    template< typename T >
    bool
    equalsAbsolute( T const& lhs, T const& rhs, T const& tolerance = std::numeric_limits< T >::epsilon()  ) {
        return isZero( lhs - rhs, tolerance );
    }

    template< typename T >
    bool
    equalsRelative( T const& lhs, T const& rhs, T const& tolerance = std::numeric_limits< T >::epsilon()  ) {
        return isZero( lhs - rhs, tolerance * max( abs( lhs ), abs( rhs ), T( 1 ) ) );
    }

    template< typename T >
    bool
    equalsRelativeApproximately( T const& lhs, T const& rhs, T const& tolerance = std::numeric_limits< T >::epsilon()  ) {
        return isZero( lhs - rhs, tolerance * ( abs( lhs ) + abs( rhs ) + T( 1 ) ) );
    }    

    template< typename T >
    void shrinkToFit( std::vector< T >& v ) {
        std::vector< T >( v ).swap( v );
    }

} 

#endif 