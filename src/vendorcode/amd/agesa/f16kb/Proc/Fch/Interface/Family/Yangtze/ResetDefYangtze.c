/* $NoKeywords:$ */
/**
 * @file
 *
 * Graphics Controller family specific service procedure
 *
 *
 *
 * @xrefitem bom "File Content Label" "Release Content"
 * @e project:     AGESA
 * @e sub-project: FCH
 * @e \$Revision: 85510 $   @e \$Date: 2013-01-08 16:26:41 -0600 (Tue, 08 Jan 2013) $
 *
 */
/*
*****************************************************************************
*
 * Copyright (c) 2008 - 2013, Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ADVANCED MICRO DEVICES, INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* ***************************************************************************
*
*/


/*----------------------------------------------------------------------------------------
 *                             M O D U L E S    U S E D
 *----------------------------------------------------------------------------------------
 */
#include  "FchPlatform.h"
#include  "Filecode.h"
/*----------------------------------------------------------------------------------------
 *                   D E F I N I T I O N S    A N D    M A C R O S
 *----------------------------------------------------------------------------------------
 */



/*----------------------------------------------------------------------------------------
 * Default FCH interface settings at InitReset phase.
 *----------------------------------------------------------------------------------------
 */
CONST FCH_RESET_INTERFACE ROMDATA FchResetInterfaceDefault = {
  TRUE,                // UmiGen2
  TRUE,                // SataEnable
  TRUE,                // IdeEnable
  TRUE,                // GppEnable
  TRUE,                // Xhci0Enable
  TRUE                 // Xhci1Enable
};


/*----------------------------------------------------------------
 *  InitReset Phase Data Block Default (Failsafe)
 *----------------------------------------------------------------
 */
FCH_RESET_DATA_BLOCK   InitResetCfgDefault = {
  NULL,                  // StdHeader
  { TRUE,
    TRUE,
    FALSE,
    FALSE,
    TRUE,
    TRUE
    },                   // FchReset

  1,                     // FastSpeed
  3,                     // WriteSpeed
  5,                     // Mode
  0,                     // AutoMode
  0,                     // BurstWrite
  FALSE,                 // SataIdeCombMdPriSecOpt
  0,                     // Cg2Pll
  FALSE,                 // EcKbd
  FALSE,                 // LegacyFree
  FALSE,                 // SataSetMaxGen2
  1,                     // SataClkMode
  0,                     // SataModeReg
  FALSE,                 // SataInternal100Spread
  2,                     // SpiSpeed
//  0xFCFCFCFC,                     // 38
//  0x88FC,                     // 3c
//  0,                     // 1d_34
  1,                     // 20_0
  FALSE,                 // EcChannel0

  {                      // FCH_GPP
    {                    // Array of FCH_GPP_PORT_CONFIG       PortCfg[4]
      {
        FALSE,           // PortPresent
        FALSE,           // PortDetected
        FALSE,           // PortIsGen2
        FALSE,           // PortHotPlug
        0,               // PortMisc
      },
      {
        FALSE,           // PortPresent
        FALSE,           // PortDetected
        FALSE,           // PortIsGen2
        FALSE,           // PortHotPlug
        0,               // PortMisc
      },
      {
        FALSE,           // PortPresent
        FALSE,           // PortDetected
        FALSE,           // PortIsGen2
        FALSE,           // PortHotPlug
        0,               // PortMisc
      },
      {
        FALSE,           // PortPresent
        FALSE,           // PortDetected
        FALSE,           // PortIsGen2
        FALSE,           // PortHotPlug
        0,               // PortMisc
      },
    },
    PortA1B1C1D1,        // GppLinkConfig
    FALSE,               // GppFunctionEnable
    FALSE,               // GppToggleReset
    0,                   // GppHotPlugGeventNum
    0,                   // GppFoundGfxDev
    FALSE,               // GppGen2
    0,                   // GppGen2Strap
    FALSE,               // GppMemWrImprove
    FALSE,               // GppUnhidePorts
    0,                   // GppPortAspm
    FALSE,               // GppLaneReversal
    FALSE,               // GppPhyPllPowerDown
    FALSE,               // GppDynamicPowerSaving
    FALSE,               // PcieAer
    FALSE,               // PcieRas
    FALSE,               // PcieCompliance
    FALSE,               // PcieSoftwareDownGrade
    FALSE,               // UmiPhyPllPowerDown
    FALSE,               // SerialDebugBusEnable
    0,                   // GppHardwareDownGrade
    0,                   // GppL1ImmediateAck
    FALSE,               // NewGppAlgorithm
    0,                   // HotPlugPortsStatus
    0,                   // FailPortsStatus
    40,                  // GppPortMinPollingTime
    FALSE,               // IsCapsuleMode
  },
  {                      // FCH_SPI
    FALSE,               // LpcMsiEnable
    0x00000000,          // LpcSsid
    0,                   // RomBaseAddress
    0,                   // Speed
    0,                   // FastSpeed
    0,                   // WriteSpeed
    0,                   // Mode
    0,                   // AutoMode
    0,                   // BurstWrite
    TRUE,                // LpcClk0
    TRUE,                // LpcClk1
    0,                   // SPI100_Enable
    {0}                  // SpiDeviceProfile
  },
  FALSE,                 // QeEnabled
  NULL                   // OemResetProgrammingTablePtr
};


