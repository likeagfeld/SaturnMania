#ifndef SMS_SAVE_H
#define SMS_SAVE_H

/* Phase 3: Saturn backup-RAM save slot.
 *
 * 16-byte struct stored to the Saturn's internal backup RAM (the on-board
 * 64 KB battery-backed memory at 0x00180000) via the jo_engine backup
 * module. Written at end-of-tally for every act-clear; read at boot to
 * resume from the last-completed zone.
 *
 * Layout is fixed-width / no padding-dependent fields so it round-trips
 * across rebuilds. Magic+version guards reject incompatible old saves.
 */

#define SMS_SAVE_MAGIC    0x534D5356u   /* 'SMSV' big-endian as a u32 */
#define SMS_SAVE_VERSION  1
#define SMS_SAVE_FNAME    "SMSV.0"      /* backup RAM file name (max 11 chars) */
#define SMS_SAVE_COMMENT  "SonicMania"  /* shown by the BIOS save manager     */

typedef struct {
    unsigned int   magic;          /* must equal SMS_SAVE_MAGIC */
    unsigned char  version;        /* must equal SMS_SAVE_VERSION */
    unsigned char  current_zone;   /* index into g_zones[] to resume at */
    unsigned int   score;          /* persistent run score */
    unsigned int   best_score;     /* lifetime high score across runs */
    unsigned char  lives;          /* lives remaining (Phase 4) */
    unsigned char  emeralds;       /* bitfield of collected emeralds (Phase 12) */
    unsigned char  char_select;    /* 0 = Sonic; 1/2 = Tails/Knuckles (Phase 11) */
    unsigned char  pad;            /* keeps the struct at exactly 16 bytes */
} sms_save_t;

/* Returns true if the backup device mounted successfully. Must be called
 * once at boot before any other sms_save_* function. */
int  sms_save_init(void);

/* True if SMS_SAVE_FNAME is present on the device. */
int  sms_save_exists(void);

/* Write the slot to backup RAM. Returns true on success. */
int  sms_save_write(const sms_save_t *s);

/* Read the slot into *out. Returns true on success (magic + version match). */
int  sms_save_read(sms_save_t *out);

#endif
