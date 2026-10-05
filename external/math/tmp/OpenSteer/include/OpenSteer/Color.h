
#ifndef OPENSTEER_COLOR_H
#define OPENSTEER_COLOR_H

namespace OpenSteer {

    class Vec3;

    class Color {
    public:
        Color();
        explicit Color( float greyValue );
        Color( float rValue, float gValue, float bValue, float aValue = 1.0f );
        explicit Color( Vec3 const& vector );

        float r() const;
        float g() const;
        float b() const;
        float a() const;

        void setR( float value );
        void setG( float value );
        void setB( float value );
		void setA( float value );
        void set( float rValue, float gValue, float bValue, float aValue = 1.0f );

        Vec3 convertToVec3() const;

		float const*const colorFloatArray() const { return &r_; }

        Color& operator+=( Color const& other );

        Color& operator-=( Color const& other );

        Color& operator*=( float factor );

        Color& operator/=( float factor );

    private:
        float r_;
        float g_;
        float b_;
		 float a_;	
    }; 

    Color operator+( Color const& lhs, Color const& rhs );

    Color operator-( Color const& lhs, Color const& rhs );

    Color operator*( Color const& lhs, float rhs );

    Color operator*( float lhs, Color const& rhs );

    Color operator/( Color const& lhs, float rhs );

    Color grayColor( float value );

    extern Color const gBlack;
    extern Color const gWhite; 
    extern Color const gRed; 
    extern Color const gGreen;
    extern Color const gBlue;
    extern Color const gYellow;
    extern Color const gCyan;
    extern Color const gMagenta;
    extern Color const gOrange;
    extern Color const gDarkRed;
    extern Color const gDarkGreen;
    extern Color const gDarkBlue;
    extern Color const gDarkYellow;
    extern Color const gDarkCyan;
    extern Color const gDarkMagenta;
    extern Color const gDarkOrange;

    extern Color const gGray10;
    extern Color const gGray20;
    extern Color const gGray30;
    extern Color const gGray40;
    extern Color const gGray50;
    extern Color const gGray60;
    extern Color const gGray70;
    extern Color const gGray80;
    extern Color const gGray90;

} 

#endif 