#pragma once

#include <iostream>
#include "DrumUiAbstraction.h"

class DrumModel {
public:
    virtual ~DrumModel() {}
    virtual void Init() = 0;
    virtual void Trigger() = 0;
    virtual float Process() = 0;

    /*
     * Desktop control rendering is optional for embedded DSP porting.
     * Engines can still override this for the desktop demo host.
     */
    virtual void RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI
        /* Default empty implementation for non-UI engines. */
#endif
    }

    // Serialization hooks are optional during embedded drum DSP porting.
    virtual void saveParameters(std::ostream& os) const {
        (void)os;
    }
    virtual void loadParameters(std::istream& is) {
        (void)is;
    }
};
