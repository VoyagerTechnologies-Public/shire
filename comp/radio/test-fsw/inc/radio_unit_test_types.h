/*
** Stub header for missing types used in unit testing
*/

#ifndef _RADIO_UNIT_TEST_TYPES_H_
#define _RADIO_UNIT_TEST_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

/*
** cFE-style type definitions for compatibility
*/
typedef int32_t int32;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

/*
** TC (Telecommand) structure stub for unit testing
*/
typedef struct {
    uint16_t tc_pdu_len;
    uint8_t tc_pdu[1024];
} TC_t;

/*
** Security Association Interface stub for unit testing
*/
typedef struct SecurityAssociation {
    uint32_t spi;
    uint8_t sa_state;
} SecurityAssociation_t;

typedef struct SaInterfaceStruct {
    int32_t (*sa_get_operational_sa_from_gvcid)(uint8_t gvcid, uint8_t scid, uint8_t vcid, uint8_t mapid, SecurityAssociation_t **sa);
} *SaInterface;

/*
** Constants for unit testing
*/
#define CRYPTO_LIB_SUCCESS 0
#define CRYPTO_LIB_ERROR -1
#define TM_SYNC_ASM_SIZE 4
#define TM_SYNC_ASM_STR "\x1A\xCF\xFC\x1D"

#endif /* _RADIO_UNIT_TEST_TYPES_H_ */
