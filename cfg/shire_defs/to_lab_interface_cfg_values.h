/************************************************************************
 * SHIRE Mission TO_LAB Interface Config Values Override
 *
 * Overrides TO_LAB_MISSION_TLM_PORT to 1235 to match the port that
 * YAMCS debug-in listens on (yamcs.shire.yaml).
 *
 * Default was 2234; YAMCS was configured for 1235 as the SHIRE
 * telemetry debug port.
 ************************************************************************/
#ifndef SHIRE_TO_LAB_INTERFACE_CFG_VALUES_H
#define SHIRE_TO_LAB_INTERFACE_CFG_VALUES_H

#define TO_LAB_MISSION_CFGVAL(x) SHIRE_TO_LAB_MISSION_##x

/* Send telemetry to port 1235 — matches YAMCS debug-in link */
#define SHIRE_TO_LAB_MISSION_TLM_PORT 1235

/* Forward remaining values to defaults */
#define SHIRE_TO_LAB_MISSION_MAX_SUBSCRIPTIONS DEFAULT_TO_LAB_MISSION_MAX_SUBSCRIPTIONS

#endif /* SHIRE_TO_LAB_INTERFACE_CFG_VALUES_H */
