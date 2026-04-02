/* $Id$

	Add status entry of a tag into context

	silent

	The status is sizemax encoded like this:
		sizemax | 0x8001
	--> neither of the bytes can become 0

	Return:
		E_None on success
*/

#include "../config.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>

#include "../include/command.h"
#include "../include/context.h"

#if UINT_MAX != 65535U
#error "sizeof(unsigned) != 2 is not supported"
#endif

int ctxtAddStatus(const Context_Tag tag)
{
	char value[3];

	ctxtCheckInfoTag(tag);

	{
		uint16_t encoded = (uint16_t)(CTXT_INFO(tag, sizemax) | 0x8001);

		value[0] = (char)(encoded & 0xFF);
		value[1] = (char)((encoded >> 8) & 0xFF);
		value[2] = '\0';
	}

	return ctxtSet(tag, 0, value);
}
