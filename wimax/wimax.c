/*
 * Copyright 2011-2012, The CyanogenMod Project
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
 *
 * Maintained by Richard Ross <toastcfh@gmail.com>
 */

#define LOG_TAG "WiMaxHW"

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "hardware_legacy/wimax.h"
#include "libwpa_client/wpa_ctrl.h"

#include "cutils/log.h"
#include "cutils/memory.h"
#include "cutils/misc.h"
#include "cutils/properties.h"
#include "private/android_filesystem_config.h"
#ifdef HAVE_LIBC_SYSTEM_PROPERTIES
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#endif

static struct wpa_ctrl *ctrl_conn;
static struct wpa_ctrl *monitor_conn;

extern int do_dhcp();
extern int ifc_init();
extern void ifc_close();
extern int ifc_up(const char *name);
extern char *dhcp_lasterror();
extern void get_dhcp_info();

static char iface[PROPERTY_VALUE_MAX];

extern int init_module(void *, unsigned long, const char *);
extern int delete_module(const char *, unsigned int);

// static vars
// driver
static const char DRIVER_MODULE_PATH[]	= "/system/lib/modules/sequans_sdio.ko";
static const char DRIVER_MODULE_NAME[]	= "sequans_sdio";
static const char DRIVER_MODULE_TAG[]   = "sequans_sdio";
static const char DRIVER_MODULE_ARG[]   = "";
static const char MODULE_FILE[]         = "/proc/modules";
// wimax properties
static const char GET_PROP_NAME[]       = "getWMXPropd";
static const char SET_PROP_NAME[]       = "setWMXPropd";
// wimax daemon
static const char DRIVER_PROP_NAME[]    = "wimax.sequansd.pid";
static const char SERVICE_NAME[]	= "sequansd";
// routing
static const char WIMAX_ADD_ROUTE[]	= "wimaxAddRoute";
// dhcp
static const char WIMAX_DHCP_RELEASE[]     = "wimaxDhcpRelease";
static const char WIMAX_DHCP_NAME[]        = "dhcpWimax";
static const char DHCP_WIMAX_PROP_NAME[]   = "init.svc.dhcpWimax";
static const char WIMAX_CFG_IFACE[]	   = "wmxCfgItf";
// debugging
static const char WIMAX_KMSG[]		= "wimaxDumpKmsg";
static const char WIMAX_LAST_KMSG[]	= "wimaxDumpLastKmsg";
static const char WIMAX_LOGCAT[]	= "wimaxDumpLogcat";

// load kernel module
static int insmod(const char *filename, const char *args)
{
    void *module;
    unsigned int size;
    int ret;

    module = load_file(filename, &size);
    if (!module)
        return -1;

    ret = init_module(module, size, args);

    free(module);

    return ret;
}

// remove kernel module
static int rmmod(const char *modname)
{
    int ret = -1;
    int maxtry = 10;

    while (maxtry-- > 0) {
        ret = delete_module(modname, O_NONBLOCK | O_EXCL | O_TRUNC);
        if (ret < 0 && errno == EAGAIN)
            usleep(500000);
        else
            break;
    }

    if (ret != 0)
        ALOGD("wimax: rmmod: unable to unload driver module \"%s\": %s\n", modname, strerror(errno));
    return ret;
}

// check if wimax module is loaded
static int check_driver_loaded()
{
    char driver_status[PROPERTY_VALUE_MAX];
    FILE *proc;
    char line[sizeof(DRIVER_MODULE_TAG)+10];
    int propExists = property_get(DRIVER_PROP_NAME, driver_status, NULL);

    if (!propExists || strcmp(driver_status, "") == 0) {
        return 0;  /* driver not loaded */
    }
    /*
     * If the property says the driver is loaded, check to
     * make sure that the property setting isn't just left
     * over from a previous manual shutdown or a runtime
     * crash.
     */
    if ((proc = fopen(MODULE_FILE, "r")) == NULL) {
        ALOGW("wimax: check_driver_loaded: could not open %s: %s", MODULE_FILE, strerror(errno));
        property_set(DRIVER_PROP_NAME, "");
        return 0;
    }
    while ((fgets(line, sizeof(line), proc)) != NULL) {
        if (strncmp(line, DRIVER_MODULE_TAG, strlen(DRIVER_MODULE_TAG)) == 0) {
            fclose(proc);
            return 1;
        }
    }
    fclose(proc);
    property_set(DRIVER_PROP_NAME, "");
    return 0;
}

// unload wimax module and sequansd
int unloadWimaxDriver()
{
    ALOGI("unloadWimaxDriver: Unloading wimax driver...");
    char pid[PROPERTY_VALUE_MAX];
    int count = 20; /* wait at most 10 seconds for completion */
    property_set("ctl.stop", SERVICE_NAME);
    if (property_get(DRIVER_PROP_NAME, pid, NULL)) {
	ALOGI("unloadWimaxDriver: Killing sequansd...");
        kill(atoi(pid), SIGQUIT);
    }
    sched_yield();
    property_set(DRIVER_PROP_NAME, "");
    if (rmmod(DRIVER_MODULE_NAME) == 0) {
        while (count-- > 0) {
            if (!check_driver_loaded())
                break;
            usleep(500000);
        }
        if (count) {
            return 0;
        }
        return -1;
    } else
        return -1;
}

// load wimax module and sequansd
int loadWimaxDriver()
{
    char driver_status[PROPERTY_VALUE_MAX];
    int count = 50; /* wait at most 10 seconds for completion */

	ALOGI("loadWimaxDriver: Checking driver...");
    if (check_driver_loaded()) {
	    ALOGI("loadWimaxDriver: Driver already loaded!");
        return 0;
    }

	ALOGI("loadWimaxDriver() - insmod(driver_mod)");
    if (insmod(DRIVER_MODULE_PATH, DRIVER_MODULE_ARG) < 0)
	    ALOGI("loadWimaxDriver: insmod succeeded!");

        sched_yield();
        while (count-- > 0) {
            if (property_get(DRIVER_PROP_NAME, driver_status, NULL)) {
                if (strcmp(driver_status, "") != 0) {
                    usleep(100000);
                    ALOGI("loadWimaxDriver: sleeping to let sequansd die...");
            }
        }
    }

    count = 50;
	ALOGI("loadWimaxDriver: starting sequansd...");
        property_set("ctl.start", SERVICE_NAME);

    while (count-- > 0) {
        if (property_get(DRIVER_PROP_NAME, driver_status, NULL)) {
            if (strcmp(driver_status, "") != 0)
                return 0;
            else {
                unloadWimaxDriver();
                return 0;
            }
        }
        usleep(200000);
    }
    property_set(DRIVER_PROP_NAME, "");
    unloadWimaxDriver();
    return -1;
}

// start wimax daemon service
int startWimaxDaemon()
{
    char wimax_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 50; // wait at most 15 seconds for completion

    sched_yield();
    //Check whether already running
    if (property_get(DRIVER_PROP_NAME, wimax_status, NULL)
           && strcmp(wimax_status, "running") == 0) {
        ALOGI("startWimaxDaemon: daemon already running");
        return 0;

        if (strcmp(SERVICE_NAME,"") == 0) {
            ALOGI("startWimaxDaemon: sleeping...");
            usleep(100000);
        } else {
            ALOGI("startWimaxDaemon: starting wimax daemon!");
            property_set("ctl.start", SERVICE_NAME);
        }
    }

    while (count-- > 0) {
        if (property_get(DRIVER_PROP_NAME, wimax_status, NULL)) {
            ALOGI("startWimaxDaemon: DRIVER_PROP_NAME not null");
            if (strcmp(wimax_status, "") != 0) {
                ALOGI("startWimaxDaemon: daemon started!");
                return 0;
            }
        }
        usleep(100000);
    }
    property_set(DRIVER_PROP_NAME, "");
    ALOGI("startWimaxDaemon: fail condition...");
    unloadWimaxDriver();
    return -1;
}

// stop wimax daemon service
int stopWimaxDaemon()
{
    char wimax_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 300; /* wait at most 30 seconds for completion */

    /* Check whether supplicant already stopped */
    ALOGI("stopWimaxDaemon: checking driver loaded");
    if (property_get(DRIVER_PROP_NAME, wimax_status, NULL)
            && strcmp(wimax_status, "stopped") == 0) {
        ALOGI("stopWimaxDaemon: wimax driver already stopped!");
        return 0;
    }

    ALOGI("stopWimaxDaemon: stopping wimax daemon...");
    property_set("ctl.stop", SERVICE_NAME);
    sched_yield();

    while (count-- > 0) {
        if (property_get(DRIVER_PROP_NAME, wimax_status, NULL)) {
            if (strcmp(wimax_status, "stopped") == 0)
                ALOGI("stopWimaxDaemon: wimax daemon stopped!");
                return 0;
        }
        usleep(100000);
    }
    return -1;
}

// TODO fix var names
// decrypt responses
size_t decryptString(const char *_buf)
{
  char *buf; // r5@1
  size_t i; // r4@1
  int c; // r3@2
  char newc; // r3@3
  size_t result; // r0@18

  buf = _buf;
  for (i = 0; ; ++i) {
    result = strlen(buf);
    if (i >= result)
        break;
    c = (uint8_t)buf[i];
    if ((uint8_t)(c - 97) > 0xCu) {
      if ((unsigned int)(c - 110) > 0xC) {
        if ((unsigned int)(c - 65) > 5 && (uint8_t)(c - 78) > 5u) {
          if ((unsigned int)(c - 72) > 5 && (uint8_t)(c - 85) > 5u) {
            if ((unsigned int)(c - 52) > 5) {
              if ((unsigned int)(c - 49) > 2)
                   continue;
              newc = c + 6;
            } else {
              newc = c - 3;
            }
          } else {
            newc = c - 7;
          }
        } else {
          newc = c + 7;
        }
      } else {
        newc = c - 13;
      }
    } else {
      newc = c + 13;
    }
    buf[i] = newc;
  }
  return result;
}

// encrypt responses
size_t encryptString(const char *a1)
{
  char *v1; // r5@1
  size_t i; // r4@1
  int v3; // r3@2
  char v4; // r3@3
  size_t result; // r0@18

  v1 = a1;
  for ( i = 0; ; ++i ) {
    result = strlen(v1);
    if ( i >= result )
        break;
    v3 = (uint8_t)v1[i];
    if ( (uint8_t)(v3 - 97) > 0xCu ) {
      if ( (unsigned int)(v3 - 110) > 0xC ) {
        if ( (unsigned int)(v3 - 65) > 5 && (uint8_t)(v3 - 78) > 5u ) {
          if ( (unsigned int)(v3 - 72) > 5 && (uint8_t)(v3 - 85) > 5u ) {
            if ( (unsigned int)(v3 - 49) > 5 ) {
              if ( (unsigned int)(v3 - 55) > 2 )
                  continue;
              v4 = v3 - 6;
            } else {
                v4 = v3 + 3;
            }
          } else {
              v4 = v3 - 7;
          }
        } else {
            v4 = v3 + 7;
        }
      } else {
          v4 = v3 - 13;
      }
    } else {
        v4 = v3 + 13;
    }
    v1[i] = v4;
  }
  return result;
}

// get wimax props from wimax flash, called from wimax daemon (sequansd). response from server is encrypted.
// *result_buffer is a pointer that will receive the result after running the command. JNI will read this buffer.
int getWimaxProp(const char *prop, char *result_buffer)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sockfd, ret, gai_err, s;
    char buffer[1024];
    const char *v11;
    size_t buflen;

    // start service
    property_set("ctl.start", GET_PROP_NAME);

    // load up address structs
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = 2;
    hints.ai_socktype = SOCK_STREAM;

    // get infoz
    gai_err = getaddrinfo("127.0.0.1", "7774", &hints, &res);
    if (gai_err) {
        ALOGE("getWimaxProp: ERROR getaddrinfo: %s\n", gai_strerror(gai_err));
        return -1;
    }

    // get socket
    s = sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        ALOGE("getWimaxProp: ERROR socket");
        return -1;
    }

    // connect
    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
        ALOGE("getWimaxProp: ERROR connect");
        return -1;
    }

     // write to server socket
     memset(buffer, 0, 1024);
     strcpy(buffer, prop);
     buflen = strlen(buffer);

     if (write(sockfd, buffer, buflen + 1) >= 0) {
         memset(buffer, 0, 1024);
          // read response
          if (read(sockfd, buffer, 1023) >= 0 ) {
              // decrypt response
              decryptString(buffer);
              // store response
              if (strcmp(buffer, "##NULL_STRING##")) {
                  if (strcmp(buffer, "##EMPTY_STRING##")) {
                      v11 = buffer;
                  } else {
                      v11 = (const char *)"";
                  }
                  strcpy(result_buffer, v11);
                  close(sockfd);
                  ret = 0;
              } else {
                  strcpy(result_buffer, "##NULL_STRING##");
                  ALOGE("getWimaxProp: reading buffer = NULL, return pPropData = \"##NULL_STRING##\"");
                  close(sockfd);
                  ret = 1;
              }
          } else {
              ALOGE("getWimaxProp: ERROR reading pPropData from socket");
              close(sockfd);
              ret = -1;
          }
      } else {
          ALOGE("getWimaxProp: ERROR writing to socket");
          close(sockfd);
          ret = -1;
      }
    ALOGI("getWimaxProp(prop = %s): service response: %s, converted: %s", prop, buffer, result_buffer);
    return ret;
}

// set wimax props into wimax flash
int setWimaxProp(char* prop, char* val)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sockfd, gai_err, s;
    char *p;
    char buffer[1024], result[1024];
    size_t buflen;

    // start service
    property_set("ctl.start", SET_PROP_NAME);

    // load up address structs
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = 2;
    hints.ai_socktype = SOCK_STREAM;

    // get infoz
    gai_err = getaddrinfo("127.0.0.1", "7773", &hints, &res);
    if (gai_err) {
        ALOGE("setWimaxProp: ERROR getaddrinfo: %s\n", gai_strerror(gai_err));
        return -1;
    }

    // get socket
    s = sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        ALOGE("setWimaxProp: ERROR socket");
        return -1;
    }

    // connect
    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
        ALOGE("setWimaxProp: ERROR connect");
        return -1;
    }

     // write to server socket
     memset(buffer, 0, 1024);
     strcpy(buffer, prop);
     buflen = strlen(buffer);

     if (write(sockfd, buffer, buflen + 1) >= 0) {
         memset(buffer, 0, 1024);
          // read response
          if (read(sockfd, buffer, 1023) >= 0 ) {
              // encrypt response
              encryptString(buffer);
              close(sockfd);
              ALOGI("setWimaxProp: done");
          }else{
              ALOGE("setWimaxProp: ERROR read from socket");
              close(sockfd);
          }
     }else{
        ALOGE("setWimaxProp: ERROR writing to socket");
        close(sockfd);
     }

     // write to server socket
     memset(buffer, 0, 1024);
     strcpy(buffer, val);
     decryptString(buffer);
     buflen = strlen(buffer);

     if (write(sockfd, buffer, buflen + 1) >= 0) {
         memset(buffer, 0, 1024);
          // read response
          if (read(sockfd, buffer, 1023) >= 0 ) {
              close(sockfd);
              ALOGI("setWimaxProp: done");
          }else{
              ALOGE("setWimaxProp: ERROR read from socket"); 
              close(sockfd);
          }
     }else{
        ALOGE("setWimaxProp: ERROR writing to socket");
        close(sockfd);
     }
    ALOGI("setWimaxProp(prop = %s): response = %s",prop, buffer);
    return 0;
}

int doWimaxDhcpRequest()
{
    property_set("ctl.start", "wimaxDhcpRenew");
    return 0;
}

// handle wimax dhcp release
int doWimaxDhcpRelease()
{
    int i = 0;
    signed int result;
    char dhcp_status[PROPERTY_VALUE_MAX] = {'\0'};

    memset(dhcp_status, 0, 92);
    property_set("ctl.start", WIMAX_DHCP_RELEASE);
    do {
      property_get("dhcp.wimax0.reason", dhcp_status, NULL);
      if (!strcmp(dhcp_status, "RELEASE")) {
          ALOGI("dhcpRelease: dhcp release success");
          result = 0;
          goto done;
      }
      ++i;
      usleep(100000);
    }
    while (i != 50);
    ALOGE("dhcpRelease: release fail");
    result = -1;
done:
    return result;
}

// stop wimax dhcp
int stopDhcpWimaxDaemon()
{
    int count = 300; /* wait at most 30 seconds for completion */
    char dhcp_status[PROPERTY_VALUE_MAX] = {'\0'};

    ALOGD("stopDhcpWimaxDaemon: Stopping DHCP...");
    /* Check whether dhcpcd already stopped */
    if (property_get(DHCP_WIMAX_PROP_NAME, dhcp_status, NULL)
        && strcmp(dhcp_status, "stopped") == 0) {
        return 0;
    }

    property_set("ctl.stop", WIMAX_DHCP_NAME);
    sched_yield();

    while (count-- > 0) {
        if (property_get(DHCP_WIMAX_PROP_NAME, dhcp_status, NULL)) {
            if (strcmp(dhcp_status, "stopped") == 0)
                return 0;
        }
        usleep(100000);
    }
    return -1;
}

int startDhcpWimaxDaemon()
{
    char dhcp_status[PROPERTY_VALUE_MAX] = {'\0'};
    int count = 100; /* wait at most 10 seconds for completion */

    ALOGI("startDhcpWimaxDaemon: stopping dhcpWimax");
    stopDhcpWimaxDaemon();

    ifc_init();
    ifc_up("wimax0");
    ALOGI("startDhcpWimaxDaemon: wimax0 up!");

    property_set("ctl.start", WIMAX_DHCP_NAME);
    sched_yield();
    ALOGI("startDhcpWimaxDaemon: dhcp starting...");

    while (count-- > 0) {
        if (property_get("dhcp.wimax0.reason", dhcp_status, NULL)) {
            if (strcmp(dhcp_status, "BOUND") == 0 || strcmp(dhcp_status, "RENEW") == 0 || strcmp(dhcp_status, "PREINIT") == 0) {
                ALOGI("startDhcpWimaxDaemon: dhcp finished!");
                return 0;
            }
        }
        usleep(100000);
    }
    return -1;

}

// terminate a procces. seems to only be used for killing sequansd so far
int terminateProcess(char *pid)
{
    if(!kill(atoi(pid), SIGTERM)) {
       ALOGD("terminateProcess: process terminated successfully.");
//TODO needed?
//     stopDhcpWimaxDaemon();
       return 0;
    } else {
    ALOGE("terminateProcess: process could not be killed!");
  }
  return -1;
}

// TODO test, it should work
// run wimax routing commands. looks hacky eh?
int addRouteToGateway()
{
    char cmd[1024], gw[92], ip[92];

    ALOGD("addRouteToGateway!");
    memset(ip, 0, 92);
    memset(gw, 0, 92);
    property_get("dhcp.wimax0.ipaddress", ip, NULL);
    property_get("dhcp.wimax0.gateway", gw, NULL);
    strcpy(cmd, "/system/bin/ip route flush table wimax");
    system(cmd);
    sprintf(cmd, "/system/bin/ip route add to %s src %s dev wimax0 table wimax", gw, ip);
    system(cmd);
    sprintf(cmd, "/system/bin/ip route append default via %s dev wimax0 table wimax", gw);
    system(cmd);
    strcpy(cmd, "/system/bin/ip route flush dev wimax0");
    sprintf(cmd, "/system/bin/ip route add to %s src %s dev wimax0", gw, ip);
    system(cmd);
    sprintf(cmd, "/system/bin/ip route append default via %s dev wimax0", gw);
    system(cmd);
  return 1;
}

// connect to socket and send command and argument
int thpIoctl(int cmd, long int arg)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sockfd, bytes_recieved, gai_err, s;
    char buffer[1024];
    char * err;
    size_t buflen;

    // load up address structs
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = 2;
    hints.ai_socktype = SOCK_STREAM;

    // get infoz
    gai_err = getaddrinfo("127.0.0.1", "7772", &hints, &res);
    if (gai_err) {
        ALOGE("thpIoctl: ERROR getaddrinfo: %s\n", gai_strerror(gai_err));
        return -1;
    }

    // get socket
    s = sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        ALOGE("thpIoctl: ERROR socket");
        return -1;
    }

    // connect
    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
        ALOGE("thpIoctl: ERROR connect");
        return -1;
    }

    ALOGI("thpIoctl: ioctl_cmd_idx=%d, arg=%ld", cmd, arg);
    memset(buffer, 0, 1024);
    sprintf(buffer, "%d", cmd);
    buflen = strlen(buffer);

    if (write(sockfd, buffer, buflen + 1) >= 0) {
        memset(buffer, 0, 1024);
        sprintf(buffer, "%ld", arg);
        buflen = strlen(buffer);
        if (write(sockfd, buffer, buflen + 1) >= 0 ) {
            shutdown(sockfd, 2);
            close(sockfd);
            return 0;
        }
        err = "ERROR writing to socket";
    } else {
        err = "ERROR writing to socket";
    }
    ALOGI("thpIoctl: wimaxIoctl %s", err);
    close(sockfd);
    return -1;
}

// start wmxCfgItf service
int wimaxConfigInterface()
{
  ALOGI(" wimaxConfigInterface!");
  property_set("ctl.start", WIMAX_CFG_IFACE);

  return 0;
}

// dump wimax ALOGcat messages
int wimaxDumpLogcat()
{
    ALOGD("wimaxDumpLogcat!");
    property_set("ctl.start", WIMAX_LOGCAT);

    return 0;
}

// dump wimax /proc/kmsg messages
int wimaxDumpKmsg()
{
    ALOGI("wimaxDumpKmsg!");
    property_set("ctl.start", WIMAX_KMSG);

    return 0;
}

// dump wimax /proc/last_kmsg messages
int wimaxDumpLastKmsg()
{
    ALOGI("wimaxDumpLastKmsg!");
    property_set("ctl.start", WIMAX_LAST_KMSG);

    return 0;
}

// TODO unhook from jni
int testConnect()
{
	return 0;
}

// methods used by wimax daemon (sequansd) only
// get data from various wimax server sockets
char* getDataFromServer(const char *cmd, char* port, int get_int)
{
    struct addrinfo hints;
    struct addrinfo *res;
    int sockfd, gai_err, s;
    char buffer[1024];
    char *ret_int;
    size_t buflen;

    // load up address structs
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = 2;
    hints.ai_socktype = SOCK_STREAM;

    // get infoz
    gai_err = getaddrinfo("127.0.0.1", port, &hints, &res);
    if (gai_err) {
        ALOGE("getDataFromServer: ERROR getaddrinfo: %s\n", gai_strerror(gai_err));
        goto done;
    }

    // get socket
    s = sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        ALOGE("getDataFromServer: ERROR socket");
        goto done;
    }

    // connect
    if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
        ALOGE("getDataFromServer: ERROR connect");
        goto done;
    }

     // write to server socket
     memset(buffer, 0, 1024);
     strcpy(buffer, cmd);
     if (get_int && !strchr(buffer, 10))
         strcat(buffer, "\n");
     buflen = strlen(buffer);

     if (write(sockfd, buffer, buflen + 1) >= 0) {
         memset(buffer, 0, 1024);
          // read response
          if (read(sockfd, buffer, 1023) >= 0 ) {
              // since get_int arg, return 0 for integer instead of char
              if (get_int) {
                  ret_int = strchr(buffer, 10);
                  if (ret_int)
                      *ret_int = 0;
              }
              // decrypt response
              decryptString(buffer);
              shutdown(sockfd, 2);
              close(sockfd);
              ALOGI("getDataFromServer: done");
          }else{
              ALOGE("getDataFromServer: ERROR read from socket");
              close(sockfd);
          }
     }else{
        ALOGE("getDataFromServer: ERROR writing to socket");
        close(sockfd);
     }
done:
    ALOGI("getDataFromServer(cmd = %s): response = %s",cmd, buffer);
    return buffer;
}

//  connect to wimax java API socket server. WimaxMonitor.java
int getDataFromWimaxStateTracker(const char *cmd)
{
    ALOGI("getDataFromWimaxStateTracker(%s)",cmd);
    return getDataFromServer(cmd, "7775", 1); // flag 1 to return 0 for int
}

// wimaxDaemon
char* getDataFromWimaxDaemon(const char *cmd)
{
     ALOGI("getDataFromWimaxDaemon: cmd=%s", cmd);
     return getDataFromServer(cmd, "7776", 0); // flag 0 to allow char return
}

// TODO set to emmc device now.
// tell if the wimax flash is mtd or emmc and return its proper size to sequansd.
int getBlockSize()
{

    int result;

//  if   // EMMC
    result = 262144;
//  else // MTD
//  result = 131072;
  return 1;
}

// return date and time for wimax daemon (sequansd)
char* getDateAndTime()
{
  time_t rawtime;
  struct tm *timeinfo;
  char buffer[80];
  char *result;

  time(&rawtime);
  timeinfo = localtime(&rawtime);
  strftime(buffer, sizeof(buffer), "%B%d_%H%M%S", timeinfo);
  result = buffer;
  ALOGI("getDateAndTime: result=%s", result);

  return result;
}
