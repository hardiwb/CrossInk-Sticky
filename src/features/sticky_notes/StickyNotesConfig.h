#pragma once

// Personal-fork feature gate. Override with
// -DCROSSINK_ENABLE_STICKY_NOTES=0 to build without the menu and activity.
#ifndef CROSSINK_ENABLE_STICKY_NOTES
#define CROSSINK_ENABLE_STICKY_NOTES 1
#endif

#if CROSSINK_ENABLE_STICKY_NOTES != 0 && CROSSINK_ENABLE_STICKY_NOTES != 1
#error "CROSSINK_ENABLE_STICKY_NOTES must be 0 or 1"
#endif
