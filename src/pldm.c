#/**
 * @file src/pldm.c
 * @brief PLDM handlers and helper functions.
 *
 * Portions of this code are based on the Platform Level Data Model
 * (PLDM) specifications from the Distributed Management Task Force
 * (DMTF). More information about PLDM can be found at www.dmtf.org.
 */
#include "pldm.h"
#include "mctp.h"

static uint8_t   tid = 0;
static uint8_t   global_event_enable_state = 0;
static uint8_t   event_fifo_insert_id = 0;
static uint8_t   event_fifo_extract_id = 0;

#ifdef UUID
    const unsigned char uuid_bytes[] PROGMEM = {UUID};
#else
    const uint8_t uuid_bytes[] PROGMEM = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
#endif

#define FRU_BYTE_TYPE const unsigned char
#define LINTABLE_TYPE const long
#define PDR_DATA_ATTRIBUTES PROGMEM
#define FRU_DATA_ATTRIBUTES PROGMEM
#define LINTABLE_DATA_ATTRIBUTES PROGMEM

#define UINT8_TYPE  0
#define SINT8_TYPE  1
#define UINT16_TYPE 2
#define SINT16_TYPE 3
#define UINT32_TYPE 4
#define SINT32_TYPE 5


static void transmit_byte(unsigned char data) {
    // send the data to the MCTP buffer in little-endian fashion
    mctp_transmit_frame_data(&data,1);
}

static void transmit_short(unsigned int data) {
    // send the data to the MCTP buffer in little-endian fashion
    uint8_t ch = (data&0xff);
    transmit_byte(ch);
    data = data>>8;
    ch = (data&0xff);
    transmit_byte(ch);
}

static void transmit_long(unsigned long data) {
    // send the data to the MCTP buffer in little-endian fashion
    transmit_short(data);
    data = data>>16;
    transmit_short(data);
}

/**
 * @brief Retrieve a PDR header by index from the PDR repository.
 *
 * Iterates the repository to locate the PDR header for the specified
 * index. Indexing is 1-based; passing 0 will return the first record.
 *
 * @param index The 1-based index of the PDR to retrieve (0 returns first).
 * @return PdrCommonHeader* Pointer to the PDR header, or NULL if not found.
 */
static pdr_common_header* get_pdr_header(unsigned int index) {
    // adjust the index for the pdr structure - assume records start at 1,
    // and increase sequentially.  0 is a special case which will also retrieve
    // the first record
    if (index > 0) index--;
    pdr_common_header* result = (pdr_common_header*)(__pdr_data);
    unsigned long offset = 0;
    unsigned int counter = 0;
    while (counter != index) {
        unsigned long part1 = sizeof(pdr_common_header);
        unsigned long part2 = pgm_read_dword(&(result->dataLength));
        offset = offset + part1 + part2;
        result = (pdr_common_header*)(&__pdr_data[offset]);
        if (pgm_read_byte(&(__pdr_data[offset])) == 0) return 0;
        counter++;
    }
    return result;
}

/**
 * @brief Return the byte offset of a PDR's data by index.
 *
 * Walks the PDR repository to compute the data offset for the requested
 * PDR index. Indexing is 1-based; passing 0 will return the first record's offset.
 *
 * @param index The 1-based PDR index (0 returns first).
 * @return unsigned int Byte offset to the PDR data, or 0 if not found.
 */
static unsigned int get_pdr_offset(unsigned int index) {
    // adjust the index for the pdr structure - assume records start at 1,
    // and increase sequentially.  0 is a special case which will also retrieve
    // the first record
    if (index > 0) index--;

    PdrCommonHeader* hdr = (PdrCommonHeader*)(__pdr_data);
    unsigned int offset = 0;
    unsigned int counter = 0;
    while (counter != index) {
        unsigned long part1 = sizeof(PdrCommonHeader);
        unsigned long part2 = pgm_read_dword(&(hdr->dataLength));
        offset = offset + part1 + part2;
        hdr = (PdrCommonHeader*)(&__pdr_data[offset]);
        if (pgm_read_byte(&(__pdr_data[offset])) == 0) return 0;
        counter++;
    }
    return offset;
}

/**
 * @brief Compute the next PDR record number.
 *
 * Returns the record number following the provided index. If index is 0,
 * the next record is 2.
 *
 * @param index Current record number.
 * @return unsigned long Next record number.
 */
static unsigned long  get_next_record(unsigned long index) {
    unsigned result = 0;
    if (index == 0) result = 2;
    else result = index+1;
    return result;
}

/**
 * @brief Calculate the total size of a PDR (header + data).
 *
 * @param index The 1-based index of the PDR to measure.
 * @return unsigned int Size in bytes of the PDR including its header, or 0 on error.
 */
static unsigned int pdr_size(unsigned int index) {
    PdrCommonHeader * header = get_pdr_header(index);
    if (!header) return 0;
    unsigned int pdr_size = pgm_read_word(&(header->dataLength)); 
    return pdr_size + sizeof(PdrCommonHeader);
}

/**
 * @brief Handle the PLDM GetPDR command and manage multi-part transfers.
 *
 * Implements the PLDM GetPDR transfer state machine, supporting single
 * and multi-part transfers and constructing appropriate responses in the
 * transmit buffer.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void process_command_get_pdr(pldm_request_header* rx_header) 
{
    static char pdrTxState = 0;
    static char crc8;
    static unsigned int pdrNextHandle;
    static unsigned long pdrRecord;
    unsigned int extraction_point;
    GetPdrCommand* request = (GetPdrCommand*)(mctp_context.rxBuffer + sizeof(*rx_header));
    unsigned char errorcode = 0;
    switch (pdrTxState) {
    case 0:
        // transfer has not begun yet
        if (request->transferOperationFlag != 0x1)
            errorcode = RESPONSE_INVALID_TRANSFER_OPERATION_FLAG;
        else if (request->record_handle > PDR_NUMBER_OF_RECORDS)
            errorcode = RESPONSE_INVALID_RECORD_HANDLE;
        else if (request->data_transfer_handle != 0x0000)
            errorcode = RESPONSE_INVALID_DATA_TRANSFER_HANDLE;
        else if (request->recordChangeNumber != 0x0000)
            errorcode = RESPONSE_INVALID_RECORD_CHANGE_NUMBER;
        if (errorcode) {
            // send the error response
            mctp_transmit_frame_Start(sizeof(pldm_response_header)+sizeof(GetPdrResponse) + 5-1, 1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            mctp_transmit_frame_data(&errorcode,1);  // response->completionCode = errorcode;
            transmit_long(0);                       // response->nextrecord_handle = 0;
            transmit_long(0);                       // response->next_record_transfer_handle;
            transmit_byte(0);                       // response->transfer_flag = 0;
            transmit_short(0);                      // response->response_count = 0;
            mctp_transmit_frame_end();
            return;
        }
        if (request->request_count >= pdr_size(request->record_handle)) {
            // send the data (single part)
            mctp_transmit_frame_Start( sizeof(pldm_response_header) + sizeof(GetPdrResponse) + pdr_size(request->record_handle)+5-1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
            transmit_long((get_next_record(request->record_handle) <= PDR_NUMBER_OF_RECORDS) ?
                get_next_record(request->record_handle) : 0);
            transmit_long(0);                      //response->nextdata_transfer_handle = 0;
            transmit_byte(0x05);                   // response->transfer_flag = 0x05;   // start and end
            transmit_short(pdr_size(request->record_handle)); // response->response_count = pdrSize(request->record_handle);
            unsigned int extraction_point = get_pdr_offset(request->record_handle);
            // insert the pdr
            for (int i = 0;i < pdr_size(request->record_handle); i++) {
                transmit_byte(pgm_read_byte(&(__pdr_data[extraction_point++])));
            }
            mctp_transmit_frame_end();
            return;
        }
        // start sending the data (multi-part)
        mctp_transmit_frame_Start( sizeof(pldm_response_header) + sizeof(GetPdrResponse) + request->request_count+5-1,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
        transmit_long((get_next_record(request->record_handle) <= PDR_NUMBER_OF_RECORDS) ?
                get_next_record(request->record_handle) : 0);
        transmit_long(get_pdr_offset(request->record_handle) + request->request_count);
        transmit_byte(0x00);                // response->transfer_flag = 0x0;   // start
        transmit_short(request->request_count); // response->response_count = request->request_count;
        extraction_point = request->data_transfer_handle+get_pdr_offset(request->record_handle);
        // insert the pdr
        crc8 = 0;
        for (int i = 0;i < request->request_count;i++) {
            unsigned char byte = pgm_read_byte(&(__pdr_data[extraction_point++])); 
            transmit_byte(byte);
            crc8 = calc_new_crc8(crc8, byte);
        }
        mctp_transmit_frame_end();
        pdrTxState = 1;
        pdrRecord = request->record_handle;
        pdrNextHandle = get_pdr_offset(request->record_handle) + request->request_count;
        return;
    case 1:
        // transfer has already begun
        if (request->transferOperationFlag != 0x0)
            errorcode = RESPONSE_INVALID_TRANSFER_OPERATION_FLAG;
        else if (request->record_handle != pdrRecord)
            errorcode = RESPONSE_INVALID_RECORD_HANDLE;
        else if (request->data_transfer_handle != pdrNextHandle)
            errorcode = RESPONSE_INVALID_DATA_TRANSFER_HANDLE;
        else if (request->recordChangeNumber != 0x0000)
            errorcode = RESPONSE_INVALID_RECORD_CHANGE_NUMBER;
        if (errorcode) {
            // send the error response
            mctp_transmit_frame_Start(sizeof(pldm_response_header)+sizeof(GetPdrResponse) + 5-1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            mctp_transmit_frame_data(&errorcode,1);  // response->completionCode = errorcode;
            transmit_long(0);                       // response->nextrecord_handle = 0;
            transmit_long(0);                       // response->next_record_transfer_handle;
            transmit_byte(0);                       // response->transfer_flag = 0;
            transmit_short(0);                      // response->response_count = 0;
            mctp_transmit_frame_end();
            return;
        }
        if (request->request_count + request->data_transfer_handle >= get_pdr_offset(request->record_handle)+pdr_size(request->record_handle)+1) {
            // transfer end part of the data
            mctp_transmit_frame_Start( sizeof(pldm_response_header) + sizeof(GetPdrResponse) + pdr_size(request->record_handle) -
                (request->data_transfer_handle-get_pdr_offset(request->record_handle)) + 5 - 1 + 1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
            transmit_long((get_next_record(request->record_handle) <= PDR_NUMBER_OF_RECORDS) ?
                get_next_record(request->record_handle) : 0);
            transmit_long(0);                      // next data transfer handle
            transmit_byte(0x04);                   // response->transfer_flag = 0x04;   end
            transmit_short(pdr_size(request->record_handle) -
                (request->data_transfer_handle-get_pdr_offset(request->record_handle))); 
            unsigned int extraction_point = request->data_transfer_handle;
            // insert the pdr
            for (int i = 0;i < pdr_size(request->record_handle) -
                (request->data_transfer_handle-get_pdr_offset(request->record_handle));i++) {
                unsigned char byte = pgm_read_byte(&(__pdr_data[extraction_point++])); 
                transmit_byte(byte);
                crc8 = calc_new_crc8(crc8, byte);
            }
            transmit_byte(crc8);
            mctp_transmit_frame_end();
            pdrTxState = 0;
            return;
        }
        // send the middle data (multi-part)
        mctp_transmit_frame_Start( sizeof(pldm_response_header) + sizeof(GetPdrResponse) + request->request_count + 5-1,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
        transmit_long((get_next_record(request->record_handle) <= PDR_NUMBER_OF_RECORDS) ?
            get_next_record(request->record_handle) : 0);
        transmit_long(request->data_transfer_handle + request->request_count); // next data transfer handle
        transmit_byte(0x01);                   // response->transfer_flag = 0x01;   middle
        transmit_short(request->request_count); 
        unsigned int extraction_point = request->data_transfer_handle;
        // insert the pdr
        for (int i = 0;i < request->request_count;i++) {
            unsigned char byte = pgm_read_byte(&(__pdr_data[extraction_point++])); 
            transmit_byte(byte);
            crc8 = calc_new_crc8(crc8, byte);
        }
        mctp_transmit_frame_end();
        pdrTxState = 1;
        pdrNextHandle = request->data_transfer_handle + request->request_count;
        return;
    }
}

/**
 * @brief Handle the PLDM Get FRU Record Table command with transfer support.
 *
 * Implements the FRU transfer state machine for single and multi-part
 * transfers, building response frames in the transmit buffer.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void process_command_fru_table(pldm_request_header* rx_header) 
{
    static char fru_tx_state = 0;
    static unsigned int fru_next_handle;

    unsigned long data_transfer_handle = *((long*)(mctp_context.rxBuffer + sizeof(*rx_header)));
    unsigned char transferOperationFlag  = *(mctp_context.rxBuffer + sizeof(*rx_header) + sizeof(unsigned long));
    unsigned char errorcode = 0;
    const unsigned short request_count = 32;
    unsigned char padding = ((unsigned char)FRU_TOTAL_SIZE&0x03);
    if (padding) padding = 4-padding;

    switch (fru_tx_state) {
    case 0: // transfer has not begun yet
        if (transferOperationFlag != 0x1)
            errorcode = RESPONSE_INVALID_TRANSFER_OPERATION_FLAG;
        else if (data_transfer_handle != 0x0000)
            errorcode = RESPONSE_INVALID_DATA_TRANSFER_HANDLE;
        if (errorcode) {
            // send the error response
            mctp_transmit_frame_Start(sizeof(pldm_response_header)+ 6 + 5-1, 1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            mctp_transmit_frame_data(&errorcode,1);  // response->completionCode = errorcode;
            transmit_long(0);                       // response->NextTransferHandle;
            transmit_byte(0);                       // response->transfer_flag = 0;
            mctp_transmit_frame_end();
            return;
        }
        if (request_count >= FRU_TABLE_MAXIMUM_SIZE + padding) {          
            // send the data (single part)
            mctp_transmit_frame_Start( sizeof(pldm_response_header) + 6 + FRU_TOTAL_SIZE + padding + 4 + 5-1, 1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_SUCCESS);       // response->completionCode = RESPONSE_SUCCESS;
            transmit_long(0);                      // response->nextdata_transfer_handle = 0;
            transmit_byte(0x05);                   // response->transfer_flag = 0x05;   // start and end
            // send the FRU data
            for (int i = 0;i < FRU_TOTAL_SIZE; i++) {
                transmit_byte(pgm_read_byte(&(__fru_data[i])));
            }
            // send padding bytes if required
            for (int i = 0;i < padding; i++) {
                transmit_byte(0x00);
            }
            transmit_long(0x00);                   // TODO: calculate and send CRC
            mctp_transmit_frame_end();
            return;
        }
        // start sending the data (multi-part)
        mctp_transmit_frame_Start( sizeof(pldm_response_header) + 6 + request_count + 5-1,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
        transmit_long(data_transfer_handle + request_count);  // next data transfer handle
        transmit_byte(0x00);                               // response->transfer_flag = 0x0;   // start
        // insert the pdr
        for (int i = 0;i < request_count;i++) {
            unsigned char byte = pgm_read_byte(&(__fru_data[data_transfer_handle++])); 
            transmit_byte(byte);
        }
        mctp_transmit_frame_end();
        fru_tx_state = 1;
        fru_next_handle = data_transfer_handle;
        return;
    case 1: // transfer has already begun
        if (transferOperationFlag != 0x0) errorcode = RESPONSE_INVALID_TRANSFER_OPERATION_FLAG;
        else if (data_transfer_handle != fru_next_handle) errorcode = RESPONSE_INVALID_DATA_TRANSFER_HANDLE;
        if (errorcode) {
            // send the error response
            mctp_transmit_frame_Start(sizeof(pldm_response_header)+ 6 + 5-1, 1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            mctp_transmit_frame_data(&errorcode,1);  // response->completionCode = errorcode;
            transmit_long(0);                       // response->NextTransferHandle;
            transmit_byte(0);                       // response->transfer_flag = 0;
            mctp_transmit_frame_end();
            return;
        }
        if (request_count + data_transfer_handle >= FRU_TOTAL_SIZE + padding) {
            // transfer end part of the data
            mctp_transmit_frame_Start( sizeof(pldm_response_header) + 10 + FRU_TOTAL_SIZE + padding -
                data_transfer_handle + 5 - 1 + 1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
            transmit_long(0);                      // next data transfer handle
            transmit_byte(0x04);                   // response->transfer_flag = 0x04;   end
            // send the FRU data
            for (int i = 0;i < FRU_TOTAL_SIZE; i++) {
                transmit_byte(pgm_read_byte(&(__fru_data[data_transfer_handle++])));
            }
            // send padding bytes if required
            for (int i = 0;i < padding; i++) {
                transmit_byte(0x00);
            }
            transmit_long(0x00);                   // TODO: calculate and send CRC
            mctp_transmit_frame_end();
            fru_tx_state = 0;
            return;
        }
        // send the middle data (multi-part)
        mctp_transmit_frame_Start( sizeof(pldm_response_header) + 6 + request_count + 5-1,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(RESPONSE_SUCCESS);        // response->completionCode = RESPONSE_SUCCESS;
        transmit_long(data_transfer_handle + request_count); // next data transfer handle
        transmit_byte(0x01);                   // response->transfer_flag = 0x01;   middle
        for (int i = 0;i < request_count;i++) {
            unsigned char byte = pgm_read_byte(&(__pdr_data[data_transfer_handle++])); 
            transmit_byte(byte);
        }
        mctp_transmit_frame_end();
        fru_tx_state = 1;
        fru_next_handle = data_transfer_handle;
        return;
    }
}

/**
 * @brief Set state effecter states via entity callbacks.
 *
 * Delegates the SetStateEffecterStates request to configured entity
 * implementations and sends a PLDM completion response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void set_state_effecter_states(pldm_request_header* rx_header) {
    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_set_state_effecter_states(rx_header);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_set_state_effecter_states(rx_header);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_set_state_effecter_states(rx_header);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_set_state_effecter_states(rx_header);
    #endif
    
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_end();
} 

/**
 * @brief Set state effecter enables via entity callbacks.
 *
 * Delegates the SetStateEffecterEnables request to configured entity
 * implementations and transmits the PLDM completion response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void set_state_effecter_enables(pldm_request_header* rx_header) {
    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_set_state_effecter_enables(rx_header);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_set_state_effecter_enables(rx_header);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_set_state_effecter_enables(rx_header);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_set_state_effecter_enables(rx_header);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_end();
} 

/**
 * @brief Retrieve a state sensor reading via entity callbacks.
 *
 * Invokes the configured entity to obtain the sensor reading payload
 * and size, then packages and transmits the PLDM response frame.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void get_state_sensor_reading(pldm_request_header* rx_header) {
    unsigned char body[10];
    unsigned char size;

    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_get_state_sensor_reading(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_get_state_sensor_reading(rx_header, body, &size);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_get_state_sensor_reading(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_get_state_sensor_reading(rx_header, body, &size);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5 + size,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);         // completion code
        mctp_transmit_frame_data(body,size);
        mctp_transmit_frame_end();
} 

/**
 * @brief Retrieve state effecter states via entity callbacks.
 *
 * Calls into the configured entity implementation to obtain effecter
 * states and transmits the PLDM response with the returned data.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void get_state_effecter_states(pldm_request_header* rx_header) {
    unsigned char body[10];
    unsigned char size;

    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_get_state_effecter_states(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_get_state_effecter_states(rx_header, body, &size);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_get_state_effecter_states(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_get_state_effecter_states(rx_header, body, &size);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5 + size,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);         // completion code
        mctp_transmit_frame_data(body,size);
        mctp_transmit_frame_end();
} 

/**
 * @brief Set a numeric effecter value via entity callbacks.
 *
 * Delegates the SetNumericEffecterValue request to entity implementations
 * and sends the PLDM completion response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void set_numeric_effecter_value(pldm_request_header* rx_header) {
    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_set_numeric_effecter_value(rx_header);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_set_numeric_effecter_value(rx_header);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_set_numeric_effecter_value(rx_header);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_set_numeric_effecter_value(rx_header);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_end();    
}

/**
 * @brief Get a numeric effecter value via entity callbacks.
 *
 * Invokes the configured entity implementation to fill the response body
 * with the current numeric effecter value and transmits the PLDM response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void get_numeric_effecter_value(pldm_request_header* rx_header) {
    unsigned char body[10];
    unsigned char size;

    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_get_numeric_effecter_value(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_get_numeric_effecter_value(rx_header, body, &size);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_get_numeric_effecter_value(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_get_numeric_effecter_value(rx_header, body, &size);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + size + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_data(body,size);
        mctp_transmit_frame_end();
}

/**
 * @brief Retrieve a numeric sensor reading via entity callbacks.
 *
 * Calls the configured entity to produce the sensor reading payload and
 * transmits the PLDM response containing the data.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void get_sensor_reading(pldm_request_header* rx_header) {
    unsigned char body[10];
    unsigned char size;

    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_get_sensor_reading(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_get_sensor_reading(rx_header, body, &size);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_get_sensor_reading(rx_header, body, &size);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_get_sensor_reading(rx_header, body, &size);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + size + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_data(body,size);
        mctp_transmit_frame_end();
}

/**
 * @brief Set numeric sensor enable via entity callbacks.
 *
 * Delegates the SetNumericSensorEnable request to entity implementations
 * and transmits the corresponding PLDM completion response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void set_numeric_sensor_enable(pldm_request_header* rx_header) {
    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_set_numeric_sensor_enable(rx_header);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_set_numeric_sensor_enable(rx_header);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_set_numeric_sensor_enable(rx_header);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_set_numeric_sensor_enable(rx_header);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_end();
}

/**
 * @brief Set numeric effecter enable via entity callbacks.
 *
 * Delegates the SetNumericEffecterEnable request to configured entities
 * and transmits the PLDM completion response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void set_numeric_effecter_enable(pldm_request_header* rx_header) {
    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_set_numeric_effecter_enable(rx_header);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_set_numeric_effecter_enable(rx_header);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_set_numeric_effecter_enable(rx_header);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_set_numeric_effecter_enable(rx_header);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_end();
}

/**
 * @brief Set state sensor enables via entity callbacks.
 *
 * Delegates SetStateSensorEnables requests to entities and sends the
 * PLDM completion response.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
static void set_state_sensor_enables(pldm_request_header* rx_header) {
    #ifdef ENTITY_STEPPER1
        unsigned char response = entity_stepper1_set_state_sensor_enables(rx_header);
    #endif
    #ifdef ENTITY_SERVO1
        unsigned char response = entity_servo1_set_state_sensor_enables(rx_header);
    #endif
    #ifdef ENTITY_PID1
        unsigned char response = entity_pid1_set_state_sensor_enables(rx_header);
    #endif
    #ifdef ENTITY_SIMPLE1
        unsigned char response = entity_simple1_set_state_sensor_enables(rx_header);
    #endif

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response);   // completion code
        mctp_transmit_frame_end();
}

/**
 * @brief Set the terminus ID (TID) for this node.
 *
 * Extracts the requested TID from the request payload and updates the
 * local `tid` variable. Replies with an appropriate completion code.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void set_tid(pldm_request_header* rx_header) {
    tid = *((uint8*)(((char*)rx_header)+sizeof(pldm_request_header)));
    unsigned char response_code = RESPONSE_SUCCESS;
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5,1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        mctp_transmit_frame_end();
}

/**
 * @brief Get the terminus ID (TID) for this node.
 *
 * Responds with the currently configured TID in a PLDM response frame.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void get_tid(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        transmit_byte(tid);
        mctp_transmit_frame_end();
}

/**
 * @brief Respond with the PLDM version supported by this node.
 *
 * Constructs and transmits a PLDM GetPLDMVersion response frame.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void get_pldm_version(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 13 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        transmit_long(0x00000000);      // next transfer handle
        transmit_byte(0x05);            // start and end
        transmit_long(0xF1F0F000);      // Version 1.0.0.0
        transmit_long(0x4A868FFB);      // CRC32 of the
        mctp_transmit_frame_end();
}

/**
 * @brief Respond with the PLDM types supported by this node.
 *
 * Sends a PLDM GetPLDMTypes response indicating supported types.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void get_pldm_types(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 8 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        transmit_byte(0x15);            // types 0-7 (base, platform management, fru supported)
        transmit_byte(0x00);            // types 8-15
        transmit_byte(0x00);            // types 6-23
        transmit_byte(0x00);            // types 24-31
        transmit_byte(0x00);            // types 32-39
        transmit_byte(0x00);            // types 40-47
        transmit_byte(0x00);            // types 48-55
        transmit_byte(0x00);            // types 56-63
        mctp_transmit_frame_end();
}

/**
 * @brief Respond with the PLDM commands supported by this node.
 *
 * Builds and transmits a PLDM GetPLDMCommands response for the
 * requested PLDM type.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void get_pldm_commands(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 32 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);  // completion code   
        switch(rx_header->flags2&0x3F) {
            case 0:
                // pldm command and discovery
                transmit_long(0x0000003E);
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                break;
            case 2:
                // pldm for platform management and control
                transmit_long(0x007F1810);
                transmit_long(0x07070003);
                transmit_long(0x00030000);
                transmit_long(0x00000000);
                break;
            case 4:
                // pldm for fru
                transmit_long(0x00000006);
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                break;
            default:
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                transmit_long(0x00000000);
                break;
        }
        mctp_transmit_frame_end();
}

/**
 * @brief Respond with the terminus UUID for this node.
 *
 * Transmits the node's UUID in a PLDM response frame.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void get_uuid(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 16 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        for (int i=0;i<16;i++) transmit_byte(pgm_read_byte(uuid_bytes[i]));
        mctp_transmit_frame_end();
}

/**
 * @brief Handle EventMessageSupported requests (polling mode supported).
 *
 * Responds with the device's event support capabilities. Currently
 * only polling mode is reported as supported.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void process_command_event_message_supported(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 4 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        transmit_byte(global_event_enable_state);
        transmit_byte(0x04);  // polled mode support only
        transmit_byte(0x01);  // one class of event generated
        transmit_byte(0x00);  // sensor event class generated
        mctp_transmit_frame_end();
}

/**
 * @brief Handle SetEventReceiver requests.
 *
 * Enables or disables event reporting per the request payload and
 * responds with the appropriate PLDM completion code.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void process_set_event_receiver(pldm_request_header* rx_header) {
    unsigned char enable = *((char*)(rx_header+1));

    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5, 1);
    transmit_byte(rx_header->flags1 & 0x7f);
    transmit_byte(rx_header->flags2);
    transmit_byte(rx_header->command);
    if ((enable==0)||(enable==2)) transmit_byte(RESPONSE_SUCCESS);
    else transmit_byte(RESPONSE_ENABLE_METHOD_NOT_SUPPORTED);

    global_event_enable_state = 0;
    if (enable==2) global_event_enable_state = 1;
}

/**
 * @brief Handle PollForPlatformEvent requests and deliver polled events.
 *
 * If events are enabled, this function will either acknowledge an
 * existing queued event or call the entity to respond with event data.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void process_poll_for_platform_event(pldm_request_header* rx_header) {
    if (!global_event_enable_state) {
        // send error if events are not enabled
        mctp_transmit_frame_Start(sizeof(pldm_request_header) + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(RESPONSE_ERROR);   // completion code
        mctp_transmit_frame_end();
        return;
    }
    
    // note: for simplicity, this function sends events as a
    // single transfer.  It is assumed that acknowledgements
    // always are targeted at the correct ID.
    unsigned char transferOperation = *(((char*)(rx_header+1))+1);

        if (transferOperation==0x02) {
        // acknowledge only
        if (event_fifo_insert_id!=event_fifo_extract_id) {    
            // there are items in the fifo - call the entity to
            // acknowledge the current event
#ifdef ENTITY_SIMPLE1
            entity_simple1_acknowledge_event(event_fifo_extract_id);
#endif
#ifdef ENTITY_STEPPER1
            entity_stepper1_acknowledge_event(event_fifo_extract_id);
#endif
            // "remove the event from the fifo"
            event_fifo_extract_id = (event_fifo_extract_id+1)&0xF;
        } 
        // send the response
        mctp_transmit_frame_Start(sizeof(pldm_request_header) + 3 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(RESPONSE_SUCCESS);   // completion code
        transmit_byte(tid);
        if (event_fifo_insert_id!=event_fifo_extract_id) transmit_short(0xFFFF); 
        else transmit_short(0x0000); 
        mctp_transmit_frame_end();
    } else {
        if (event_fifo_insert_id!=event_fifo_extract_id) {
            // there are items in the FIFO - call the entity to
            // respond to this request
#ifdef ENTITY_SIMPLE1
            entity_simple1_respond_to_poll_event(rx_header, event_fifo_insert_id, event_fifo_extract_id);
#endif
#ifdef ENTITY_STEPPER1
            entity_stepper1_respond_to_poll_event(rx_header, event_fifo_insert_id, event_fifo_extract_id);
#endif
        } else {
            // send the response - there was nothing to retrieve
            mctp_transmit_frame_Start(sizeof(pldm_request_header) + 3 + 1 + 5, 1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_SUCCESS);   // completion code
            transmit_byte(tid);
            transmit_short(0x00); 
            mctp_transmit_frame_end();
        }
    }
}

/**
 * @brief Respond to a Get FRU Table Metadata command.
 *
 * Constructs and transmits a Get FRU Table Metadata response using
 * the provided request header information.
 *
 * @param rx_header Pointer to the received PLDM request header.
 * @return void
 */
void get_fru_table_metadata(pldm_request_header* rx_header) {
    unsigned char response_code = RESPONSE_SUCCESS;
    
    // send the response
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 18 + 1 + 5, 1);
        transmit_byte(rx_header->flags1 & 0x7f);
        transmit_byte(rx_header->flags2);
        transmit_byte(rx_header->command);
        transmit_byte(response_code);   // completion code
        transmit_byte(0x01);  // major version
        transmit_byte(0x00);  // minor version
        transmit_long(FRU_TABLE_MAXIMUM_SIZE); 
        transmit_long(FRU_TOTAL_SIZE); 
        transmit_short(FRU_TOTAL_RECORD_SETS);
        transmit_short(FRU_NUMBER_OF_RECORDS);
        transmit_long(0x00000000);  // CRC32 - TODO: Calculate this checksum       
        mctp_transmit_frame_end();
}

/**
 * @brief Parse a received PLDM command and dispatch the handler.
 *
 * Inspects the PLDM request stored in the RX buffer and invokes the
 * appropriate command handler. Handlers may construct responses in the
 * transmit buffer.
 *
 * @return void
 */
static void parse_command()
{
    // cast the relevant portions of the header so that
    // they are easier to use later.
    pldm_request_header* rx_header = (pldm_request_header*)mctp_getPacket();

    // switch based on the command and type in the header
    if (((rx_header->flags2)&0x3f)==0) {
        // PLDM Messanging Control and Discovery
        switch (rx_header->command) {
        case CMD_GET_TID:
            get_tid(rx_header);
            break;
        case CMD_SET_TID:
            set_tid(rx_header);
            break;
        case CMD_GET_PLDM_VERSION:
            get_pldm_version(rx_header);
            break;
        case CMD_GET_PLDM_TYPES:
            get_pldm_types(rx_header);
            break;
        case CMD_GET_PLDM_COMMANDS:
            get_pldm_commands(rx_header);
            break;
        default:
            mctp_transmit_frame_Start(sizeof(pldm_request_header) + 5 + 1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_ERROR_UNSUPPORTED_PLDM_CMD);   // completion code
            break;
        }
    } else if (((rx_header->flags2)&0x3f)==2) {
        // PLDM for Platform Monitoring and Control
        switch (rx_header->command) {
        case CMD_GET_TERMINUS_UID:
            get_uuid(rx_header);
            break;
        case CMD_GET_SENSOR_READING:
            get_sensor_reading(rx_header);
            break;
        case CMD_SET_NUMERIC_SENSOR_ENABLE:
            set_numeric_sensor_enable(rx_header);
            break;
        case CMD_GET_STATE_SENSOR_READINGS:
            get_state_sensor_reading(rx_header);
            break;
        case CMD_SET_STATE_SENSOR_ENABLES:
            set_state_sensor_enables(rx_header);
            break;
        case CMD_SET_NUMERIC_EFFECTER_VALUE:
            set_numeric_effecter_value(rx_header);
            break;
        case CMD_GET_NUMERIC_EFFECTER_VALUE:
            get_numeric_effecter_value(rx_header);
            break;
        case CMD_SET_STATE_EFFECTER_STATES:
            set_state_effecter_states(rx_header);
            break;
        case CMD_GET_STATE_EFFECTER_STATES:
            get_state_effecter_states(rx_header);
            break;
        case CMD_GET_PDR_REPOSITORY_INFO:
        {
            mctp_transmit_frame_Start(sizeof(GetPdrRepositoryInfoResponse) + sizeof(pldm_request_header) + 5,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_SUCCESS);   // completion code
            transmit_byte(0);                  // repository state = available
            for (int i=0;i<13;i++) transmit_byte(0); // update time
            for (int i=0;i<13;i++) transmit_byte(0); // oem update time
            transmit_long(PDR_NUMBER_OF_RECORDS);            // pdr record count
            transmit_long(PDR_TOTAL_SIZE);   // repository size
            transmit_long(PDR_MAX_RECORD_SIZE);  // record size
            transmit_byte(0);                  // no timeout
            mctp_transmit_frame_end();
            break;
        }
        case CMD_GET_PDR:
            process_command_get_pdr(rx_header);
            break;
        case CMD_SET_NUMERIC_EFFECTER_ENABLE:
            set_numeric_effecter_enable(rx_header);
            break;
        case CMD_SET_STATE_EFFECTER_ENABLES:
            set_state_effecter_enables(rx_header);
            break;
        case CMD_EVENT_MESSAGE_SUPPORTED:
            process_command_event_message_supported(rx_header);
            break;
        case CMD_POLL_FOR_PLATFORM_EVENT_MESSAGE:
            process_poll_for_platform_event(rx_header);
            break;
        case CMD_SET_EVENT_RECEIVER:
            process_set_event_receiver(rx_header);
            break;
        default:
            mctp_transmit_frame_Start(sizeof(pldm_request_header) + 5 + 1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_ERROR_UNSUPPORTED_PLDM_CMD);   // completion code
            break;
        }
    } else if (((rx_header->flags2)&0x3f)==4) {
        // PLDM for FRU Data
        switch (rx_header->command) {
        case CMD_GET_FRU_TABLE_METADATA:
            get_fru_table_metadata(rx_header);
            break;
        case CMD_GET_FRU_RECORD_TABLE:
            process_command_fru_table(rx_header); 
            break;
        default:
            mctp_transmit_frame_Start(sizeof(pldm_request_header) + 5 + 1,1);
            transmit_byte(rx_header->flags1 & 0x7f);
            transmit_byte(rx_header->flags2);
            transmit_byte(rx_header->command);
            transmit_byte(RESPONSE_ERROR_UNSUPPORTED_PLDM_CMD);   // completion code
            break;
        }
    } 
    return;
}

/**
 * @brief Public API to send a PLDM command to the connector node.
 *
 * Packages the provided request header and command body into an MCTP
 * transmit frame and starts transmission.
 *
 * @param hdr Pointer to the PLDM request header.
 * @param command Pointer to the command body bytes.
 * @param size Number of bytes in the command body.
 * @return void
 */
void node_put_command(pldm_request_header* hdr, unsigned char* command, unsigned int size) {
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + size +5,1);
    mctp_transmit_frame_data((unsigned char *)hdr,sizeof(pldm_request_header));
    mctp_transmit_frame_data(command,size);
    mctp_transmit_frame_end();
}

/**
 * @brief Process received PLDM/MCTP frames and return a response pointer.
 *
 * Advances the PLDM RX finite state machine. If a PLDM request is ready
 * it will be parsed and a response prepared; this function returns a
 * pointer to the most recent message response buffer when available.
 * If no packet is available, returns NULL. MCTP control packets are
 * consumed by the handler.
 *
 * @return unsigned char* Pointer to the most recent message response, or NULL.
 */
unsigned char* node_get_response(void) {
    if (!mctp_isPacketAvailable()) {
        mctp_updateRxFSM();
        return 0;
    } else {
        parse_command();               // process the command
        return mctp_getPacket();  // clear the packet ready flag
    }
}

/**
 * @brief Send a numeric sensor event frame.
 *
 * Builds and transmits a PLDM platform event message for a numeric
 * sensor. Intended to be invoked by entity instances from a
 * high-priority loop when a sensor condition requires reporting.
 *
 * @param rx_header Pointer to a PLDM request header (used as template for header fields).
 * @param more Set to non-zero if more events will follow in the sequence.
 * @param egi Pointer to the related EventGeneratorInstance.
 * @param sensorId ID of the sensor that produced the event.
 * @param previousEventState Prior event state for change detection.
 * @param presentReading Current sensor reading in FIXEDPOINT_24_8 format.
 * @return void
 */
void node_send_numeric_sensor_event(
    pldm_request_header *rx_header,
    unsigned char more,
    EventGeneratorInstance* egi, 
    unsigned int sensorId, 
    unsigned char previousEventState, 
    FIXEDPOINT_24_8 presentReading
) 
{  
    // begin the event frame
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 13 + 5,1);
    // transmit the header
    transmit_byte( 0x80 );    // pldm datagram request message type, instance ID 0
    transmit_byte( 0x00);     // header version = 00, pldm type = 0 (pldm messaging/discovery)
    transmit_byte( CMD_PLATFORM_EVENT_MESSAGE ); 

    // transmit the platform event message common data
    transmit_byte(0x01);         // format version
    transmit_byte(0x01);         // terminus ID
    transmit_byte(0x00);         // event class 0 = sensor

    // transmit the body
    transmit_short(sensorId);
    transmit_byte(2);            // cause = numeric sensor state change
    transmit_byte(egi->eventState);
    transmit_byte(previousEventState);
    transmit_byte(5);            // reading is a signed 32-bit integer
    transmit_long(presentReading);
    mctp_transmit_frame_end();
}

/**
 * @brief Send a state sensor event frame.
 *
 * Builds and transmits a PLDM platform event message for a state sensor.
 * Intended to be invoked by entity instances from a high-priority loop
 * when a sensor state change requires reporting.
 *
 * @param rx_header Pointer to a PLDM request header (used as template for header fields).
 * @param more Set to non-zero if more events will follow in the sequence.
 * @param egi Pointer to the related EventGeneratorInstance.
 * @param sensorId ID of the sensor that produced the event.
 * @param previousEventState Prior event state for change detection.
 * @return void
 */
void node_send_state_sensor_event(
    pldm_request_header *rx_header,
    unsigned char more,
    EventGeneratorInstance* egi, 
    unsigned int sensorId, 
    unsigned char previousEventState) {

    // begin the event frame
    mctp_transmit_frame_Start(sizeof(pldm_request_header) + 8 + 5,1);
    // transmit the header
    transmit_byte( 0x80 );    // pldm datagram request message type, instance ID 0
    transmit_byte( 0x00);     // header version = 00, pldm type = 0 (pldm messaging/discovery)
    transmit_byte( CMD_PLATFORM_EVENT_MESSAGE ); 

    // transmit the platform event message common data
    transmit_byte(0x01);         // format version
    transmit_byte(0x01);         // terminus ID
    transmit_byte(0x00);         // event class 0 = sensor

    // transmit the body
    transmit_short(sensorId);
    transmit_byte(1);            // cause = state sensor state change
    transmit_byte(egi->eventState);
    transmit_byte(previousEventState);
    
    mctp_transmit_frame_end();
}

/**
 * @brief Update event generators from the low-priority loop.
 *
 * Called from the low-priority loop to allow configured entity
 * implementations to update their event generator state. Each entity
 * may append new events to the event FIFO via the provided insert id.
 *
 * @return void
 */
void node_update_events() {
    #ifdef ENTITY_STEPPER1
    entity_stepper1_update_events(&event_fifo_insert_id);
    #endif
    #ifdef ENTITY_SIMPLE1
    entity_simple1_update_events(&event_fifo_insert_id);
    #endif
}