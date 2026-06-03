//*******************************************************************
//    pldm.h
//
//    Portions of this code are based on the Platform Level Data Model
//    (PLDM) specifications from the Distributed Management Task Force 
//    (DMTF).  More information about PLDM can be found on the DMTF
//    web site (www.dmtf.org).
//
//    More information on the PICMG IoT data model can be found within
//    the PICMG family of IoT specifications.  For more information,
//    please visit the PICMG web site (www.picmg.org)
//
//
#ifndef PLDM_H
#define PLDM_H

#include "mctp.h"
#include <stdint.h>

// common data values
#define PDR_TYPE_TERMINUS_LOCATOR                   1
#define PDR_TYPE_NUMERIC_SENSOR                     2
#define PDR_TYPE_NUMERIC_SENSOR_INITIALIZATION      3
#define PDR_TYPE_STATE_SENSOR                       4
#define PDR_TYPE_STATE_SENSOR_INITIALIZATION        5
#define PDR_TYPE_OEM_STATE_SET                      8
#define PDR_TYPE_NUMERIC_EFFECTER                   9
#define PDR_TYPE_NUMERIC_EFFECTER_INITIALIZATION    10
#define PDR_TYPE_STATE_EFFECTER                     11
#define PDR_TYPE_STATE_EFFECTER_INITIALIZATION      12
#define PDR_TYPE_ENTITY_ASSOCIATION                 15
#define PDR_TYPE_OEM_ENTITY_ID                      17
#define PDR_TYPE_FRU_RECORD_SET                     20

// PLDM Control and discovery command codes (Type code = 0x00)
#define CMD_SET_TID                                 0x01 // SetTID
#define CMD_GET_TID                                 0x02 // GetTID
#define CMD_GET_PLDM_VERSION                        0x03 //
#define CMD_GET_PLDM_TYPES                          0x04 //
#define CMD_GET_PLDM_COMMANDS                       0x05 //

// PLDM message command codes (Type code = 2)
#define CMD_GET_TERMINUS_UID                        0x03 // GetTerminusUID
#define CMD_SET_EVENT_RECEIVER                      0x04 // SetEventReceiver
#define CMD_GET_EVENT_RECEIVER                      0x05 // GetEventReceiver
#define CMD_PLATFORM_EVENT_MESSAGE                  0x0A // PlatformEventMessage
#define CMD_POLL_FOR_PLATFORM_EVENT_MESSAGE         0x0B // PollForPlatformEventMessage
#define CMD_EVENT_MESSAGE_SUPPORTED                 0x0C // EventMessageSupported
#define CMD_EVENT_MESSAGE_BUFFER_SIZE               0x0D // EventMessageBufferSize
#define CMD_SET_NUMERIC_SENSOR_ENABLE               0x10 // SetNumericSensorEnable
#define CMD_GET_SENSOR_READING                      0x11 // GetSensorReading
#define CMD_GET_SENSOR_THRESHOLDS                   0x12 // GetSensorThresholds
#define CMD_SET_SENSOR_THRESHOLDS                   0x13 // SetSensorThresholds
#define CMD_RESTORE_SENSOR_THRESHOLDS               0x14 // RestoreSensorThresholds
#define CMD_GET_SENSOR_HYSTERESIS                   0x15 // GetSensorHysteresis
#define CMD_SET_SENSOR_HYSTERESIS                   0x16 // SetSensorHysteresis
#define CMD_INIT_NUMERIC_SENSOR                     0x17 // InitNumericSensor
#define CMD_SET_STATE_SENSOR_ENABLES                0x20 // SetStateSensorEnables
#define CMD_GET_STATE_SENSOR_READINGS               0x21 // GetStateSensorReadings
#define CMD_INIT_STATE_SENSOR                       0x22 // InitStateSensor
#define CMD_SET_NUMERIC_EFFECTER_ENABLE             0x30 // SetNumericEffecterEnable
#define CMD_SET_NUMERIC_EFFECTER_VALUE              0x31 // SetNumericEffecterValue
#define CMD_GET_NUMERIC_EFFECTER_VALUE              0x32 // GetNumericEffecterValue
#define CMD_SET_STATE_EFFECTER_ENABLES              0x38 // SetStateEffecterEnables
#define CMD_SET_STATE_EFFECTER_STATES               0x39 // SetStateEffecterStates
#define CMD_GET_STATE_EFFECTER_STATES               0x3A // GetStateEffecterStates
#define CMD_GET_PLDM_EVENT_LOG_INFO                 0x40 // GetPLDMEventLogInfo
#define CMD_ENABLE_PLDM_EVENT_LOGGING               0x41 // EnablePLDMEventLogging
#define CMD_CLEAR_PLDM_EVENT_LOG                    0x42 // ClearPLDMEventLog
#define CMD_GET_PLDM_EVENT_LOG_TIMESTAMP            0x43 // GetPLDMEventLogTimestamp
#define CMD_SET_PLDM_EVENT_LOG_TIMESTAMP            0x44 // SetPLDMEventLogTimestamp
#define CMD_READ_PLDM_EVENT_LOG                     0x45 // ReadPLDMEventLog
#define CMD_GET_PLDM_EVENT_LOG_POLICY_INFO          0x46 // GetPLDMEventLogPolicyInfo
#define CMD_SET_PLDM_EVENT_LOG_POLICY               0x47 // SetPLDMEventLogPolicy
#define CMD_FIND_PLDM_EVENT_LOG_ENTRY               0x48 // FindPLDMEventLogEntry
#define CMD_GET_PDR_REPOSITORY_INFO                 0x50 // GetPDRRepositoryInfo
#define CMD_GET_PDR                                 0x51 // GetPDR
#define CMD_FIND_PDR                                0x52 // FindPDR
#define CMD_RUN_INIT_AGENT                          0x58 // RunInitAgent
#define CMD_GET_PDR_REPOSITORY_SIGNATURE            0x53 // GetPDRRepositorySignature

// PLDM for FRU DATA PLDM TYPE = 4
#define CMD_GET_FRU_TABLE_METADATA                  0x01 
#define CMD_GET_FRU_RECORD_TABLE                    0x02 

#define RESPONSE_SUCCESS                            0x00
#define RESPONSE_ERROR                              0x01
#define RESPONSE_ERROR_INVALID_DATA                 0x02
#define RESPONSE_ERROR_INVALID_LENGTH               0x03
#define RESPONSE_ERROR_NOT_READY                    0x04
#define RESPONSE_ERROR_UNSUPPORTED_PLDM_CMD         0x05
#define RESPONSE_ERROR_INVALID_PLDM_TYPE            0x20

// PLDM completion codes
#define RESPONSE_INVALID_PROTOCOL_TYPE              0x80
#define RESPONSE_INVALID_SENSOR_ID                  0x80
#define RESPONSE_INVALID_EFFECTER_ID                0x80
#define RESPONSE_INVALID_SEARCH_TYPE                0x80
#define RESPONSE_INVALID_DATA_TRANSFER_HANDLE       0x80
#define RESPONSE_INVALID_FIND_HANDLE                0x80
#define RESPONSE_ENABLE_METHOD_NOT_SUPPORTED        0x81
#define RESPONSE_UNSUPPORTED_EVENT_FORMAT_VERSION   0x81
#define RESPONSE_INVALID_SENSOR_OPERATIONAL_STATE   0x81
#define RESPONSE_REARM_UNAVAILABLE_IN_PRESENT_STATE 0x81
#define RESPONSE_UNSUPPORTED_SENSORSTATE            0x81
#define RESPONSE_INVALID_STATE_VALUE                0x81
#define RESPONSE_INVALID_TRANSFER_OPERATION_FLAG    0x81
#define RESPONSE_INVALID_FIND_OPERATION_FLAG        0x81
#define RESPONSE_HEARTBEAT_FREQUENCY_TOO_HIGH       0x82
#define RESPONSE_EVENT_ID_NOT_VALID                 0x82
#define RESPONSE_EVENT_GENERATION_NOT_SUPPORTED     0x82
#define RESPONSE_UNSUPPORTED_EFFECTERSTATE          0x82
#define RESPONSE_INVALID_ENTRY_ID                   0x82
#define RESPONSE_INVALID_RECORD_HANDLE              0x82
#define RESPONSE_INVALID_PDR_TYPE                   0x82
#define RESPONSE_INVALID_RECORD_CHANGE_NUMBER       0x83
#define RESPONSE_INVALID_PARAMETER_FORMAT_NUMBER    0x83
#define RESPONSE_TRANSFER_TIMEOUT                   0x84
#define RESPONSE_INVALID_FIND_PARAMETERS            0x84
#define RESPONSE_REPOSITORY_UPDATE_IN_PROGRESS      0x85

/*********************************************************
* Command and response structures
*/
typedef struct {
    uint8_t bytes[13];
} timestamp104;

#pragma pack(push)
#pragma pack(1)
typedef struct {
    unsigned char flags1;   // 7:rq, 6:D, 5:rsvd, 4:0: Instance Id
    unsigned char flags2;   // 7:6: Hdr Ver, 5:0: PldmType
    unsigned char command;
} pldm_request_header;

typedef struct {
    unsigned char flags1;   // 7:rq, 6:D, 5:rsvd, 4:0: Instance Id
    unsigned char flags2;   // 7:6: Hdr Ver, 5:0: PldmType
    unsigned char command;
    unsigned char completion_code;
} pldm_response_header;

typedef struct {
    uint8_t        completion_code;
    uint8_t        repository_state;
    timestamp104 update_time;
    timestamp104 OEM_update_time;
    uint32_t       record_count;
    uint32_t       repository_size;
    uint32_t       largest_record_size;
    uint8_t        data_transfer_handle_timeout;
} get_pdr_repository_info_response;

typedef struct {
    uint32_t       record_handle;
    uint32_t       data_transfer_handle;
    uint8_t        transfer_operation_flag;
    uint16_t       request_count;
    uint16_t       record_change_number;
} get_pdr_command;

typedef struct {
    uint8_t        completion_code;
    uint32_t       next_record_handle;
    uint32_t       next_data_transfer_handle;
    uint8_t        transfer_flag;
    uint16_t       response_count;
} get_pdr_response;

/******************************************************************
* Platform Data Record Structures
*/
typedef struct {
    uint32_t record_handle;
    uint8_t pdr_header_version;
    uint8_t pdr_type;
    uint16_t record_change_number;
    uint16_t data_length;
} pdr_common_header;
#pragma pack(pop)

// update a CRC-8 value when transmitting a new character of data.
uint8_t calc_new_crc8(uint8_t old_crc, uint8_t new_byte);


void pldm_put_command(pldm_request_header* hdr, uint8_t* command, unsigned int size);
uint8_t* pldm_get_response(void);
void pldm_send_numeric_sensor_event(pldm_request_header *rx_header, uint8_t more, event_generator_instance* egi, unsigned int sensor_id, 
                                    uint8_t previous_event_state, FIXEDPOINT_24_8 present_reading);
void pldm_send_state_sensor_event(pldm_request_header *rx_header, uint8_t more, event_generator_instance* egi, unsigned int sensor_id, 
                                    uint8_t previous_event_state);
void pldm_update_events();

#endif // PLDM_H