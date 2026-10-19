/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2024 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*                                                                    *
*       SEGGER SystemView * Real-time application analysis           *
*                                                                    *
**********************************************************************
*                                                                    *
* All rights reserved.                                               *
*                                                                    *
* SEGGER strongly recommends to not make any changes                 *
* to or modify the source code of this software in order to stay     *
* compatible with the SystemView and RTT protocol, and J-Link.       *
*                                                                    *
* Redistribution and use in source and binary forms, with or         *
* without modification, are permitted provided that the following    *
* condition is met:                                                  *
*                                                                    *
* o Redistributions of source code must retain the above copyright   *
*   notice, this condition and the following disclaimer.             *
*                                                                    *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
* DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR           *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  *
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;    *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      *
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT          *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE  *
* USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH   *
* DAMAGE.                                                            *
*                                                                    *
**********************************************************************
-------------------------- END-OF-HEADER -----------------------------

File    : SEGGER_SYSVIEW_Conf.h
Purpose : SEGGER SystemView configuration file.
          Set defines which deviate from the defaults (see SEGGER_SYSVIEW_ConfDefaults.h) here.          
Revision: $Rev: 21292 $

Additional information:
  Required defines which must be set are:
    SEGGER_SYSVIEW_GET_TIMESTAMP
    SEGGER_SYSVIEW_GET_INTERRUPT_ID
  For known compilers and cores, these might be set to good defaults
  in SEGGER_SYSVIEW_ConfDefaults.h.
  
  SystemView needs a (nestable) locking mechanism.
  If not defined, the RTT locking mechanism is used,
  which then needs to be properly configured.
*/

#ifndef SEGGER_SYSVIEW_CONF_H
#define SEGGER_SYSVIEW_CONF_H

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

/*********************************************************************
*
*       Define: SEGGER_SYSVIEW_SECTION
*
*  Description
*    Section to place the SystemView RTT Buffer into.
*  Default
*    undefined: Do not place into a specific section.
*  Notes
*    If SEGGER_RTT_SECTION is defined, the default changes to use
*    this section for the SystemView RTT Buffer, too.
*/
#if !(defined SEGGER_SYSVIEW_SECTION) && (defined SEGGER_RTT_BUFFER_SECTION)
  #define SEGGER_SYSVIEW_SECTION                  SEGGER_RTT_BUFFER_SECTION
#endif


/*********************************************************************
* Project-specific SystemView configuration                          *
**********************************************************************
*/

// Use dedicated RTT channel 1 for SystemView (channel 0 = RTT terminal).
// Default (0) causes SystemView to share channel 0 and skip buffer setup.
#define SEGGER_SYSVIEW_RTT_CHANNEL      1

// The SystemView up buffer (_UpBuffer, RTT channel 1): SEGGER's default, 1 KB (was 4 KB,
// RAM reclaim 2026-10-19). It only matters while a J-Link SystemView host is recording:
// with no viewer it fills and NO_BLOCK_SKIP drops whole packets (a write never splits or
// blocks, whatever the size), so a bigger buffer only buys a longer burst before the host
// drains it. Nothing else writes channel 1: the RTT host transport (Board::RttChannel(0)),
// the interface trace (interface_trace.cpp) and the terminal use channel 0
// (_acUpBuffer, BUFFER_SIZE_UP = 1 KB, unchanged). The start-of-recording burst (system
// description, task list, ~6 tasks) is a few hundred bytes. If a recording shows overflow
// events, raise this back.
#define SEGGER_SYSVIEW_RTT_BUFFER_SIZE  1024


#endif  // SEGGER_SYSVIEW_CONF_H

/*************************** End of file ****************************/
