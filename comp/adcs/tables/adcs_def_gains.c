/**
 * @file
 *  The ADCS Application default control-law gains table.
 *
 *  All fields except RotisserieRateRadS match the compiled-in defaults in
 *  comp/adcs/sim/adcs_sim.h (ADCS_SUN_POINT_KP, etc.), so loading this
 *  default table changes nothing else from pre-table behavior.
 *  RotisserieRateRadS defaults to 0.005 rad/s (~0.29 deg/s, a full
 *  rotation in ~20 minutes) -- issue #8 asks for mild rotisserie as the
 *  actual default sun-point behavior, not an opt-in a ground command has
 *  to enable. Set to 0.0 to reproduce the original static point-and-hold.
 */
#include "cfe.h"
#include "cfe_tbl_filedef.h"
#include "adcs_tbl.h"

ADCS_GainsTbl_t ADCS_GainsTable = {
    0.5f,   /* SunPointKp */
    0.1f,   /* SunPointKd */
    0.005f, /* WheelMaxTorqueNm */
    1.42f,  /* MtbMaxDipoleAm2 */
    0.01f,  /* DetumbleGainBase */
    0.02f,  /* DetumbleGainHigh */
    0.005f, /* RotisserieRateRadS */
};

CFE_TBL_FILEDEF(ADCS_GainsTable, ADCS_APP.GAINS_TBL, ADCS gains table, adcs_def_gains.tbl)
