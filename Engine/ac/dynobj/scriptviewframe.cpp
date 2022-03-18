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
#include "ac/dynobj/scriptviewframe.h"
#include "util/stream.h"

using namespace AGS::Common;

int ScriptViewFrame::Dispose(const char *address, bool force) {
    // always dispose a ViewFrame
    delete this;
    return 1;
}

const char *ScriptViewFrame::GetType() {
    return "ViewFrame";
}

size_t ScriptViewFrame::CalcSerializeSize()
{
    return sizeof(int32_t) * 3;
}

void ScriptViewFrame::Serialize(const char *address, Stream *out) {
    out->WriteInt32(view);
    out->WriteInt32(loop);
    out->WriteInt32(frame);
}

void ScriptViewFrame::Unserialize(int index, const char *serializedData, int dataSize) {
    StartUnserialize(serializedData, dataSize);
    view = UnserializeInt();
    loop = UnserializeInt();
    frame = UnserializeInt();
    ccRegisterUnserializedObject(index, this, this);
}

ScriptViewFrame::ScriptViewFrame(int p_view, int p_loop, int p_frame) {
    view = p_view;
    loop = p_loop;
    frame = p_frame;
}

ScriptViewFrame::ScriptViewFrame() {
    view = -1;
    loop = -1;
    frame = -1;
}
