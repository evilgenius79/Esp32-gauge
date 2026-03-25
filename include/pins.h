#pragma once

// =============================================================================
// Pin Definitions — auto-selects based on build target
// =============================================================================

#if defined(TARGET_TAB5)
    #include "pins_tab5.h"
#else
    #include "pins_crowpanel.h"
#endif
