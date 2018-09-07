/*******************************************************************************
 * Copyright (c) 2016-2017 VMware, Inc.  All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 ******************************************************************************/

/*
 * test_rts.c -- tests UEFI RTS relocation and functionality. This test
 *               relocates UEFI RT services after exiting UEFI, and thus
 *               does not ever return back control to UEFI on success.
 *
 *   test_rts [-sS]
 *
 *      OPTIONS
 *         -B <base>      Override RTS base.
 *         -C <caps>      Force specific EFI/RTS caps.
 *         -N             Skip quirks (i.e. new/old mapping).
 *         -S <1...4>     Set the default serial port (1=COM1, 2=COM2, 3=COM3,
 *                        4=COM4, 0xNNNN=hex I/O port address ).
 *         -s <BAUDRATE>  Set the serial port speed to BAUDRATE (in bits per
 *                        second).
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>
#include <bootlib.h>
#include <boot_services.h>

static int serial_com;
static int serial_speed;

#define DEFAULT_SERIAL_COM      1       /* Default serial port (COM1) */
#define DEFAULT_SERIAL_BAUDRATE 115200  /* Default serial baud rate */

#define DEFAULT_PROG_NAME       "test_rts"

static bool no_quirks;
static bool caps_override;
static efi_info_t efi_info;

/*
 * These values match vmkernel layout.
 */
#if defined(only_arm64)
#define DEFAULT_RTS_VADDR 0x0000808000000000UL
#else
#define DEFAULT_RTS_VADDR 0xffff808000000000UL
#endif


/*-- test_rts_init -------------------------------------------------------------
 *
 *      Parse test_rts command line options.
 *
 * Parameters
 *      IN argc: number of command line arguments
 *      IN argv: pointer to the command line arguments array
 *
 * Results
 *      ERR_SUCCESS, or a generic error status.
 *----------------------------------------------------------------------------*/
static int test_rts_init(int argc, char **argv)
{
   int opt;

   if (argc == 0 || argv == NULL || argv[0] == NULL) {
      return ERR_INVALID_PARAMETER;
   }

   serial_com = DEFAULT_SERIAL_COM;
   serial_speed = DEFAULT_SERIAL_BAUDRATE;

   if (argc > 1) {
      optind = 1;
      do {
         opt = getopt(argc, argv, "s:S:C:B:N");
         switch (opt) {
            case -1:
               break;
            case 'N':
               no_quirks = true;
               break;
            case 'B':
               efi_info.rts_vaddr = strtol(optarg, NULL, 0);
               break;
            case 'C':
               caps_override = true;
               efi_info.caps = strtol(optarg, NULL, 0);
               break;
            case 'S':
               serial_com = strtol(optarg, NULL, 0);
               break;
            case 's':
               if (!is_number(optarg)) {
                  return ERR_SYNTAX;
               }
               serial_speed = atoi(optarg);
               break;
            case '?':
            default:
               return ERR_SYNTAX;
         }
      } while (opt != -1);
   }

   return ERR_SUCCESS;
}

/*-- main ----------------------------------------------------------------------
 *
 *      test_rts main function.
 *
 * Parameters
 *      IN argc: number of command line arguments
 *      IN argv: pointer to the command line arguments array
 *
 * Results
 *      ERR_SUCCESS, or a generic error status.
 *----------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
   int status;
   size_t count;
   e820_range_t *e820_mmap;

   status = log_init(true);
   if (status != ERR_SUCCESS) {
      return status;
   }

   efi_info.rts_size = (512UL*1024*1024*1024);
   efi_info.rts_vaddr = DEFAULT_RTS_VADDR;

   status = test_rts_init(argc, argv);
   if (status != ERR_SUCCESS) {
      Log(LOG_ERR, "test_rts_init: %s\n", error_str[status]);
      return status;
   }

   if (!caps_override) {
      efi_info.caps =
         EFI_RTS_CAP_RTS_DO_TEST |
         EFI_RTS_CAP_RTS_SIMPLE  |
         EFI_RTS_CAP_RTS_SIMPLE_GQ |
         EFI_RTS_CAP_RTS_SPARSE  |
         EFI_RTS_CAP_RTS_COMPACT |
         EFI_RTS_CAP_RTS_CONTIG;
   }

   Log(LOG_INFO, "Using efi_info.caps = 0x%lx\n", efi_info.caps);
   if ((efi_info.caps & EFI_RTS_CAP_OLD_AND_NEW) != 0) {
      Log(LOG_INFO, "\tEFI_RTS_CAP_OLD_AND_NEW\n");
   }
   if ((efi_info.caps & EFI_RTS_CAP_RTS_DO_TEST) != 0) {
      Log(LOG_ERR, "\tEFI_RTS_CAP_RTS_DO_TEST\n");
   }
   if ((efi_info.caps & EFI_RTS_CAP_RTS_SIMPLE) != 0) {
      Log(LOG_INFO, "\tEFI_RTS_CAP_RTS_SIMPLE\n");
   }
   if ((efi_info.caps & EFI_RTS_CAP_RTS_SIMPLE_GQ) != 0) {
      Log(LOG_INFO, "\tEFI_RTS_CAP_RTS_SIMPLE_GQ\n");
   }
   if ((efi_info.caps & EFI_RTS_CAP_RTS_SPARSE) != 0) {
      Log(LOG_INFO, "\tEFI_RTS_CAP_RTS_SPARSE\n");
   }
   if ((efi_info.caps & EFI_RTS_CAP_RTS_COMPACT) != 0) {
      Log(LOG_INFO, "\tEFI_RTS_CAP_RTS_COMPACT\n");
   }
   if ((efi_info.caps & EFI_RTS_CAP_RTS_CONTIG) != 0) {
      Log(LOG_INFO, "\tEFI_RTS_CAP_RTS_CONTIG\n");
   }

   status = get_memory_map(0, &e820_mmap, &count, &efi_info);
   if (status != ERR_SUCCESS) {
      Log(LOG_ERR, "get_memory_map failed: %s\n", error_str[status]);
      return status;
   }

   status = serial_log_init(serial_com, serial_speed);
   if (status != ERR_SUCCESS) {
      /*
       * This test is useless without working serial.
       */
      Log(LOG_ERR, "serial_log_init failed: %s\n", error_str[status]);
      return status;
   }

   Log(LOG_INFO, "The rest of this test logs only via the serial port!\n");

   /*
    * No need to log everything twice.
    */
   log_unsubscribe(firmware_print);

   if (!no_quirks) {
      check_efi_quirks(&efi_info);
   }

   Log(LOG_INFO, "\nTrying to exit boot services\n");
   status = exit_boot_services(0, &e820_mmap, &count, &efi_info);
   if (status != ERR_SUCCESS) {
      Log(LOG_ERR, "exit_boot_services failed: %s\n", error_str[status]);
      free_memory_map(e820_mmap, &efi_info);
      return status;
   }

   e820_mmap_merge(e820_mmap, &count);
   status = e820_to_blacklist(e820_mmap, count);
   if (status != ERR_SUCCESS) {
      Log(LOG_ERR, "scan_memory_map: %s\n", error_str[status]);
      goto out;
   }

   Log(LOG_INFO, "\nRelocating runtime services%s\n", no_quirks ? " (no quirks)" : "");
   status = relocate_runtime_services(&efi_info, false, no_quirks);
   if (status != ERR_SUCCESS) {
      Log(LOG_EMERG, "relocate_runtime_services failed: %s\n", error_str[status]);
   } else {
      Log(LOG_ERR, "\nAll done! It's now safe to turn off your computer\n");
   }

out:
   while (1);
}