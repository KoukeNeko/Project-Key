// How apps draw the composition: a dotted underline for the composition, a
// bold accent underline for the segment being chosen.
#pragma once

#include <msctf.h>

namespace ProjectKey {

HRESULT CreateDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** result);
HRESULT CreateDisplayAttributeEnum(IEnumTfDisplayAttributeInfo** result);

}  // namespace ProjectKey
