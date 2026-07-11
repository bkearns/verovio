/////////////////////////////////////////////////////////////////////////////
// Name:        iojsm.h
// Purpose:     Native JSM input adapter
/////////////////////////////////////////////////////////////////////////////

#ifndef __VRV_IOJSM_H__
#define __VRV_IOJSM_H__

#include "iobase.h"

namespace vrv {

class JsmInput : public Input {
public:
    JsmInput(Doc *doc);
    ~JsmInput() override = default;

    bool Import(const std::string &data) override;
};

} // namespace vrv

#endif // __VRV_IOJSM_H__
