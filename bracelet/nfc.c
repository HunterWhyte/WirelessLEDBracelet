#include <stdbool.h>
#include <stdint.h>

#include "app_error.h"
#include "nrf_log.h"

#include "fds.h"
#include "nfc_ndef_msg.h"
#include "nfc_t4t_lib.h"

#include "bracelet.h"
#include "common.h"

/* just using 1 byte to store group data but have to receive ascii text with a header */
#define NFC_FILE_SIZE 11

__ALIGN(4) static uint8_t ndef_msg_buf[NFC_FILE_SIZE]; /* Buffer for NFC message */
__ALIGN(4) static uint8_t ndef_loaded_msg[NFC_FILE_SIZE]; /* Buffer for NFC message */

#define FILE_ID 0x1111 /* NDEF message file ID. */
#define REC_KEY 0x2222 /* NDEF message record KEY. */

__ALIGN(4) static const uint8_t default_group = 0;
__ALIGN(4) static uint8_t group = 0;

/* Flag used to indicate that FDS initialization is finished. */
static volatile bool fds_ready = false;
/* Flag used to preserve write request during Garbage Collector activity. */
static volatile bool pending_write = false;
/* Flag used to preserve update request during Garbage Collector activity. */
static volatile bool pending_update = false;
/* Pending write/update request data size. */
static uint32_t pending_msg_size = 0;
/* Pending write/update request data pointer. */
static uint8_t const* p_pending_msg_buff = NULL;
static fds_record_desc_t record_desc;
static fds_record_t record;

static void ndef_file_prepare_record(uint8_t const* p_buff, uint32_t size);
static ret_code_t ndef_file_create(uint8_t const* p_buff, uint32_t size);
static ret_code_t ndef_file_setup(void);
static ret_code_t ndef_file_update(uint8_t const* p_buff, uint32_t size);
static ret_code_t ndef_file_load(uint8_t* p_buff, uint32_t size);
static void fds_evt_handler(fds_evt_t const* const p_fds_evt);
static void nfc_callback(void* context, nfc_t4t_event_t event, const uint8_t* data,
                         size_t data_length, uint32_t flags);

static void ndef_file_prepare_record(uint8_t const* p_buff, uint32_t size) {
  // Set up record.
  record.file_id = FILE_ID;
  record.key = REC_KEY;
  record.data.p_data = p_buff;
  /* Align data length to 4 bytes. */
  record.data.length_words = BYTES_TO_WORDS(size);
}

static ret_code_t ndef_file_create(uint8_t const* p_buff, uint32_t size) {
  ret_code_t ret_code;

  /* Prepare record structure. */
  ndef_file_prepare_record(p_buff, size);

  /* Create FLASH file with NDEF message. */
  ret_code = fds_record_write(&record_desc, &record);
  NRF_LOG_INFO("fds_record_write %d", ret_code);

  if (ret_code == FDS_ERR_NO_SPACE_IN_FLASH) {
    /* If there is no space, preserve write request and call Garbage Collector. */
    pending_write = true;
    pending_msg_size = size;
    p_pending_msg_buff = p_buff;
    NRF_LOG_INFO("FDS has no free space left, Garbage Collector triggered!");
    ret_code = fds_gc();
  }

  return ret_code;
}

ret_code_t ndef_file_setup(void) {
  ret_code_t ret_code;

  ret_code = fds_register(fds_evt_handler); /* Register FDS event handler to the FDS module. */
  NRF_LOG_INFO("fds_register %d", ret_code);
  VERIFY_SUCCESS(ret_code);
  ret_code = fds_init(); /* Initialize FDS. */
  NRF_LOG_INFO("fds_init %d", ret_code);
  VERIFY_SUCCESS(ret_code);
  while (!fds_ready) {}; /* Wait until FDS is initialized. */

  return ret_code;
}

ret_code_t ndef_file_update(uint8_t const* p_buff, uint32_t size) {
  ret_code_t ret_code;

  /* Prepare record structure. */
  ndef_file_prepare_record(p_buff, size);

  /* Update FLASH file with new NDEF message. */
  ret_code = fds_record_update(&record_desc, &record);
  if (ret_code == FDS_ERR_NO_SPACE_IN_FLASH) {
    /* If there is no space, preserve update request and call Garbage Collector. */
    pending_update = true;
    pending_msg_size = size;
    p_pending_msg_buff = p_buff;
    NRF_LOG_INFO("FDS has no space left, Garbage Collector triggered!");
    ret_code = fds_gc();
  }

  return ret_code;
}

ret_code_t ndef_file_load(uint8_t* p_buff, uint32_t size) {
  ret_code_t ret_code;
  fds_find_token_t ftok;
  fds_flash_record_t flash_record;
  uint8_t new_group;

  /* Always clear token before running new file/record search. */
  memset(&ftok, 0x00, sizeof(fds_find_token_t));

  /* Search for NDEF message in FLASH. */
  ret_code = fds_record_find(FILE_ID, REC_KEY, &record_desc, &ftok);

  /* If there is no record with given key and file ID, 
     create default message and store in FLASH. */
  if (ret_code == NRF_SUCCESS) {
    NRF_LOG_INFO("Found NDEF file record.");

    /* Open record for read. */
    ret_code = fds_record_open(&record_desc, &flash_record);
    VERIFY_SUCCESS(ret_code);

    /* Access the record through the flash_record structure. */
    memcpy(ndef_loaded_msg, flash_record.p_data, flash_record.p_header->length_words * sizeof(uint32_t));

    /* Print file length and raw message data. */
    NRF_LOG_INFO("NDEF file data length: %u bytes.",
                 flash_record.p_header->length_words * sizeof(uint32_t));

    NRF_LOG_HEXDUMP_INFO(ndef_loaded_msg, flash_record.p_header->length_words * sizeof(uint32_t));
    new_group = ndef_loaded_msg[0];
    NRF_LOG_INFO("READ GROUP FROM FLASH: %d", new_group);
    if(VALID_GROUP(new_group)){
      group = new_group;
      set_group(group);
    }
    
    /* Close the record when done. */
    ret_code = fds_record_close(&record_desc);
  } else if (ret_code == FDS_ERR_NOT_FOUND) {
    NRF_LOG_INFO("NDEF file record not found, default NDEF file created.", ret_code);

    /* Create record with default NDEF message. */
    ret_code = ndef_file_create(&default_group, 1);
  }

  return ret_code;
}

static void fds_evt_handler(fds_evt_t const* const p_fds_evt) {
  ret_code_t ret_code;

  NRF_LOG_INFO("FDS event %u with result %u.", p_fds_evt->id, p_fds_evt->result);

  switch (p_fds_evt->id) {
    case FDS_EVT_INIT:
      APP_ERROR_CHECK(p_fds_evt->result);
      fds_ready = true;
      break;

    case FDS_EVT_UPDATE:
      APP_ERROR_CHECK(p_fds_evt->result);
      NRF_LOG_INFO("FDS update success.");
      break;

    case FDS_EVT_WRITE:
      APP_ERROR_CHECK(p_fds_evt->result);
      NRF_LOG_INFO("FDS write success.");
      break;

    case FDS_EVT_GC:
      APP_ERROR_CHECK(p_fds_evt->result);
      NRF_LOG_INFO("Garbage Collector activity finished.");

      /* Perform pending write/update. */
      if (pending_write) {
        NRF_LOG_INFO("Write pending msg.", p_fds_evt->id, p_fds_evt->result);
        pending_write = false;
        ret_code = ndef_file_create(p_pending_msg_buff, pending_msg_size);
        APP_ERROR_CHECK(ret_code);
      } else if (pending_update) {
        NRF_LOG_INFO("Update pending msg.", p_fds_evt->id, p_fds_evt->result);
        pending_update = false;
        ret_code = ndef_file_update(p_pending_msg_buff, pending_msg_size);
        APP_ERROR_CHECK(ret_code);
      }
      break;

    default:
      break;
  }
}

static void nfc_callback(void* context, nfc_t4t_event_t event, const uint8_t* data,
                         size_t data_length, uint32_t flags) {
  ret_code_t ret_code;
  uint8_t new_group;
  (void)context;

  switch (event) {
    case NFC_T4T_EVENT_FIELD_ON:
      /* NRF_LOG_INFO("NFC_T4T_EVENT_FIELD_ON"); */
      break;

    case NFC_T4T_EVENT_FIELD_OFF:
      /* NRF_LOG_INFO("NFC_T4T_EVENT_FIELD_OFF"); */
      break;

    case NFC_T4T_EVENT_NDEF_READ:
      /* NRF_LOG_INFO("NFC_T4T_EVENT_NDEF_READ");  */
      break;

    case NFC_T4T_EVENT_NDEF_UPDATED:
      NRF_LOG_INFO("NFC_T4T_EVENT_NDEF_UPDATED");
      if (data_length > 0) {
        NRF_LOG_HEXDUMP_INFO(ndef_msg_buf, data_length + NLEN_FIELD_SIZE);
        /* get group number from ascii data */
        if (data_length > 8) {
          new_group = (ndef_msg_buf[9] - 0x30) * 10;
          new_group += (ndef_msg_buf[10] - 0x30);
        } else {
          new_group = ndef_msg_buf[9] - 0x30;
        }

        NRF_LOG_INFO("GROUP RECEIVED: %d", new_group);
        if (VALID_GROUP(new_group)) {
          group = new_group - 1;
          /* update ANT group */
          set_group(group);
          /* save to nonvol */
          ret_code = ndef_file_update(&group, 1);
          APP_ERROR_CHECK(ret_code);
        }
        else{
          NRF_LOG_INFO("INVALID GROUP");
        }
      }
      break;
    default:
      break;
  }
}

void nfc_init(void) {
  ret_code_t ret_code;

  /* Initialize FDS. */
  ret_code = ndef_file_setup();
  NRF_LOG_INFO("ndef_file_setup %d", ret_code);
  APP_ERROR_CHECK(ret_code);

  /* Load NDEF message from the flash file. */
  ret_code = ndef_file_load(ndef_msg_buf, sizeof(ndef_msg_buf));
  NRF_LOG_INFO("ndef_file_load %d", ret_code);
  APP_ERROR_CHECK(ret_code);

  /* restore default message */
  /*
  uint32_t size = sizeof(ndef_msg_buf);
  ret_code = ndef_file_default_message(ndef_msg_buf, &size);
  APP_ERROR_CHECK(ret_code);
  ret_code = ndef_file_update(ndef_msg_buf, size);
  APP_ERROR_CHECK(ret_code);
  NRF_LOG_INFO("Default NDEF message restored!");
  */

  /* Set up NFC */
  ret_code = nfc_t4t_setup(nfc_callback, NULL);
  NRF_LOG_INFO("nfc_t4t_setup %d", ret_code);
  APP_ERROR_CHECK(ret_code);

  /* Run Read-Write mode for Type 4 Tag platform */
  ret_code = nfc_t4t_ndef_rwpayload_set(ndef_msg_buf, sizeof(ndef_msg_buf));
  NRF_LOG_INFO("nfc_t4t_ndef_rwpayload_set %d", ret_code);
  APP_ERROR_CHECK(ret_code);

  /* Start sensing NFC field */
  ret_code = nfc_t4t_emulation_start();
  NRF_LOG_INFO("nfc_t4t_emulation_start %d", ret_code);
  APP_ERROR_CHECK(ret_code);

  NRF_LOG_INFO("NFC initialized");
}