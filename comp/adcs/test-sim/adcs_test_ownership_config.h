#ifndef ADCS_TEST_OWNERSHIP_CONFIG_H
#define ADCS_TEST_OWNERSHIP_CONFIG_H
/* The failure executable substitutes configuration values, not init logic.
 * This lets it test bad generated ownership settings without editing headers
 * or relaxing validation in the production simulator. */
#include "adcs_sim.h"
enum {
    ADCS_TEST_DEFAULT_SPACECRAFT = ADCS_CFG_SPACECRAFT_ID,
    ADCS_TEST_DEFAULT_WHEELS = ADCS_CFG_WHEEL_MASK,
    ADCS_TEST_DEFAULT_MTBS = ADCS_CFG_MTB_MASK
};
extern int adcs_test_spacecraft, adcs_test_wheels, adcs_test_mtbs;
#undef ADCS_CFG_SPACECRAFT_ID
#undef ADCS_CFG_WHEEL_MASK
#undef ADCS_CFG_MTB_MASK
#define ADCS_CFG_SPACECRAFT_ID adcs_test_spacecraft
#define ADCS_CFG_WHEEL_MASK adcs_test_wheels
#define ADCS_CFG_MTB_MASK adcs_test_mtbs
#endif
