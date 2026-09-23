#include "adcs_app.h"
#include "adcs_gains_limits.h"

/*
** Bounds-check each gain against the physical actuator ceilings (mirrored
** from comp/adcs/sim/adcs_sim.h's ADCS_* macros -- see
** adcs_sim_gains_limits.h) and a "mild" cap on the rotisserie rate.
*/
int32 ADCS_ValidateGainsTbl(void *TblData)
{
    const ADCS_GainsTbl_t *tbl = (const ADCS_GainsTbl_t *)TblData;
    int32                  status = CFE_SUCCESS;

    if (tbl->SunPointKp <= 0.0f || tbl->SunPointKd <= 0.0f)
    {
        CFE_EVS_SendEvent(ADCS_TBL_VALIDATE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Gains table validation failed: SunPointKp/Kd must be > 0 (Kp=%f Kd=%f)",
                          (double)tbl->SunPointKp, (double)tbl->SunPointKd);
        status = CFE_STATUS_VALIDATION_FAILURE;
    }
    else if (tbl->WheelMaxTorqueNm <= 0.0f || tbl->WheelMaxTorqueNm > ADCS_GAINS_WHEEL_MAX_TORQUE_CEILING)
    {
        CFE_EVS_SendEvent(ADCS_TBL_VALIDATE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Gains table validation failed: WheelMaxTorqueNm out of range (0, %f]: %f",
                          (double)ADCS_GAINS_WHEEL_MAX_TORQUE_CEILING, (double)tbl->WheelMaxTorqueNm);
        status = CFE_STATUS_VALIDATION_FAILURE;
    }
    else if (tbl->MtbMaxDipoleAm2 <= 0.0f || tbl->MtbMaxDipoleAm2 > ADCS_GAINS_MTB_MAX_DIPOLE_CEILING)
    {
        CFE_EVS_SendEvent(ADCS_TBL_VALIDATE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Gains table validation failed: MtbMaxDipoleAm2 out of range (0, %f]: %f",
                          (double)ADCS_GAINS_MTB_MAX_DIPOLE_CEILING, (double)tbl->MtbMaxDipoleAm2);
        status = CFE_STATUS_VALIDATION_FAILURE;
    }
    else if (tbl->DetumbleGainBase <= 0.0f || tbl->DetumbleGainHigh <= 0.0f ||
             tbl->DetumbleGainHigh < tbl->DetumbleGainBase)
    {
        CFE_EVS_SendEvent(ADCS_TBL_VALIDATE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Gains table validation failed: DetumbleGainBase/High invalid "
                          "(base=%f high=%f)",
                          (double)tbl->DetumbleGainBase, (double)tbl->DetumbleGainHigh);
        status = CFE_STATUS_VALIDATION_FAILURE;
    }
    else if (tbl->RotisserieRateRadS < 0.0f || tbl->RotisserieRateRadS > ADCS_GAINS_ROTISSERIE_RATE_CEILING)
    {
        CFE_EVS_SendEvent(ADCS_TBL_VALIDATE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Gains table validation failed: RotisserieRateRadS out of range [0, %f]: %f",
                          (double)ADCS_GAINS_ROTISSERIE_RATE_CEILING, (double)tbl->RotisserieRateRadS);
        status = CFE_STATUS_VALIDATION_FAILURE;
    }

    return status;
}

/*
** Register and load the boot-time gains table. Called from ADCS_AppInit().
** Does not push the table to the device -- the UART isn't open until
** ADCS_Enable() (ground-commanded), which pushes ADCS_AppData.GainsTblPtr
** at the end of its success path.
*/
int32 ADCS_TableInit(void)
{
    int32 status;

    status = CFE_TBL_Register(&ADCS_AppData.GainsTblHandle, ADCS_GAINS_TBL_NAME, sizeof(ADCS_GainsTbl_t),
                              CFE_TBL_OPT_DEFAULT, ADCS_ValidateGainsTbl);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_TBL_REGISTER_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error registering gains table, RC=0x%08X", (unsigned int)status);
        return status;
    }

    status = CFE_TBL_Load(ADCS_AppData.GainsTblHandle, CFE_TBL_SRC_FILE, ADCS_GAINS_TBL_FILENAME);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_TBL_LOAD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error loading gains table, RC=0x%08X", (unsigned int)status);
        return status;
    }

    status = CFE_TBL_Manage(ADCS_AppData.GainsTblHandle);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_TBL_MANAGE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error in CFE_TBL_Manage for gains table, RC=0x%08X", (unsigned int)status);
        return status;
    }

    status = CFE_TBL_GetAddress((void **)&ADCS_AppData.GainsTblPtr, ADCS_AppData.GainsTblHandle);
    if ((status != CFE_SUCCESS) && (status != CFE_TBL_INFO_UPDATED))
    {
        CFE_EVS_SendEvent(ADCS_TBL_GETADDR_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error getting gains table address, RC=0x%08X", (unsigned int)status);
        return status;
    }

    return CFE_SUCCESS;
}
