/************************************************************************
 * SHIRE Mission SC Interface Config Values Override
 *
 * Raises SC_NUMBER_OF_RTS to 15 to match the range used by RTS 001
 * (SC_ENABLE_RTS_GRP FirstID=1, LastID=15) and support all shire RTS
 * table files (001, 003, 005, 006, 011).
 ************************************************************************/
#ifndef SHIRE_SC_INTERFACE_CFG_VALUES_H
#define SHIRE_SC_INTERFACE_CFG_VALUES_H

#define SC_INTERFACE_CFGVAL(x) SHIRE_SC_INTERFACE_##x

/* Support RTS 001 enable-group range and all defined shire RTSes */
#define SHIRE_SC_INTERFACE_NUMBER_OF_RTS 15

/* Forward remaining values to defaults */
#define SHIRE_SC_INTERFACE_PACKET_MIN_SIZE  DEFAULT_SC_INTERFACE_PACKET_MIN_SIZE
#define SHIRE_SC_INTERFACE_PACKET_MAX_SIZE  DEFAULT_SC_INTERFACE_PACKET_MAX_SIZE
#define SHIRE_SC_INTERFACE_NUMBER_OF_ATS    DEFAULT_SC_INTERFACE_NUMBER_OF_ATS
#define SHIRE_SC_INTERFACE_MAX_ATS_CMDS     DEFAULT_SC_INTERFACE_MAX_ATS_CMDS
#define SHIRE_SC_INTERFACE_ATS_BUFF_SIZE    DEFAULT_SC_INTERFACE_ATS_BUFF_SIZE
#define SHIRE_SC_INTERFACE_APPEND_BUFF_SIZE DEFAULT_SC_INTERFACE_APPEND_BUFF_SIZE
#define SHIRE_SC_INTERFACE_RTS_BUFF_SIZE    DEFAULT_SC_INTERFACE_RTS_BUFF_SIZE

#endif /* SHIRE_SC_INTERFACE_CFG_VALUES_H */
