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

#define FXBMP_BASE       0xE8000000      // memory location of start of external backup memory


extern void flash_erase_sector( u8 * sector);
extern void flash_write( u8 * addr, u8 value);
extern void flash_id( u8 * addr );

void print_at(int x, int y, int pal, char* str);
void putch_at(int x, int y, int pal, char c);
void putnumber_at(int x, int y, int pal, int digits, int value);

extern u8 font[];
extern u8 fxbmp_mem[];
extern u8 program_buffer[];

// interrupt-handling variables
volatile int sda_frame_count = 0;
volatile int last_sda_frame_count = 0;

/* HuC6270-A's status register (RAM mapping). Used during VSYNC interrupt */
volatile uint16_t * const MEM_6270A_SR = (uint16_t *) 0x80000400;


// Flash memory identifcation and usage:
u8   chip_id[4];        // first two bytes are buffer for returning flash chip identity

const char letter_display[] =
   {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890.,-[_]'!#%      "};
                              /* "<BCK> <SP> <END>" */

int target_addr = 0xe8000000;   // fxbmp_mem[]
int source_addr = 0x00100000;   // program_buffer[]
int write_len   = 0x00080000;   // 512KB

//char countdown;
int advance;
int menu_A;
int menu_B;
int confirm;
int menu_level;

int stepval = 0;


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

//      flash_write( (target + (i<<1)), bram_buffer[i]);
   }
}

void clear_panel(void)
{
   int i;

   for (i = INSTRUCT_LINE; i < HEX_LINE + 17; i++)
   {
      print_at(2, i, 0, "                                         ");
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
char tempbuf[16];
//int i;

   vsync(2);

   clear_panel();

   menu_selection = 4;

   sprintf(tempbuf, "%8.8X", target_addr);
   print_at( 8, STAT_LINE + 4, ((menu_selection == 1) ? 1 : 0), " Target Addr   0x");
   print_at(25, STAT_LINE + 4, ((menu_selection == 1) ? 1 : 0), tempbuf);

   sprintf(tempbuf, "%8.8X", source_addr);
   print_at( 8, STAT_LINE + 6, ((menu_selection == 2) ? 1 : 0), " Source Addr   0x");
   print_at(25, STAT_LINE + 6, ((menu_selection == 2) ? 1 : 0), tempbuf);

   sprintf(tempbuf, "%8.8X", write_len);
   print_at( 8, STAT_LINE + 8, ((menu_selection == 3) ? 1 : 0), " Write Length  0x");
   print_at(25, STAT_LINE + 8, ((menu_selection == 3) ? 1 : 0), tempbuf);

   while(1)
   {
      // Print frame count (temporary)
      // 
//      sprintf(tempbuf, "%6d", sda_frame_count);
//      print_at(23, HEX_LINE+14, 0, tempbuf);

      advance = 1;


      if (menu_selection == 1)
        clear_errors();


      print_at(11, STAT_LINE + 14, ((menu_selection == 4) ? 1 : 0), " ERASE ");
      print_at(24, STAT_LINE + 14, ((menu_selection == 5) ? 1 : 0), " WRITE ");

      if (joytrg & JOY_LEFT) {
         menu_selection--;
	 if (menu_selection == 3)
            menu_selection = 5;
      }

      if (joytrg & JOY_SELECT) {
         menu_A = -1;
	 break;
      }

      if (joytrg & JOY_RIGHT) {
         menu_selection++;
	 if (menu_selection == 6)
            menu_selection = 5;
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

//void confirm_menu(void)
//{
//static int confirm_value;
//
//   confirm_value = 0;
//
//   vsync(2);
//
//   clear_panel();
//
//   if (menu_A == 2)
//   {
//      print_at(16, HEX_LINE+1, 4, "Confirm ");
//      print_at(24, HEX_LINE+1, 3, "SAVE");
//      print_at(13, HEX_LINE+3, 4, "from Backup Memory");
//      print_at(16, HEX_LINE+5, 4, "to BANK #");
//      putnumber_at(25, HEX_LINE+5, 4, 2, menu_B);
//      print_at(27, HEX_LINE+5, 4, " ? ");
//   }
//   else if (menu_A == 3)
//   {
//      print_at(14, HEX_LINE+1, 4, "Confirm ");
//      print_at(22, HEX_LINE+1, 3, "RESTORE");
//      print_at(15, HEX_LINE+3, 4, "from BANK #");
//      putnumber_at(26, HEX_LINE+3, 4, 2, menu_B);
//      print_at(13, HEX_LINE+5, 4, "to Backup Memory ?");
//   }
//   else if (menu_A == 4)
//   {
//      print_at(14, HEX_LINE+1, 4, "Confirm ");
//      print_at(22, HEX_LINE+1, 3, "ERASE");
//      print_at(15, HEX_LINE+3, 4, "of BANK #");
//      putnumber_at(24, HEX_LINE+3, 4, 2, menu_B);
//      print_at(13, HEX_LINE+5, 4, "from Backup Memory ?");
//   }
//
//   while (1)
//   {
//      print_at(16, HEX_LINE+9, ((confirm_value == 1) ? 1 : 0), " YES ");
//      print_at(21, HEX_LINE+9, 0, " / ");
//      print_at(24, HEX_LINE+9, ((confirm_value == 0) ? 1 : 0), " NO ");
//
//      if (joytrg & JOY_LEFT)
//      {
//         confirm_value = (confirm_value == 0) ? 1 : 0;
//      }
//
//      if (joytrg & JOY_RIGHT)
//      {
//         confirm_value = (confirm_value == 0) ? 1 : 0;
//      }
//
//
//      if (joytrg & JOY_II)
//      {
//         confirm = 0;
//	 break;
//      }
//
//      if ((joytrg & JOY_RUN) || (joytrg & JOY_I))
//      {
//         confirm = confirm_value;
//	 break;
//      }
//
//      vsync(0);
//   }
//}


//      if ((joytrg & JOY_RUN) || (joytrg & JOY_I))
//      {
//         if (menu_item == 1)
//	 {
//	    flash_erase_sector( (u8 *) FXBMP_BASE);
//	    print_at(7, INSTRUCT_LINE+2, 3, "Sector Erased      ");
//	 }
//	 
//	 else if (menu_item == 3)
//	 {
//            for (i = 0; i < 128; i++)
//            {
//               sprintf(sector_num, "%3d", i);
//	       print_at(7, INSTRUCT_LINE+2, 3, "Erasing Sector ");
//	       print_at(22, INSTRUCT_LINE+2, 3, sector_num);
//               flash_erase_sector(  (u8 *) (FXBMP_BASE + ((i<<1) * 4096)) );
//	       vsync(0);
//            }
//	    print_at(7, INSTRUCT_LINE+2, 3, "Cartridge Erased   ");
//	 }

void credits(void)
{
//int i;

   clear_panel();

   print_at(5, HEX_LINE-1, 0, "Programmer is built to test, program,");
   print_at(5, HEX_LINE  , 0, "and erase FX-Flash cartridges in-situ");
   print_at(5, HEX_LINE+1, 0, "on a PC-FX in conjunction with an");
   print_at(5, HEX_LINE+2, 0, "fx_uploader device");

   print_at(5, HEX_LINE+6, 0, "This program is a proof-of-concept");
   print_at(5, HEX_LINE+7, 0, "and future versions may have more");
   print_at(5, HEX_LINE+8, 0, "capabilities.");

   print_at(11, HEX_LINE+13, 0, "(c) 2023 by David Shadoff");

   while (1)
   {
      if ((joytrg & JOY_RUN) || (joytrg & JOY_I))
	 break;

      vsync(0);
   }
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
char numeric[8];
char hexdata[8];
int lower_limit;
int num_sectors;
int i;

   init();

   /* title of page */
   print_at(15, TITLE_LINE, 4, "FX Programmer");
   print_at(34, TITLE_LINE, 4, "v0.1");

   /* check whether flash identifies itself as a flash chip which */
   /* works with the programming sequences in use in this program */

   flash_id( &chip_id[0] );

   sprintf(hexdata, "%2.2X %2.2X", chip_id[0], chip_id[1]);
   
#ifndef NO_ENFORCE_FLASH
//   if ((chip_id[0] != 0xBF) || (chip_id[1] != 0xB7))
//   {
//      print_at( 8, INSTRUCT_LINE +  4, 0, "THIS IS NOT BEING RUN ON THE");
//      print_at( 8, INSTRUCT_LINE +  6, 0, "CORRECT TYPE OF FLASH CHIP.");
//      print_at( 8, INSTRUCT_LINE + 10, 0, "PLEASE USE ORIGINAL MEDIA !!!");
//      print_at( 8, INSTRUCT_LINE + 12, 0, "MEDIA = ");
//      print_at(16, INSTRUCT_LINE + 12, 0, hexdata);
//      print_at(15, INSTRUCT_LINE + 15, 0, "*** ABORT *** ");
//      while(1);
//   }
#endif
   
   menu_level = 1;

   /* determine whether each bank is actually in use, and */
   /* most recent date of save on card */

   while (1)
   {
      if (menu_level == 1)
      {
         top_menu();

	 if (menu_A == -1)
	 {
            credits();
            menu_level = 1;
            continue;
	 }

         lower_limit = 0;                  // for now, always start at sector 0

         num_sectors = (write_len >> 12);  // divide by 4096

         if ((write_len & 0xFFF) != 0)     // any leftover, must erase an additional sector
            num_sectors++;
		

	 if (menu_A == 4)         // Erase Range
         {
            /* Erase range */
            for (i = lower_limit; i < (lower_limit + num_sectors); i++)
            {
               sprintf(numeric, "%3d", i);
               print_at(7, INSTRUCT_LINE+2, 3, "Erasing Sector ");
               print_at(22, INSTRUCT_LINE+2, 3, numeric);

               flash_erase_sector(  (u8 *) (FXBMP_BASE + ((i<<1) * 4096)) );
            }
         }
	 else if (menu_A == 5)    // Program Data
         {
            /* Erase range */
//            for (i = lower_limit; i < (lower_limit + num_sectors); i++)
//            {
//               sprintf(numeric, "%3d", i);
//               print_at(7, INSTRUCT_LINE+2, 3, "Erasing Sector ");
//               print_at(22, INSTRUCT_LINE+2, 3, numeric);
//
//               flash_erase_sector(  (u8 *) (FXBMP_BASE + ((i<<1) * 4096)) );
//            }
            /* Program Data */
            for (i = 0; i < write_len; i++)
            {
               if ((i & 31) == 0)
               {
                  sprintf(numeric, "%6d", i);
                  print_at(7, INSTRUCT_LINE+2, 3, "Writing Byte ");
                  print_at(20, INSTRUCT_LINE+2, 3, numeric);
               }

//               flash_write( (u8 *)(target_addr + (i<<1)), *((u8 *)(source_addr + i)) );
               flash_write( (u8 *)(target_addr + (i<<1)), program_buffer[i] );
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

