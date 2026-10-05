#pragma once

namespace prisma
{

template<typename To, typename From>
inline To handleCast(From handle)
{
    return To::from_bits(handle.bits());
}

} // namespace prisma
