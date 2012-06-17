/*
 * Main.cpp
 *
 *  Created on: 21/4/2012
 *      Author: podonoghue
 *
 *  Simple program demonstrating use of USBDM API
 *
 *  This program dumps the contents of a chip to stdout in S19 format.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __unix__
#include <memory.h> // linux
#else
#include <mem.h> // win
#endif
#include "USBDM_API.h"

// Required API
#define USBDM_API_VERSION_REQUIRED (40905) // Need V4.9.5

// Static check that correct API (header file) is being used
#if (USBDM_API_VERSION < USBDM_API_VERSION_REQUIRED)
#error "This program requires USBDM version 4.9.5 or later"
#endif

// Maximum size of a S-record, should be power of 2
const int maxSrecSize = (1<<4);

// Buffer for flash image
uint8_t buffer[0x20000];

// A region of flash to dump
//
typedef struct {
   uint32_t start;  //< start address (inclusive)
   uint32_t end;    //< end address (inclusive)
} MemRange;

// Describes target & region to dump
//
struct DeviceDescription {
   const char  *name;        //< Name of device
   TargetType_t targetType;  //< Target e.g. T_HCS12
   unsigned     numRanges;   //< Number of entries in ranges
   MemRange     ranges[10];  //< Ranges to dump
};
// Describes MC9S08JM60
DeviceDescription deviceDescriptionJM60 = {
      "MC9S08JM60",
      T_HCS08,
      2,
      {{0x10B0, 0x17FF},
       {0x1960, 0xFFFF},}
};
// Describes MC9S12NE64
DeviceDescription deviceDescriptionNE64 = {
      "MC9S12NE64",
      T_HCS12,
      2,
      {{0x4000, 0x7FFF},
       {0xC000, 0xFFFF},}
};
// Describes MCF51CN128
DeviceDescription deviceDescriptionCN128 = {
      "MCF51CN128",
      T_CFV1,
      1,
      {{0x000000, 0x01FFFF},}
};

// Check error code from USBDM API function
//
// @param rc - error code to access
//
// An error message is printed with line # and the program exited if
// rc indicates any error
//
void check(USBDM_ErrorCode rc, const char *file = NULL, unsigned lineNum = 0) {
   if (rc == BDM_RC_OK) {
      fprintf(stderr, "OK,     [%s:#%4d]\n", file, lineNum);
      return;
   }
   fprintf(stderr, "Failed, [%s:#%4d] Reason= %s\n", file, lineNum,  USBDM_GetErrorString(rc));
   USBDM_Close();
   USBDM_Exit();
   exit(rc);
}

// Convenience macro to add line number information to check()
//
#define CHECK(x) check((x), __FILE__, __LINE__)

// Dump a single S-record to stdout
//
// @param buffer   location of data to dump
// @param address  base address of data
// @param size     number of bytes to dump
//
// @note size must be less than or equal to \ref maxSrecSize
// @note S-records filled with 0xFF are discarded
//
void dumpSrec(uint8_t *buffer, uint32_t address, uint8_t size) {

   // Discard 0xFF filled records (blank Flash)
   bool allFF = true;
   for(int sub=0; sub<size; sub++) {
      if (buffer[sub] != 0xFF ) {
         allFF = false;
         break;
      }
   }
   if ((size == 0) || allFF) {
      return;
   }
   uint8_t *ptr = buffer;
   uint8_t checkSum;
   if ((address) < 0x10000U) {
      printf("S1%02X%04X", size+3, address);
      checkSum = size+3;
      checkSum += (address>>8)&0xFF;
      checkSum += (address)&0xFF;
   }
   else if (address < 0x1000000U) {
      printf("S2%02X%06X", size+4, address);
      checkSum = size+4;
      checkSum += (address>>16)&0xFF;
      checkSum += (address>>8)&0xFF;
      checkSum += (address)&0xFF;
   }
   else {
      printf("S3%02X%08X", size+5, address);
      checkSum = size+5;
      checkSum += (address>>24)&0xFF;
      checkSum += (address>>16)&0xFF;
      checkSum += (address>>8)&0xFF;
      checkSum += (address)&0xFF;
   }
   while (size-->0) {
      checkSum += *ptr;
      printf("%02X", *ptr++);
   }
   checkSum = checkSum^0xFF;
   printf("%02X\n", checkSum);
}

// Dump data as S-records to stdout
//
// @param buffer   location of data to dump
// @param address  base address of data
// @param size     number of bytes to dump
//
// @note Regions filled with 0xFF are discarded
//
void dump(uint8_t *buffer, uint32_t address, unsigned size) {
   while (size>0) {
      uint8_t oddBytes = address & (maxSrecSize-1);
      uint8_t srecSize = maxSrecSize - oddBytes;
      if (srecSize > size) {
         srecSize = (uint8_t) size;
      }
      dumpSrec(buffer, address, srecSize);
      address += srecSize;
      buffer  += srecSize;
      size    -= srecSize;
   }
}

// Dynamic check for USBDM DLL version
//
// @return BDM_RC_OK if successful
//
USBDM_ErrorCode checkAPI() {
   if (USBDM_DLLVersion() < USBDM_API_VERSION_REQUIRED) {
      return BDM_RC_WRONG_DLL_REVISION;
   }
   return BDM_RC_OK;
}

// Dump the contents of a chip
//
void dumpChip(DeviceDescription &deviceDescription) {
   fprintf(stderr, "Dumping %s\n", deviceDescription.name);

   CHECK(USBDM_Init());
   unsigned deviceCount;
   CHECK(USBDM_FindDevices(&deviceCount));
   CHECK(USBDM_Open(0));

   // Example of changing options, requires version >= V4.9.4
   USBDM_ExtendedOptions_t options = {
         sizeof(USBDM_ExtendedOptions_t),
         deviceDescription.targetType};
   CHECK(USBDM_GetDefaultExtendedOptions(&options));
   options.resetDuration         = 1000;
   options.resetReleaseInterval  = 500;
   options.resetRecoveryInterval = 1500;
   options.targetVdd             = BDM_TARGET_VDD_3V3;
   options.autoReconnect         = AUTOCONNECT_STATUS;
   CHECK(USBDM_SetExtendedOptions(&options));

   CHECK(USBDM_SetTargetType(deviceDescription.targetType));
   CHECK(USBDM_TargetReset((TargetMode_t)(RESET_SPECIAL|RESET_DEFAULT))); // >= V4.9.4
   CHECK(USBDM_Connect());
   memset(buffer, 0xFF, sizeof(buffer));
   for (unsigned sub=0;
        sub<deviceDescription.numRanges;
        sub++) {
      fprintf(stderr, "Range [0x%06X-0x%06X]\n",
            deviceDescription.ranges[sub].start, deviceDescription.ranges[sub].end);
      CHECK(USBDM_ReadMemory(1,
            deviceDescription.ranges[sub].end-deviceDescription.ranges[sub].start+1,
            deviceDescription.ranges[sub].start,
            buffer+(deviceDescription.ranges[sub].start&0xFFFF)));
   }
   USBDM_Close();
   USBDM_Exit();
   dump(buffer, 0x0000, sizeof(buffer));
}

int main(int argc, char *argv[]) {

   CHECK(checkAPI());

   int option = 0;
   if (argc > 1) {
      option = atoi(argv[1]);
   }
   switch(option) {
   default:
   case 0: dumpChip(deviceDescriptionNE64);  break;
   case 1: dumpChip(deviceDescriptionJM60);  break;
   case 2: dumpChip(deviceDescriptionCN128); break;
   }
   return 0;
}
