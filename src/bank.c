/*
 *   FlashBank - Program for the PC-FX to manage backup memory snapshots
 *               onto Flash memory for later retrieval
 *
 *   Copyright (C) 2022 David Shadoff
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <eris/types.h>
#include <eris/std.h>
#include <eris/v810.h>
#include <eris/king.h>
#include <eris/low/7up.h>
#include <eris/tetsu.h>
#include <eris/romfont.h>
#include <eris/bkupmem.h>
#include <eris/timer.h>
#include <eris/pad.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define JOY_I            1
#define JOY_II           2
#define JOY_III          4
#define JOY_IV           8
#define JOY_V            16
#define JOY_VI           32
#define JOY_SELECT       64
#define JOY_RUN          128
#define JOY_UP           256
#define JOY_RIGHT        512
#define JOY_DOWN         1024
#define JOY_LEFT         2048
#define JOY_MODE1        4096
#define JOY_MODE2        16384

#define TITLE_LINE       1
#define INSTRUCT_LINE    3
#define STAT_LINE        5
#define HEX_LINE         9

#define FX_BASE          0xE0000000      // memory location of start of internal backup memory
#define FXBMP_BASE       0xE8000000      // memory location of start of external backup memory
#define FLASH_BANK_BASE  81920           // within FX-BMP cart, start of 'slot' storage
#define FLASH_BANK_SIZE  (36 * 1024)     // size of 'slot' (32KB for data + 4KB for date/comment metadata)
#define FLASH_BANK_CMNT  (32 * 1024)     // location of metadata within slot
#define COMMENT_OFFSET   12              // Ddate is at the start of metadata; this is start of Comment location

#define COMMENT_LENGTH   18

#define MAX_SLOTS        12              // 12 slots fit in a 512KB Flash chip

// These FAT attributes are for the 32KB internal SRAM
// on the PC-FX; different values would be used when
// reporting on larger external memory carts
//
#define FAT_OFFSET           0x80
#define FAT_RESERVED         3
#define FAT_ENTRIES_32K      236
#define FAT_SECTOR_SIZE      128
#define FAT_DIR_OFFSET_32K   0x200
#define FAT_DIR_ENTRIES_32K  64
#define FAT_DIR_ENTRY_SIZE   32


extern void flash_erase_sector( u8 * sector);
extern void flash_write( u8 * addr, u8 value);
extern void flash_id( u8 * addr );

void printsjis(char *text, int x, int y);
void print_narrow(u32 sjis, u32 kram);
void print_wide(u32 sjis, u32 kram);

void print_at(int x, int y, int pal, char* str);
void putch_at(int x, int y, int pal, char c);
void putnumber_at(int x, int y, int pal, int digits, int value);

extern u8 font[];
extern u8 bram_mem[];
extern u8 fxbmp_mem[];
extern u8 bram_buffer[];

// interrupt-handling variables
volatile int sda_frame_count = 0;
volatile int last_sda_frame_count = 0;

/* HuC6270-A's status register (RAM mapping). Used during VSYNC interrupt */
volatile uint16_t * const MEM_6270A_SR = (uint16_t *) 0x80000400;


char buffer[2048];
char dir_entry[64][20]; // up to 64 entries of 19 characters (plus null terminator) each (in FAT)
u32  num_dir_entries;

// Flash memory identifcation and usage:
u8   chip_id[4];        // first two bytes are buffer for returning flash chip identity
int  page;
//u32  flashbank_addr;

// Used for entry of date and name
//
char card_date[12];    /* this is latest date stored on card */
char today_date[12];   /* this is date from user input */
char default_date[12]; /* this is the initial/base date on an empty card */

const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
const char letter_display[] =
   {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890.,-[_]'!#%      "};
                              /* "<BCK> <SP> <END>" */

char name[12];
char date[12];
char today_comment[COMMENT_LENGTH + 2];
char comment_buf[128];
char date_buf[16];

char comment[128];     /* this is used as 'Name', but only part is currently used */
                       /* (user interface might become more difficult if more is used) */

int year;
int month;
int day;

int end_addr;
int next_free;
int offset_ptr;
int save_size;

//char countdown;
int advance;
int menu_A;
int menu_B;
int confirm;
int menu_level;
int date_level;

int stepval = 0;

/* these are read/calculated from the card */
/* at start and after each major operation */
int bram_free;
int banks_in_use;
int bram_formatted;
u8 flash_formatted[MAX_SLOTS];

//u8 flash_free[MAX_SLOTS];


///////////////////////////////// Joypad routines
volatile u32 joypad;
volatile u32 joypad_last;
volatile u32 joytrg;

__attribute__ ((noinline)) void joyread(void)
{
u32 temp;

   joypad_last = joypad;
   temp = eris_pad_read(0);

   if ((temp >> 28) == PAD_TYPE_FXPAD) {  // PAD TYPE
      joypad = temp;
      joytrg = (~joypad_last) & joypad;
   }
   else {
      joypad = 0;
      joytrg = 0;
   }
}

///////////////////////////////// Interrupt handler
__attribute__ ((interrupt_handler)) void my_vblank_irq (void)
{
   uint16_t vdc_status = *MEM_6270A_SR;

   if (vdc_status & 0x20) {
      sda_frame_count++;
   }
   joyread();
}

void vsync(int numframes)
{
   while (sda_frame_count < (last_sda_frame_count + numframes + 1));

   last_sda_frame_count = sda_frame_count;
}


///////////////////////////////// CODE

//
// for setting breakpoints - add a call to step() and breakpoint on it
// or watchpoint on stepval.
//
__attribute__ ((noinline)) void step(void)
{
   stepval++;;
}
//////////

u8 * calc_bank_addr(int banknum)
{
   int offset;

   offset = (FLASH_BANK_BASE + (banknum * FLASH_BANK_SIZE)) * 2;
   offset += FXBMP_BASE;

   return( (u8 *) offset);
}

u8 * calc_bank_annotate_addr(int banknum)
{
   int offset;

   offset = (FLASH_BANK_BASE + (banknum * FLASH_BANK_SIZE) + FLASH_BANK_CMNT) * 2;
   offset += FXBMP_BASE;

   return( (u8 *) offset);
}

void buffer_to_bram()
{
int i;

   for (i = 0; i < 32768; i++)
   {
      bram_mem[(i<<1)] = bram_buffer[i];
   }
}


void buffer_to_flash(u8 * target)
{
int i;

   // erase storage slot (8 sectors data + 1 sector comments)
   //
   for (i = 0; i < 9; i++)
   {
      flash_erase_sector( target + ((i<<1) * 4096));
   }

   // write the core 32KB data into the storage slot
   // 
   for (i = 0; i < 32768; i++)
   {
      // add special programming preface
//      *(target + (i<<1)) = bram_buffer[i];
      flash_write( (target + (i<<1)), bram_buffer[i]);
   }

   // add storage of metadata (data / comment)
   for (i = 0; i < 11; i++)
   {
//      *(target + (FLASH_BANK_CMNT * 2) + (i * 2)) = date[i];
      flash_write( (target + (FLASH_BANK_CMNT * 2) + (i * 2)), date[i]);
   }
//   *(target + (FLASH_BANK_CMNT * 2) + (11 * 2)) = 0;
   flash_write( (target + (FLASH_BANK_CMNT * 2) + (11 * 2)), 0);


   for (i = 0; i < COMMENT_LENGTH; i++)
   {
//      *(target + ((FLASH_BANK_CMNT + COMMENT_OFFSET) * 2) + (i * 2)) = comment[i];
      flash_write( (target + ((FLASH_BANK_CMNT + COMMENT_OFFSET) * 2) + (i * 2)), comment[i]);
   }
//   *(target + ((FLASH_BANK_CMNT + COMMENT_OFFSET) * 2) + (16 * 2)) = 0;
   flash_write( (target + ((FLASH_BANK_CMNT + COMMENT_OFFSET) * 2) + (COMMENT_LENGTH * 2)), 0);
}

void copy_to_buffer(u8 * source)
{
int i;

   for (i = 0; i < 32768; i++)
   {
      bram_buffer[i] = *(source + (i<<1));
   }
}

void copy_annotate_to_buffer(u8 * source)
{
int i;

   for (i = 0; i < 11; i++)
   {
      date_buf[i] = *(source + (i<<1));
   }
   for (i = 0; i < COMMENT_LENGTH; i++)
   {
      comment_buf[i] = *(source + (COMMENT_OFFSET * 2) + (i<<1));
   }
}

int check_buffer_free()
{
int i;
int entrya, entryb;
int available = 0;
int start_addr, end_addr;

   start_addr = FAT_OFFSET + FAT_RESERVED;
   end_addr = start_addr + (FAT_ENTRIES_32K * 3 / 2);

   for (i = start_addr; i < end_addr; i += 3)
   {
      entrya = ((bram_buffer[i+1] & 0xf) << 8) + bram_buffer[i];
      entryb = (bram_buffer[i+2] << 4) + ((bram_buffer[i+1] & 0xf0) >> 4);

      if (entrya == 0)
         available += FAT_SECTOR_SIZE;

      if (entryb == 0)
         available += FAT_SECTOR_SIZE;
   }

   return(available);
}

void get_buffer_directory(void)
{
int i;
int j, k;
int start_addr, end_addr;

   num_dir_entries = 0;     // clear previous records
   for (j = 0; j < 64; j++)
   {
      for (k = 0; k < 20; k++)
      {
         dir_entry[j][k] = 0;
      }
   }

   start_addr = FAT_DIR_OFFSET_32K;
   end_addr = start_addr + (FAT_DIR_ENTRIES_32K * FAT_DIR_ENTRY_SIZE);

   for (i = start_addr; i < end_addr; i += FAT_DIR_ENTRY_SIZE)
   {
      if (bram_buffer[i] == 0)
         break;

      if (bram_buffer[i] == '.')
         continue;

      if (bram_buffer[i] == 0xE5)
         continue;

      for (j = 0; j < 8; j++)
      {
         dir_entry[num_dir_entries][j] = bram_buffer[i+j];
      }
      for (j = 12; j < 21; j++)
      {
         dir_entry[num_dir_entries][j-4] = bram_buffer[i+j];
      }
      num_dir_entries++;
   }

   return;
}


int is_bram_formatted()
{
static int retval;

   if ((bram_mem[6] == 'P') && (bram_mem[8] == 'C') &&
       (bram_mem[10] == 'F') && (bram_mem[12] == 'X') &&
       (bram_mem[14] == 'S') && (bram_mem[16] == 'r') &&
       (bram_mem[18] == 'a') && (bram_mem[20] == 'm'))

      retval = 1;
   else
      retval = 0;

   return(retval);
}

u8 is_formatted(u8 * buf)
{
static int retval;

   if ((buf[6] == 'P') && (buf[8] == 'C') &&
       (buf[10] == 'F') && (buf[12] == 'X') &&
       (buf[14] == 'S') && (buf[16] == 'r') &&
       (buf[18] == 'a') && (buf[20] == 'm'))

      retval = 1;
   else
      retval = 0;

   return(retval);
}

void clear_panel(void)
{
   int i;

   for (i = INSTRUCT_LINE; i < HEX_LINE + 17; i++)
   {
      print_at(2, i, 0, "                                         ");
   }
}


void clear_buff_listing(void)
{
 int i;

   for (i = 7; i >= 0; i--)
   {
      printsjis("                    ", 3, ((9 + (i * 2)) << 3) );
   }
}

void buff_listing(void)
{
// int i, j;
 int i;
// int page;
 int page_entries;
 int breakout;
 char num_buff[7];

   vsync(2);

   clear_panel();

   if (is_formatted( (u8 *)FX_BASE ) )
   {
      print_at(4, INSTRUCT_LINE + 1, 4, "Note: Use the up/down keys to");
      print_at(4, INSTRUCT_LINE + 2, 4, "      page forward/backward");

      print_at(4, STAT_LINE + 2, 5, "File");
      print_at(4, STAT_LINE + 3, 5, "----");

      print_at(11, STAT_LINE + 2, 5, "Name");
      print_at( 9, STAT_LINE + 3, 5, "-----------------------");

      print_at(36, STAT_LINE + 2, 5, "Free");
      print_at(36, STAT_LINE + 3, 5, "----");

      putnumber_at(35, HEX_LINE, 0, 5, check_buffer_free());

      page = 0;
      breakout = 0;

      while (breakout == 0)
      {
         page_entries = num_dir_entries - (page * 8);
	 if (page_entries > 8)
            page_entries = 8;

//	 sprintf(num_buff, "%5d", num_dir_entries);
//         print_at(33, STAT_LINE - 2, 5, num_buff);
//
//	 sprintf(num_buff, "%5d", page_entries);
//         print_at(33, STAT_LINE - 1, 5,  num_buff);
//
//	 sprintf(num_buff, "%5d", page);
//         print_at(33, STAT_LINE , 5,  num_buff);

         for (i = 0; i < 8; i++)
         {
            if (i >= page_entries)
            {
               printsjis("                    ", 3, ((9 + (i * 2)) << 3) );
            }
	    else
            {
               sprintf(num_buff, "%2d", ((page * 8) + i + 1) );
	       printsjis(num_buff, 3, ((9 + (i * 2)) << 3) );

               printsjis("                 ", 6, ((9 + (i * 2)) << 3) );
	       printsjis(dir_entry[ ((page * 8) + i) ], 6, ((9 + (i * 2)) << 3) );
            }
         }

	 while (1)   // wait for keys - page up/down or exit
         {
	    vsync(0);

            if ((joytrg & JOY_DOWN) && (num_dir_entries > ((page+1) * 8))) {
               page++;
	       break;
	    }

            if ((joytrg & JOY_UP) && (page > 0)) {
               page--;
	       break;
	    }

            if ((joytrg & JOY_RUN) || (joytrg & JOY_II)) {
               breakout = 1;
               break;
	    }
	 }
      }
   }
   else
   {
      print_at(5, STAT_LINE + 2, 3, "NOT Formatted");

      while (1)   // wait for exit keys
      {
         if ((joytrg & JOY_RUN) || (joytrg & JOY_II))
            break;
      }
   }
}

void check_BRAM_status()
{
int i;
//u8 * cmnt_addr;

   banks_in_use = 0;

   for (i = 0; i < 10; i++) {
      card_date[i] = 0x00;
   }

   bram_formatted = is_bram_formatted();

   if (bram_formatted)
   {
      copy_to_buffer( bram_mem );
      bram_free = check_buffer_free();
   }
   else
      bram_free = 0;

   for (i = 0; i < MAX_SLOTS; i++)
   {
      flash_formatted[i] = is_formatted( calc_bank_addr(i) ); // need to calculate offset

      if (flash_formatted[i]) {
         banks_in_use++;

	 // get reference to annotation to get date
         copy_annotate_to_buffer( calc_bank_annotate_addr(i) );

	 //cmnt_addr = calc_bank_annotate_addr(i);
         //memcpy(date, cmnt_addr, 11);

         if ( ((date_buf[0] == '1') || (date_buf[0] == '2')) )
	 {
            if ((strcmp(card_date, date_buf) < 0))
            {
               memcpy(card_date, date_buf, 10);
            }
         }
      }
   }
}

void clear_errors(void)
{
   print_at(5, INSTRUCT_LINE+1, 0, "                                       ");
   print_at(5, INSTRUCT_LINE+2, 0, "                                       ");
   print_at(5, INSTRUCT_LINE+3, 0, "                                       ");
}

void top_menu(void)
{
static int menu_selection;
//char tempbuf[16];
//int i;

   vsync(2);

   clear_panel();

   menu_selection = 1;

   print_at(7, HEX_LINE+10, 5, "PC-FX:");

// Print "PCFXSram" (if reading properly)
//
//   for (i = 0; i < 8; i++) {
//      tempbuf[i] = bram_mem[(i*2)+6];
//   }
//   tempbuf[8] = '\0';
//   print_at(20, HEX_LINE+10, 0, tempbuf);

   if (bram_formatted)
   {
      print_at(9, HEX_LINE+11, 2, "BRAM active");
      putnumber_at(8, HEX_LINE+12, 2, 5, bram_free);
      print_at(14, HEX_LINE+12, 2, "bytes free");
   }
   else
   {
      print_at(9, HEX_LINE+11, 2, "BRAM not formatted");
   }

   print_at(7, HEX_LINE+14, 5, "MEMORY CARD:");

   print_at(9, HEX_LINE+15, 2, "Banks in use:");
   putnumber_at(25, HEX_LINE+15, 2, 2, banks_in_use);
   if (banks_in_use > 0)
   {
      print_at(9, HEX_LINE+16, 2, "Last save date: ");
      print_at(25, HEX_LINE+16, 2, card_date);
   }

   while(1)
   {
      // Print frame count (temporary)
      // 
//      sprintf(tempbuf, "%6d", sda_frame_count);
//      print_at(23, HEX_LINE+14, 0, tempbuf);

      advance = 1;

      if (menu_selection == 1)
        clear_errors();

      print_at(14, STAT_LINE + 4, ((menu_selection == 1) ? 1 : 0), " VIEW DATA ");

      if (menu_selection == 2) {
         if (!bram_formatted) {
            advance = 0;
	    print_at(7, INSTRUCT_LINE+2, 3, "Cannot save unformatted BRAM!");
	 }
	 else
            print_at(5, INSTRUCT_LINE+2, 0, "                                       ");
            print_at(6, INSTRUCT_LINE+3, 0, "                                       ");
      }

      print_at(14, STAT_LINE + 6, ((menu_selection == 2) ? 1 : 0), " SAVE TO CARD ");

      if (menu_selection == 3) {
         if (banks_in_use == 0) {
            advance = 0;
	    print_at(7, INSTRUCT_LINE+2, 3, "Cannot restore.");
            print_at(7, INSTRUCT_LINE+3, 3, "No banks contain backup data !");
	 }
	 else
	 {
            print_at(5, INSTRUCT_LINE+2, 0, "                                       ");
            print_at(6, INSTRUCT_LINE+3, 0, "                                       ");
	 }
      }

      print_at(14, STAT_LINE + 8, ((menu_selection == 3) ? 1 : 0), " RESTORE FROM CARD ");

      if (joytrg & JOY_UP) {
         menu_selection--;
	 if (menu_selection == 0)
            menu_selection = 3;
      }

      if (joytrg & JOY_SELECT) {
         menu_A = -1;
	 break;
      }

      if (joytrg & JOY_DOWN) {
         menu_selection++;
	 if (menu_selection == 4)
            menu_selection = 1;
      }

      if ((advance == 1) &&
          ((joytrg & JOY_RUN) || (joytrg & JOY_I)) )
      {
         menu_A = menu_selection;
         break;
      }

      vsync(0);
   }
}

int datestr_to_num(int offset, int len)
{
static int retval;
static int count;
static int index;

   count = len;
   index = offset;
   retval = 0;

   while (count > 0) {
      retval = (retval * 10) + (date[index] - '0');
      index++;
      count--;
   }

   return(retval);
}

void num_to_datestr(int value, int offset, int len)
{
static int count;
static int remainder;
static int tempval;

   count = len;
   remainder = value;

   while (count > 0) {
      tempval = remainder % 10;
      date[offset+count-1] = tempval + '0';
      remainder = (remainder - tempval) / 10;
      count--;
   }
}

void normalize_date(void)
{
   if ((month != 2) && (day > days_in_month[month-1]))
   {
      day = days_in_month[month-1];
   }
   else if ((month == 2) && (day > 28))
   {
      if ((year % 4) != 0) {
         day = 28;
      }
      else if (year == 2100)
         day = 28;
      else
         day = 29;
   }
}

void get_date(void)
{
static char refresh;
static char disp_str[11];

   vsync(2);

   clear_panel();

   /* If we haven't set the date yet during this runtime, */
   /* set it to max found on card */

   if ((today_date[0] != '1') && (today_date[0] != '2'))
   {
      strncpy(today_date, card_date, 11);
   }

   /* If the date is still empty, set to card release date */
   /* as a default/fall-through */

   if ((today_date[0] != '1') && (today_date[0] != '2'))
   {
      strncpy(today_date, default_date, 11);
   }

   strncpy(date, today_date, 11);

   print_at(9, HEX_LINE,   5, "Please enter today's date");
   print_at(9, HEX_LINE+2, 5, "   for filing purposes");

   year  = datestr_to_num(0,4);
   month = datestr_to_num(5,2);
   day   = datestr_to_num(8,2);

   refresh = 1;
   date_level = 1;

   while(1)
   {
      if (refresh)
      {
         num_to_datestr(year, 0,4);
         num_to_datestr(month,5,2);
         num_to_datestr(day,  8,2);

         print_at(14, HEX_LINE+6, ((date_level == 1) ? 1 : 0), " ");
	 strncpy(disp_str, date, 5);
	 disp_str[4] = 0;
         print_at(15, HEX_LINE+6, ((date_level == 1) ? 1 : 0), disp_str);
         print_at(19, HEX_LINE+6, ((date_level == 1) ? 1 : 0), " ");

	 print_at(20, HEX_LINE+6, 0, "-");

	 print_at(21, HEX_LINE+6, ((date_level == 2) ? 1 : 0), " ");
	 strncpy(disp_str, &date[5], 3);
	 disp_str[2] = 0;
         print_at(22, HEX_LINE+6, ((date_level == 2) ? 1 : 0), disp_str);
	 print_at(24, HEX_LINE+6, ((date_level == 2) ? 1 : 0), " ");

	 print_at(25, HEX_LINE+6, 0, "-");

	 print_at(26, HEX_LINE+6, ((date_level == 3) ? 1 : 0), " ");
	 strncpy(disp_str, &date[8], 3);
	 disp_str[2] = 0;
         print_at(27, HEX_LINE+6, ((date_level == 3) ? 1 : 0), disp_str);
	 print_at(29, HEX_LINE+6, ((date_level == 3) ? 1 : 0), " ");

	 refresh = 0;
      }

      if (joytrg & JOY_LEFT) {
         date_level--;
	 if (date_level < 1)
            date_level = 3;

	 /* if (date_level == 3) adjust day */
	 if (date_level == 3)
            normalize_date();

	 refresh = 1;
      }

      if (joytrg & JOY_RIGHT) {
         date_level++;
	 if (date_level > 3)
            date_level = 1;

	 /* if (date_level == 3) adjust day */
	 if (date_level == 3)
            normalize_date();

	 refresh = 1;
      }

      if (joytrg & JOY_UP) {
         if (date_level == 1) {
	    if (year < 2100) year++;
	 }
	 else if (date_level == 2) {
             if (month < 12) month++;
	 }
	 else if (date_level == 3) {
            day++;
	    normalize_date();
	 }

	 refresh = 1;
      }

      if (joytrg & JOY_DOWN) {
         if (date_level == 1) {
	    if (year > 1986) year--;
	 }
	 else if (date_level == 2) {
	     if (month > 1) month--;
	 }
	 else if (date_level == 3) {
	     if (day > 1) day--;
	 }

	 refresh = 1;
      }

      if (joytrg & JOY_II) {
         if (date_level == 1)
	 {
            date_level = -1;
	    strncpy(today_date, date, 11);
	    break;
	 }
	 else
            date_level--;

	 refresh = 1;
      }

      if ((joytrg & JOY_RUN) || (joytrg & JOY_I)) {
         if (date_level == 3)
	 {
	    strncpy(today_date, date, 11);
            break;
	 }
	 else
            date_level++;

	 refresh = 1;
      }

      vsync(0);
   }
}


void get_comment(void)
{
static char refresh;
static char current_letter;
//static char disp_str[11];
static int x_pos;
static int y_pos;
static int comment_index;
int i, j;

   vsync(2);

   clear_panel();

   x_pos = 0;
   y_pos = 0;

   strcpy(today_comment, "                  ");
   today_comment[COMMENT_LENGTH] = 0x00;
   comment_index = 0;

   print_at(11, HEX_LINE, 0, ">> __________________ <<");

   refresh = 1;

   while (1)
   {
      if (refresh)
      {
         print_at(14, HEX_LINE, 0, today_comment);

	 putch_at(comment_index+14, HEX_LINE, 1, today_comment[comment_index]);

         for (i = 0; i < 6; i++)
	 {
            for (j = 0; j < 13; j++)
	    {
               int palette;

               if ((i == y_pos) && (j == x_pos))
                  palette = 1;
	       else
                  palette = 0;


               current_letter = letter_display[(i*13)+j];

	       if ((i == 5) && (j > 6)) {
                  switch(j) {
                     case 8:
                        print_at(26, HEX_LINE+15, palette, " SPC ");
                        break;
                     case 10:
                        print_at(33, HEX_LINE+15, palette, " BCK ");
                        break;
                     case 12:
                        print_at(38, HEX_LINE+15, palette, " END ");
                        break;
		  }
	       }
	       else
	       {
                  print_at((j*3)+3, HEX_LINE+(i*2)+5, palette, " ");
                  putch_at((j*3)+4, HEX_LINE+(i*2)+5, palette, current_letter);
                  print_at((j*3)+5, HEX_LINE+(i*2)+5, palette, " ");
	       }
	    }
	 }
	 refresh = 0;
      }

      if (joytrg & JOY_LEFT)
      {
	 if (x_pos == 0)
            x_pos = 12;
	 else
            x_pos--;

	 if ((y_pos == 5) && (x_pos > 6) && ((x_pos % 2) == 1) )
            x_pos--;

	 refresh = 1;
      }

      if (joytrg & JOY_RIGHT)
      {
	 if (x_pos == 12)
            x_pos = 0;
	 else
            x_pos++;

	 if ((y_pos == 5) && (x_pos > 6) && ((x_pos % 2) == 1) )
            x_pos++;

	 refresh = 1;
      }

      if (joytrg & JOY_UP)
      {
	 if (y_pos == 0)
            y_pos = 5;
	 else
            y_pos--;

	 if (y_pos == 5) {
            if ((x_pos > 6) && ((x_pos % 2) == 1))
               x_pos--;
	 }

	 refresh = 1;
      }

      if (joytrg & JOY_DOWN)
      {
	 if (y_pos == 5)
            y_pos = 0;
	 else
            y_pos++;

	 if (y_pos == 5) {
            if ((x_pos > 6) && ((x_pos % 2) == 1))
               x_pos--;
	 }

	 refresh = 1;
      }

      if (joytrg & JOY_II) {
         date_level = -1;
         strncpy(comment, today_comment, COMMENT_LENGTH);
	 break;
      }

      if (joytrg & JOY_I) {
         if ((y_pos == 5) && (x_pos == 12))   /* END */
	 {
            strncpy(comment, today_comment, COMMENT_LENGTH);
            break;
	 }
	 else if ((y_pos == 5) && (x_pos == 10))  /* Backspace */
	 {
	    today_comment[comment_index] = ' ';
            if (comment_index > 0) {
               comment_index--;
	    }

	 }
	 else if ((y_pos == 5) && (x_pos == 8))  /* Space */
	 {
	    today_comment[comment_index++] = ' ';
	    if (comment_index == COMMENT_LENGTH)
               comment_index = COMMENT_LENGTH-1;
	 }
	 else  /* everything else */
	 {
	    today_comment[comment_index++] = letter_display[(y_pos*13)+x_pos];
	    if (comment_index == COMMENT_LENGTH)
               comment_index = COMMENT_LENGTH-1;
	 }

         refresh = 1;
      }

      vsync(0);
   }
}

void select_bank_menu(void)
{
static int menu_selection;
static int page_size = 16;
static int page_end;
static int freespace;
static int pal;
static char bottom_limit;
static char refresh;
int i;
int q;
//u8 * comment_addr;
//u8 * date_addr;

   vsync(2);

   clear_panel();

   print_at(2, HEX_LINE-2, 5, "Bank");
   print_at(2, HEX_LINE-1, 5, "----");
   print_at(7, HEX_LINE-2, 5, "Save Date");
   print_at(7, HEX_LINE-1, 5, "----------");
   print_at(19, HEX_LINE-2, 5, "Free");
   print_at(19, HEX_LINE-1, 5, "----");
   print_at(25, HEX_LINE-2, 5, "Name");
   print_at(24, HEX_LINE-1, 5, "------------------");

   if (menu_A == 1)        /* view */
   {
      menu_selection = 0;  /* BRAM is eligible for selection */
      bottom_limit = 0;
   }
   else if (menu_A == 2)   /* save - date and comment should  */
   {                       /*        have been entered by now */
      menu_selection = 1;  /* BRAM is not eligible for selection */
      bottom_limit = 1;

      print_at(8, INSTRUCT_LINE+1, 5, ">> Select a bank to SAVE to <<");
      print_at(28, INSTRUCT_LINE+1, 3, "SAVE");
   }
   else if (menu_A == 3)   /* restore */
   {
      menu_selection = 1;  /* BRAM is not eligible for selection */
      bottom_limit = 1;
   }
   else if (menu_A == 4)   /* erase */
   {
      menu_selection = 1;  /* BRAM is not eligible for selection */
      bottom_limit = 1;
   }
   refresh = 1;


   while(1)
   {
      if (refresh)     /* don't display everything every cycle */
      {
         advance = 1;  /* unless otherwise stated, allow moving to next screen */

	 page = (menu_selection-1) / 16;

	 if (menu_A != 2)
            clear_errors();

         if (bram_formatted) {
            print_at(2, HEX_LINE, ((menu_selection == 0) ? 1 : 0), "BRAM             ");
            putnumber_at(18, HEX_LINE, ((menu_selection == 0) ? 1: 0), 5, bram_free);
            print_at(23, HEX_LINE, ((menu_selection == 0) ? 1 : 0), "                   ");
         }
         else
         {
            if (menu_selection == 0) /* if unformatted and current selection, */
            {                        /* disallow moving to next screen */
               advance = 0;
               print_at(6, INSTRUCT_LINE+1, 3, "No contents to view.");
	    }

            print_at(2, HEX_LINE, ((menu_selection == 0) ? 1 : 2), "BRAM Unused                              ");
         }


	 /* note that menu selection of banks is 1-relative, */
	 /* but flash index is 0-relative */
         page_end = MIN(page_size, MAX_SLOTS);

         for (i = 0; i < page_end; i++)
         {
            if (menu_selection == ((page*page_size)+i+1) )
               pal = 1;
            else
               pal = 0;

	    for (q = 0; q < 11; q++) {
               date_buf[q] = '\0';
	    }
	    for (q = 0; q < COMMENT_LENGTH + 1; q++) {
               comment_buf[q] = '\0';
	    }

            if (is_formatted(calc_bank_addr(i))) {

               copy_to_buffer( calc_bank_addr(i) );
               copy_annotate_to_buffer( calc_bank_annotate_addr(i) );

               freespace = check_buffer_free();

               print_at(2, HEX_LINE+1+i, pal, " ");
               putnumber_at(3, HEX_LINE+1+i, pal, 2, (page*page_size)+i+1);
               print_at(5, HEX_LINE+1+i, pal, "  ");

               putnumber_at(18, HEX_LINE+1+i, pal, 5, freespace);
               print_at(23, HEX_LINE+1+i, pal, " ");

               date_buf[11] = '\0';

	       // show full length even when comment is short
               if (strlen(comment_buf) < COMMENT_LENGTH)
               {
                  for (q = strlen(comment_buf); q < COMMENT_LENGTH; q++)
                  {
                     comment_buf[q] = ' ';
                  }
               }

               comment_buf[COMMENT_LENGTH] = '\0';

               print_at(24, HEX_LINE+1+i, pal, comment_buf);

            }
            else
            {
	       /* no contents */
               print_at(2, HEX_LINE+1+i, pal, " ");
               putnumber_at(3, HEX_LINE+1+i, pal, 2, (page*page_size)+i+1);
               print_at(5, HEX_LINE+1+i, pal, "  ");

               if (menu_selection != ((page*page_size)+i+1))
               {
                  print_at(18, HEX_LINE+1+i, 2, "      Not In Use        ");
               }
	       else
               {
                  print_at(18, HEX_LINE+1+i, pal, "      Not In Use        ");

		  if (menu_A == 1)
                  {
                     advance = 0;
                     print_at(6, INSTRUCT_LINE+1, 3, "No contents to view.");
		  }
		  else if (menu_A == 3)
                  {
                     advance = 0;
                     print_at(6, INSTRUCT_LINE+1, 3, "No contents to restore.");
		  }
		  else if (menu_A == 4)
                  {
                     advance = 0;
                     print_at(6, INSTRUCT_LINE+1, 3, "No contents to erase. ");
		  }
               }
            }

            if ((date_buf[0] != '1') && (date_buf[0] != '2')) { /* i.e. year = 19xx or 20xx */
               /* no date set */
               if (menu_selection != ((page*page_size)+i+1))
               {
                  print_at(7, HEX_LINE+1+i, 2, "Not Set     ");
               }
	       else
                  print_at(7, HEX_LINE+1+i, pal, "Not Set     ");
            }
            else {
               print_at(7, HEX_LINE+1+i, pal, date_buf);
               print_at(17, HEX_LINE+1+i, pal, " ");
            }
         }
	 refresh = 0;
      }

      if (joytrg & JOY_UP) {
         menu_selection--;
	 if (menu_selection < bottom_limit)
            menu_selection = page_end;

	 refresh = 1;
      }

      if (joytrg & JOY_DOWN) {
         menu_selection++;
	 if (menu_selection > page_end)
            menu_selection = bottom_limit;

	 refresh = 1;
      }

      if (joytrg & JOY_LEFT) {
         if (menu_selection > bottom_limit) {
            menu_selection = bottom_limit;
         }

	 refresh = 1;
      }

      if (joytrg & JOY_RIGHT) {
         if (menu_selection < page_end) {
            menu_selection = page_end;
         }

	 refresh = 1;
      }

      if ((advance == 1) &&
          ((joytrg & JOY_RUN) || (joytrg & JOY_I)) )
      {
         menu_B = menu_selection;
	 break;
      }

      if (joytrg & JOY_II)
      {
         menu_B = -1;
	 break;
      }

      vsync(0);
   }
}

void confirm_menu(void)
{
static int confirm_value;

   confirm_value = 0;

   vsync(2);

   clear_panel();

   if (menu_A == 2)
   {
      print_at(16, HEX_LINE+1, 4, "Confirm ");
      print_at(24, HEX_LINE+1, 3, "SAVE");
      print_at(13, HEX_LINE+3, 4, "from Backup Memory");
      print_at(16, HEX_LINE+5, 4, "to BANK #");
      putnumber_at(25, HEX_LINE+5, 4, 2, menu_B);
      print_at(27, HEX_LINE+5, 4, " ? ");
   }
   else if (menu_A == 3)
   {
      print_at(14, HEX_LINE+1, 4, "Confirm ");
      print_at(22, HEX_LINE+1, 3, "RESTORE");
      print_at(15, HEX_LINE+3, 4, "from BANK #");
      putnumber_at(26, HEX_LINE+3, 4, 2, menu_B);
      print_at(13, HEX_LINE+5, 4, "to Backup Memory ?");
   }
   else if (menu_A == 4)
   {
      print_at(14, HEX_LINE+1, 4, "Confirm ");
      print_at(22, HEX_LINE+1, 3, "ERASE");
      print_at(15, HEX_LINE+3, 4, "of BANK #");
      putnumber_at(24, HEX_LINE+3, 4, 2, menu_B);
      print_at(13, HEX_LINE+5, 4, "from Backup Memory ?");
   }

   while (1)
   {
      print_at(16, HEX_LINE+9, ((confirm_value == 1) ? 1 : 0), " YES ");
      print_at(21, HEX_LINE+9, 0, " / ");
      print_at(24, HEX_LINE+9, ((confirm_value == 0) ? 1 : 0), " NO ");

      if (joytrg & JOY_LEFT)
      {
         confirm_value = (confirm_value == 0) ? 1 : 0;
      }

      if (joytrg & JOY_RIGHT)
      {
         confirm_value = (confirm_value == 0) ? 1 : 0;
      }


      if (joytrg & JOY_II)
      {
         confirm = 0;
	 break;
      }

      if ((joytrg & JOY_RUN) || (joytrg & JOY_I))
      {
         confirm = confirm_value;
	 break;
      }

      vsync(0);
   }
}

void erase_menu(void)
{
int menu_item = 1;
int i;
int j;
char sector_num[8];

   clear_panel();
   while (1)
   {
      print_at(12, STAT_LINE + 4, ((menu_item == 1) ? 1 : 0), " ERASE BOOT SECTOR  ");
      print_at(12, STAT_LINE + 6, ((menu_item == 2) ? 1 : 0), " ERASE SINGLE ENTRY ");
      print_at(12, STAT_LINE + 8, ((menu_item == 3) ? 1 : 0), " ERASE ENTIRE CART  ");

      if (joytrg & JOY_UP) {
         menu_item--;
	 if (menu_item == 0)
            menu_item = 3;
      }

      if (joytrg & JOY_DOWN) {
         menu_item++;
	 if (menu_item == 4)
            menu_item = 1;
      }

      if ((joytrg & JOY_RUN) || (joytrg & JOY_I))
      {
         if (menu_item == 1)
	 {
	    flash_erase_sector( (u8 *) FXBMP_BASE);
	    print_at(7, INSTRUCT_LINE+2, 3, "Sector Erased      ");
	 }
	 else if (menu_item == 2)
	 {
            menu_A = 4;
            select_bank_menu();

            if (menu_B == -1)
               continue;
            else
            {
               confirm_menu();
               if (confirm == 1)
	       {
                  for (j = 0; j < 9; j++)
                  {
                     flash_erase_sector( (u8 *) ( calc_bank_addr(menu_B -1) + ((j<<1) * 4096)) );
                  }
	          print_at(7, INSTRUCT_LINE+2, 3, "Entry Erased       ");
	       }
               clear_panel();
            }
	 }
	 else if (menu_item == 3)
	 {
            for (i = 0; i < 128; i++)
            {
               sprintf(sector_num, "%3d", i);
	       print_at(7, INSTRUCT_LINE+2, 3, "Erasing Sector ");
	       print_at(22, INSTRUCT_LINE+2, 3, sector_num);
               flash_erase_sector(  (u8 *) (FXBMP_BASE + ((i<<1) * 4096)) );
	       vsync(0);
            }
	    print_at(7, INSTRUCT_LINE+2, 3, "Cartridge Erased   ");
	 }
      }

      if ((joytrg & JOY_II))
      {
         break;
      }

      vsync(0);
   }
}

void credits(void)
{
//int i;

   clear_panel();

   print_at(5, HEX_LINE-1, 0, "Megavault stores and manages your");
   print_at(5, HEX_LINE  , 0, "PC-FX game save data.");

   print_at(5, HEX_LINE+2, 0, "Using modern Flash memory, you can");
   print_at(5, HEX_LINE+3, 0, "now save and index up to 12 backup");
   print_at(5, HEX_LINE+4, 0, "memory compartments for future use.");

   print_at(5, HEX_LINE+6, 0, "This card is a proof-of-concept");
   print_at(5, HEX_LINE+7, 0, "and future versions may have more");
   print_at(5, HEX_LINE+8, 0, "capabilities.");

   print_at(11, HEX_LINE+13, 0, "(c) 2022 by David Shadoff");

   while (1)
   {
      if ((joypad & 4095) == (JOY_III | JOY_IV | JOY_V | JOY_UP | JOY_SELECT) )
      {
         erase_menu();
         break;
      }

      if ((joytrg & JOY_RUN) || (joytrg & JOY_I))
	 break;

      vsync(0);
   }

//   clear_panel();
//   print_at(5, HEX_LINE+4, 0, "Erasing sectors");
//   for (i = 0; i < 128; i++)
//   {
//      flash_erase_sector( FXBMP_BASE + ((i<<1) * 4096));
//   }

}

void init(void)
{
	int i, j;
//	u32 str[256];
	u16 microprog[16];
	u16 a, img;

	eris_low_sup_init(0);
	eris_low_sup_init(1);
	eris_king_init();
	eris_tetsu_init();
	
	eris_tetsu_set_priorities(0, 0, 1, 0, 0, 0, 0);
	eris_tetsu_set_7up_palette(0, 0);
	eris_tetsu_set_king_palette(0, 0, 0, 0);
	eris_tetsu_set_rainbow_palette(0);

	eris_king_set_bg_prio(KING_BGPRIO_3, KING_BGPRIO_HIDE, KING_BGPRIO_HIDE, KING_BGPRIO_HIDE, 0);
	eris_king_set_bg_mode(KING_BGMODE_4_PAL, 0, 0, 0);
	eris_king_set_kram_pages(0, 0, 0, 0);

	for(i = 0; i < 16; i++) {
		microprog[i] = KING_CODE_NOP;
	}

	microprog[0] = KING_CODE_BG0_CG_0;
	eris_king_disable_microprogram();
	eris_king_write_microprogram(microprog, 0, 16);
	eris_king_enable_microprogram();

	//eris_tetsu_set_palette(3, 0x602C);
	//eris_tetsu_set_palette(4, 0x5080);
	//eris_tetsu_set_palette(5, 0xC422);
	//eris_tetsu_set_palette(6, 0x9999);
	//eris_tetsu_set_palette(7, 0x1234);

	/* Font uses sub-palette #1 for FG, #2 for BG */
	/* palette #0 is default - light green background, bright white foreground */
	eris_tetsu_set_palette(0x00, 0x2A66);
	eris_tetsu_set_palette(0x01, 0xFC88);
	eris_tetsu_set_palette(0x02, 0x2A66);

	/* palette #1 is selection/inverse - bright white background, light green foreground */
	eris_tetsu_set_palette(0x10, 0xFC88);
	eris_tetsu_set_palette(0x11, 0x2A66);
	eris_tetsu_set_palette(0x12, 0xFC88);

	/* palette #2 is disabled/dimmed - light green background, dimmed white foreground */
	eris_tetsu_set_palette(0x20, 0x2A66);
	eris_tetsu_set_palette(0x21, 0x9088);
	eris_tetsu_set_palette(0x22, 0x2A66);

	/* palette #3 is error/red - light green background, bright red foreground */
	eris_tetsu_set_palette(0x30, 0x2A66);
	eris_tetsu_set_palette(0x31, 0x8B3B);  // TODO: get right RED (was 4B5F)
	eris_tetsu_set_palette(0x32, 0x2A66);

	/* palette #4 is highlight/yellow - light green background, bright yellow foreground */
	eris_tetsu_set_palette(0x40, 0x2A66);
	eris_tetsu_set_palette(0x41, 0xDF09);
	eris_tetsu_set_palette(0x42, 0x2A66);

	/* palette #5 is highlight/blue-green - light green background, blue-green foreground */
	eris_tetsu_set_palette(0x50, 0x2A66);
	eris_tetsu_set_palette(0x51, 0x9BB1);
	eris_tetsu_set_palette(0x52, 0x2A66);


	eris_tetsu_set_video_mode(TETSU_LINES_262, 0, TETSU_DOTCLOCK_7MHz, TETSU_COLORS_16,
				TETSU_COLORS_16, 1, 0, 1, 0, 0, 0, 0);
	eris_king_set_bat_cg_addr(KING_BG0, 0, 0);
	eris_king_set_bat_cg_addr(KING_BG0SUB, 0, 0);
	eris_king_set_scroll(KING_BG0, 0, 0);
	eris_king_set_bg_size(KING_BG0, KING_BGSIZE_256, KING_BGSIZE_256, KING_BGSIZE_256, KING_BGSIZE_256);
	eris_low_sup_set_control(0, 0, 1, 0);
	eris_low_sup_set_access_width(0, 0, SUP_LOW_MAP_64X32, 0, 0);
	eris_low_sup_set_scroll(0, 0, 0);
	//eris_low_sup_set_video_mode(0, 2, 2, 4, 0x1F, 0x11, 2, 239, 2); // 5MHz numbers
	eris_low_sup_set_video_mode(0, 3, 3, 6, 0x2B, 0x11, 2, 239, 2);

	eris_king_set_kram_read(0, 1);
	eris_king_set_kram_write(0, 1);
	// Clear BG0's RAM
	for(i = 0; i < 0x1E00; i++) {
		eris_king_kram_write(0);
	}
	eris_king_set_kram_write(0, 1);

	eris_low_sup_set_vram_write(0, 0);
	for(i = 0; i < 0x800; i++) {
		eris_low_sup_vram_write(0, 0x120); // 0x80 is space
	}


	eris_low_sup_set_vram_write(0, 0x1200);
	// load font into video memory
	for(i = 0; i < 0x60; i++) {
		// first 2 planes of color
		for (j = 0; j < 8; j++) {
			img = font[(i*8)+j] & 0xff;
			a = (~img << 8) | img;
			eris_low_sup_vram_write(0, a);
		}
		// last 2 planes of color
		for (j = 0; j < 8; j++) {
			eris_low_sup_vram_write(0, 0);
		}
	}

	eris_pad_init(0); // initialize joypad

//	chartou32("7up BG example", str);
//	printstr(str, 9, 0x10, 1);

//	print_at(6,  6, 0, "7up BG example");
//	print_at(6,  7, 1, "Testing Color 1");
//	print_at(6,  8, 2, "Testing Color 2");
//	print_at(6,  9, 3, "Testing Color 3");
//	print_at(6, 10, 4, "Testing Color 4");
//	print_at(6, 11, 5, "Testing Color 5");

        // Disable all interrupts before changing handlers.
        irq_set_mask(0x7F);

        // Replace firmware IRQ handlers for the Timer and HuC6270-A.
        //
        // This liberis function uses the V810's hardware IRQ numbering,
        // see FXGA_GA and FXGABOAD documents for more info ...
        irq_set_raw_handler(0xC, my_vblank_irq);

        // Enable Timer and HuC6270-A interrupts.
        //
        // d6=Timer
        // d5=External
        // d4=KeyPad
        // d3=HuC6270-A
        // d2=HuC6272
        // d1=HuC6270-B
        // d0=HuC6273
        irq_set_mask(0x77);

        // Allow all IRQs.
        //
        // This liberis function uses the V810's hardware IRQ numbering,
        // see FXGA_GA and FXGABOAD documents for more info ...
        irq_set_level(8);

        // Enable V810 CPU's interrupt handling.
        irq_enable();

        eris_low_sup_setreg(0, 5, 0x88);  // Set Hu6270 BG to show, and VSYNC Interrupt

        eris_bkupmem_set_access(1,1);  // allow read and write access to both internal and external backup memory
}

int main(int argc, char *argv[])
{
char hexdata[8];

   init();

   /* title of page */
   strcpy(today_date, "          ");
   strcpy(card_date, "          ");
   strcpy(default_date, "2023-01-07");
   strcpy(comment, "               ");
   strcpy(today_comment, "               ");

   print_at(16, TITLE_LINE, 4, "FX Megavault");
   print_at(34, TITLE_LINE, 4, "v0.3");

   /* check whether flash identifies itself as a flash chip which */
   /* works with the programming sequences in use in this program */

   flash_id( &chip_id[0] );

   sprintf(hexdata, "%2.2X %2.2X", chip_id[0], chip_id[1]);
   
#ifndef NO_ENFORCE_FLASH
   if ((chip_id[0] != 0xBF) || (chip_id[1] != 0xB7))
   {
      print_at( 8, INSTRUCT_LINE +  4, 0, "THIS IS NOT BEING RUN ON THE");
      print_at( 8, INSTRUCT_LINE +  6, 0, "CORRECT TYPE OF FLASH CHIP.");
      print_at( 8, INSTRUCT_LINE + 10, 0, "PLEASE USE ORIGINAL MEDIA !!!");
      print_at( 8, INSTRUCT_LINE + 12, 0, "MEDIA = ");
      print_at(16, INSTRUCT_LINE + 12, 0, hexdata);
      print_at(15, INSTRUCT_LINE + 15, 0, "*** ABORT *** ");
      while(1);
   }
#endif
   
   menu_level = 1;

   /* determine whether each bank is actually in use, and */
   /* most recent date of save on card */

   while (1)
   {
      check_BRAM_status();

      if (menu_level == 1)
      {
         top_menu();

	 if (menu_A == -1)
	 {
            credits();
            menu_level = 1;
            continue;
	 }
	 else if (menu_A == 2)         /* save - get date, comment */
         {
            /* Get date information */
            get_date();

	    if (date_level == -1)
	    {
               menu_level = 1;
	       continue;
	    }

            /* Get comment information */
            get_comment();

	    if (date_level == -1)
	    {
               menu_level = 1;
	       continue;
	    }
         }
	 menu_level = 2;
      }

      if (menu_level == 2)
      {
         select_bank_menu();

         if (menu_B == -1)
	 {
            menu_level = 1;
            continue;
	 }
	 else
	    menu_level = 3;
      }

      if (menu_level == 3)
      {
         if (menu_A == 1)         /* view */
         {
            if (menu_B == 0)      // examine the internal SRAM
	    {
               copy_to_buffer( bram_mem);
	    }
	    else                  // determine which of the backup slots to copy
	    {
               copy_to_buffer( calc_bank_addr(menu_B -1) );
	    }

            bram_free = check_buffer_free();
            get_buffer_directory();
	    buff_listing();       /* this waits for exit keys */

            clear_buff_listing();
            menu_level = 2;
         }
         else if (menu_A == 2)    /* save */
         {
            /* need to confirm commit */
            confirm_menu();

	    if (confirm == 0)
	    {
               menu_level = 2;
	       continue;
	    }
	    else
	    {
               strncpy(date, today_date, 11);
               strncpy(comment, today_comment, COMMENT_LENGTH + 1);

               copy_to_buffer( bram_mem);
               buffer_to_flash( calc_bank_addr(menu_B -1) );

	       menu_level = 1;
	    }
         }
         else if (menu_A == 3)    /* restore */
         {
            /* need to confirm commit */
            confirm_menu();

	    if (confirm == 0)
	    {
               menu_level = 2;
	       continue;
	    }
	    else
	    {
               copy_to_buffer( calc_bank_addr(menu_B -1) );
	       buffer_to_bram();

	       menu_level = 1;
	    }
         }
      }
   }

   print_at(4, TITLE_LINE, 0, "oops - fatal error");

   while(1);

   return 0;
}

// print with first 7up (HuC6270 #0)
//
void print_at(int x, int y, int pal, char* str)
{
	int i;
	u16 a;

	i = (y * 64) + x;

	eris_low_sup_set_vram_write(0, i);
	for (i = 0; i < strlen8(str); i++) {
		a = (pal * 0x1000) + str[i] + 0x100;
		eris_low_sup_vram_write(0, a);
	}
}

void putch_at(int x, int y, int pal, char c)
{
        int i;
        u16 a;

        i = (y * 64) + x;

        eris_low_sup_set_vram_write(0, i);

        a = (pal * 0x1000) + c + 0x100;
        eris_low_sup_vram_write(0, a);
}

void putnumber_at(int x, int y, int pal, int len, int value)
{
        int i;
        u16 a;
	char str[64];

        i = (y * 64) + x;

	if (len == 2) {
	   sprintf(str, "%2d", value);
	}
	else if (len == 4) {
	   sprintf(str, "%4d", value);
	}
	else if (len == 5) {
	   sprintf(str, "%5d", value);
	}

        eris_low_sup_set_vram_write(0, i);

	for (i = 0; i < strlen8(str); i++) {
                a = (pal * 0x1000) + str[i] + 0x100;
                eris_low_sup_vram_write(0, a);
        }
}

// functions related to printing with KING processor
//

void printsjis(char *text, int x, int y)
{

int offset;
u8 ch, ch2;
u32 sjis;
u32 kram;

   offset = 0;
   kram = x + (y <<5);

   ch = *(text+offset);

   while (ch != 0)
   {
      if ((ch < 0x81) || ((ch >= 0xA1) && (ch <= 0xDF)))
      {
         sjis = ch;
         print_narrow(sjis, kram);
         kram++;
      }
      else
      {
         offset++;
         ch2 = *(text+offset);
         sjis = (ch << 8) + ch2;
         print_wide(sjis, kram);
         kram += 2;
      }

      offset++;
      ch = *(text+offset);
   }

}

void print_narrow(u32 sjis, u32 kram)
{
        u16 px;
        int x, y;
        u8* glyph;

        glyph = eris_romfont_get(sjis, ROMFONT_ANK_8x16);

        for(y = 0; y < 16; y++) {
                eris_king_set_kram_write(kram + (y << 5), 1);
                px = 0;
                for(x = 0; x < 8; x++) {
                        if((glyph[y] >> x) & 1) {
                                px |= 1 << (x << 1);
                        }
                }
                eris_king_kram_write(px);
        }
}

void print_wide(u32 sjis, u32 kram)
{
        u16 px;
        int x, y;
        u16* glyph;

        glyph = (u16*) eris_romfont_get(sjis, ROMFONT_KANJI_16x16);

        for(y = 0; y < 16; y++) {
                eris_king_set_kram_write(kram + (y << 5), 1);
                px = 0;
                for(x = 0; x < 8; x++) {
                        if((glyph[y] >> x) & 1) {
                                px |= 1 << (x << 1);
                        }
                }
                eris_king_kram_write(px);

                eris_king_set_kram_write(kram + (y << 5) + 1, 1);
                px = 0;
                for(x = 0; x < 8; x++) {
                        if((glyph[y] >> (x+8)) & 1) {
                                px |= 1 << (x << 1);
                        }
                }
                eris_king_kram_write(px);
        }
}

