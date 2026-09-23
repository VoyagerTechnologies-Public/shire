#ifndef _ADCS_TBL_H_
#define _ADCS_TBL_H_

#include "cfe.h"

/*
** Boot-loaded control-law gains table. Mirrors ADCS_Device_GainsCmd_t
** (comp/adcs/shared/adcs_device.h) field-for-field; kept as a separate
** type since a cFE table image must be independently definable (see
** comp/adcs/tables/adcs_def_gains.c) without pulling in the device wire
** protocol header.
*/
typedef struct
{
    float SunPointKp;
    float SunPointKd;
    float WheelMaxTorqueNm;
    float MtbMaxDipoleAm2;
    float DetumbleGainBase;
    float DetumbleGainHigh;
    float RotisserieRateRadS;

} ADCS_GainsTbl_t;

#define ADCS_GAINS_TBL_NAME     "GAINS_TBL"
#define ADCS_GAINS_TBL_FILENAME "/cf/adcs_def_gains.tbl"

/*
** Register and load the gains table, called from ADCS_AppInit(). Populates
** ADCS_AppData.GainsTblHandle/GainsTblPtr on success.
*/
int32 ADCS_TableInit(void);

/*
** cFE table validation function: bounds-checks each gain against physical
** actuator/behavioral limits.
*/
int32 ADCS_ValidateGainsTbl(void *TblData);

#endif /* _ADCS_TBL_H_ */
