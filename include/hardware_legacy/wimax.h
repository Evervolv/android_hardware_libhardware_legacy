/*
 * Copyright 2011, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(_WIN32) || defined(_WIN64)
#ifdef WIMAX_EXPORTS
#define WIMAX_API __declspec(dllexport)
#else
#define WIMAX_API __declspec(dllimport)
#endif
#else
#ifdef _DYLIB_
#define WIMAX_API __attribute__((visibility("default")))
#else
#define WIMAX_API
#endif
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Load the Wimax driver.
 *
 * @return 0 on success, < 0 on failure.
 */
int loadWimaxDriver();

/**
 * Unload the Wimax driver.
 *
 * @return 0 on success, < 0 on failure.
 */
int unloadWimaxDriver();

/**
 * Start supplicant (sequansd).
 *
 * @return 0 on success, < 0 on failure.
 */
int startWimaxDaemon();

/**
 * Stop supplicant (sequansd).
 *
 * @return 0 on success, < 0 on failure.
 */
int stopWimaxDaemon();

/**
 * Terminate Process.
 *
 * Takes pid of process as an argument and kills the process.
 *
 * @return 0 on success, < 0 on failure.
 */
int terminateProcess(char *pid);

/**
 * Get Wimax Properties from flash.
 *
 * Takes two arguments.
 * @arg1 would be the desired property to retreive.
 * @arg2 stores result from socket in a pointer to be read and used by JNI
 *
 */
int getWimaxProp(const char *prop, char *result_buffer);

/**
 * Set Wimax Properties into flash.
 *
 * Takes two arguments.
 * @arg1 would be the desired property to set.
 * @arg2 value to append to property
 *
 * @return 0 on success, < 0 on failure.
 */
int setWimaxProp(char* prop, char* val);

/**
 * Stop Wimax DHCP daemon.
 *
 * @return 0 on success, < 0 on failure.
 */
int stopDhcpWimaxDaemon();

/**
 * Start Wimax DHCP daemon.
 *
 * @return 0 on success, < 0 on failure.
 */
int startDhcpWimaxDaemon();

/**
 * Run Wimax DHCP release daemon.
 *
 * @return 0 on success, < 0 on failure.
 */
int doWimaxDhcpRelease();

/**
 * Run Wimax DHCP request daemon.
 *
 * @return 0 on success, < 0 on failure.
 */
int doWimaxDhcpRequest();

/**
 * Run Wimax DHCP renew daemon.
 *
 * @return 0 on success, < 0 on failure.
 */
int wimaxDhcpRenew();

/**
 * Run Wimax routing commands.
 *
 * @return 0 on success, < 0 on failure.
 */
int addRouteToGateway();

/**
 * Send commands to wimaxDaemon.
 *
 * @return 0 on success, < 0 on failure.
 */
int thpIoctl(int cmd, long int arg);

/**
 * Run daemon too configure Wimax interface.
 *
 * @return 0 on success, < 0 on failure.
 */
int wimaxConfigInterface();

/**
 * Run Wimax debugging dump daemons.
 *
 * Daemons that dump Wimax tagged logs to /data/wimax/logs
 * @return 0 on success, < 0 on failure.
 */
int wimaxDumpKmsg();
int wimaxDumpLastKmsg();
int wimaxDumpLogcat();

/**
 * Test connection somehow.
 *
 * This method is stubbed. But should be done at the JNI level not in hardeware.
 * @return 0 on success, < 0 on failure.
 */
int testConnect();

/**
 * Determine if Wimax flash device is MTD or EMMC.
 *
 * WARNING: currently hardcoded for EMMC.
 *
 * @return 0 on success, < 0 on failure.
 */
int getBlockSize();

/**
 * Get time and date.
 *
 * Wimax daemon (sequansd) needs to get time and date and uses this method to do so.
 * Method currently stubbed but disassembled method commented inside method.
 *
 * @return char of time and date on success.
 */
char* getDateAndTime();

/**
 * Connect to and send commands to WimaxMonitor socket.
 *
 * Takes one argument of the command to send.
 *
 * @return 0 on success, < 0 on failure.
 */
int getDataFromWimaxStateTracker(const char *cmd);

/**
 * Connect to and send commands to wimaxDaemon socket.
 *
 * Takes one argument of the command to send.
 *
 * @return char of response on success
 */
char* getDataFromWimaxDaemon(const char *cmd);

#ifdef __cplusplus
}
#endif
