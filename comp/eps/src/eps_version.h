#ifndef _EPS_VERSION_H_
#define _EPS_VERSION_H_

#define EPS_MAJOR_VERSION 1
#define EPS_MINOR_VERSION 0
/* The uplink patch build (see CMakeLists.txt) reports revision 1 so its start up event identifies it. */
#ifdef EPS_PATCH_PROTECTED_SWITCH
#define EPS_REVISION      1
#else
#define EPS_REVISION      0
#endif
#define EPS_MISSION_REV   0

#endif
