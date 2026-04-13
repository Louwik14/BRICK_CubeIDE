// Macro for killing denormalled numbers
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// Based on IS_DENORMAL macro by Jon Watte
// This code is public domain

#ifndef _denormals_
#define _denormals_

#include <cstdint>
#include <cstring>

static inline void undenormalise(float &sample)
{
	uint32_t bits = 0U;
	std::memcpy(&bits, &sample, sizeof(bits));
	if ((bits & 0x7f800000U) == 0U)
	{
		sample = 0.0f;
	}
}

#endif//_denormals_

//ends
