//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================
#include "ac/dynobj/cc_guiobject.h"
#include "ac/dynobj/scriptgui.h"
#include "gui/guimain.h"
#include "gui/guiobject.h"
#include "util/stream.h"

using namespace AGS::Common;

// return the type name of the object
const char *CCGUIObject::GetType() {
    return "GUIObject";
}

size_t CCGUIObject::CalcSerializeSize()
{
    return sizeof(int32_t) * 2;
}

// serialize the object into BUFFER (which is BUFSIZE bytes)
// return number of bytes used
void CCGUIObject::Serialize(const char *address, Stream *out) {
    GUIObject *guio = (GUIObject*)address;
    out->WriteInt32(guio->ParentId);
    out->WriteInt32(guio->Id);
}

void CCGUIObject::Unserialize(int index, const char *serializedData, int dataSize) {
    StartUnserialize(serializedData, dataSize);
    int guinum = UnserializeInt();
    int objnum = UnserializeInt();
    ccRegisterUnserializedObject(index, guis[guinum].GetControl(objnum), this);
}
