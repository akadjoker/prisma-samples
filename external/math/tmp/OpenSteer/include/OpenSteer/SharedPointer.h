
#ifndef OPENSTEER_SHAREDPOINTER_H
#define OPENSTEER_SHAREDPOINTER_H

#include <algorithm>

#include <cassert>

#include "OpenSteer/StandardTypes.h"

namespace OpenSteer {

    struct SharedPointerReferenceCount {
        typedef size_t size_type;

        explicit SharedPointerReferenceCount(): referenceCount_( 1 ) {

        }

        size_type referenceCount_;
    };

    template< typename T >
    class SharedPointer {
    public:
        typedef size_t size_type;
        typedef T value_type;
        typedef value_type& reference;
        typedef value_type const& const_reference;
        typedef value_type* pointer;
        typedef value_type const* const_pointer;

        template< typename U > friend class SharedPointer;

        SharedPointer() : data_( 0 ), referenceCount_( new SharedPointerReferenceCount() ) {

        }

        explicit SharedPointer( T* _data ) : data_( _data ), referenceCount_( new SharedPointerReferenceCount() ) {

        }

        SharedPointer( SharedPointer const& other ) : data_( other.data_ ), referenceCount_( other.referenceCount_ ) {
            retain();
        }

        template< typename U >
        SharedPointer( SharedPointer< U > const& other ) : data_( other.data_ ), referenceCount_( other.referenceCount_ ) {
            retain();
        }

        ~SharedPointer() {
            release();
        }

        SharedPointer& operator=( SharedPointer other ) {
            swap( other );

            return *this;
        }

        void swap( SharedPointer& other ) {
            std::swap( data_, other.data_ );
            std::swap( referenceCount_, other.referenceCount_ );
        }

        reference operator*() const {
            assert( 0 != data_ && "Unable to dereference a 0-pointer." );
            return *data_;
        }

        pointer operator->() const {
            assert( 0 != data_ && "Unable to dereference a 0-pointer." );
            return data_;
        }

        size_type useCount() const {
            return referenceCount_->referenceCount_;
        }

        pointer get() const {
            return data_;
        }

        void reset( T* _data = 0 ) {

            SharedPointer( _data ).swap( *this );
        }

        typedef T* (SharedPointer::*unspecified_bool_type)() const;

        operator unspecified_bool_type () const {
            return 0 == data_ ? 0 : &SharedPointer::get;
        }

        template< typename U >
            bool operator<( SharedPointer< U > const& rhs ) {

                return referenceCount_ < rhs.referenceCount_;
            }

    private:

        void release() {
            assert( 0 < referenceCount_->referenceCount_ && "Only call release for reference counts greater than 0." );

            --( referenceCount_->referenceCount_ );
            if ( 0 == referenceCount_->referenceCount_ ) {
                delete data_;
                data_ = 0;
                delete referenceCount_;
                referenceCount_ = 0;
            }
        }

        void retain() {
            ++( referenceCount_->referenceCount_ );

        }

    private:
        pointer data_;
        SharedPointerReferenceCount* referenceCount_;
    }; 

    template< typename T, typename U >
        bool operator==( SharedPointer< T > const& lhs, SharedPointer< U > const& rhs ) {
            return lhs.get() == rhs.get();
        }

    template< typename T, typename U >
        bool operator!=( SharedPointer< T > const& lhs, SharedPointer< U > const& rhs ) {
            return !( lhs == rhs );
        }

    template< typename T >
        void swap( SharedPointer< T >& lhs, SharedPointer< T >& rhs ) {
            lhs.swap( rhs );
        }

} 

#endif 