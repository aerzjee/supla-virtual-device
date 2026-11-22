/*
 Copyright (C) AC SOFTWARE SP. Z O.O.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "devconnection.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "channel-io.h"
#include "devcfg.h"
#include "supla-client-lib/log.h"
#include "supla-client-lib/srpc.h"
#include "supla-client-lib/sthread.h"
#include "supla-client-lib/supla-socket.h"
#include "supla-client-lib/tools.h"

typedef struct {
  void *ssd;
  void *srpc;
  void *sthread;

  char registered;

  struct timeval last_call;
  unsigned char server_activity_timeout;
  time_t connection_start_time;
} TDeviceConnectionData;

void devconnection_send_all_channel_states(TDeviceConnectionData *dcd);

int devconnection_socket_read(void *buf, int count, void *dcd) {
  return ssocket_read(((TDeviceConnectionData *)dcd)->ssd, NULL, buf, count);
}

int devconnection_socket_write(void *buf, int count, void *dcd) {
  return ssocket_write(((TDeviceConnectionData *)dcd)->ssd, NULL, buf, count);
}

void devconnection_before_async_call(void *_srpc, unsigned int call_type,
                                     void *_dcd) {
  TDeviceConnectionData *dcd = (TDeviceConnectionData *)_dcd;
  gettimeofday(&dcd->last_call, NULL);
}

void devconnection_terminate(void *sthread) {
#ifdef __SINGLE_THREAD
  st_app_terminate = 1;
#else
  sthread_terminate(sthread);
#endif
}

char devconnection_isterminated(void *sthread) {
#ifdef __SINGLE_THREAD
  return st_app_terminate != 0 ? 1 : 0;
#else
  return sthread_isterminated(sthread) ? 1 : 0;
#endif
}

void devconnection_on_version_error(TDeviceConnectionData *dcd,
                                    TSDC_SuplaVersionError *version_error) {
  supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_ERR,
                         "Protocol version error. Server doesn't support this "
                         "Device. S:%d-%d/D:%d",
                         version_error->server_version_min,
                         version_error->server_version, SUPLA_PROTO_VERSION);

  devconnection_terminate(dcd->sthread);
}

void devconnection_channel_valuechanged(unsigned char number,
                                        char value[SUPLA_CHANNELVALUE_SIZE],
                                        void *_dcd) {
  srpc_ds_async_channel_value_changed(((TDeviceConnectionData *)_dcd)->srpc,
                                      number, value);
}

void devconnection_channel_extendedValuechanged(
    unsigned char number, TSuplaChannelExtendedValue *value, void *_dcd) {
  srpc_ds_async_channel_extendedvalue_changed(
      ((TDeviceConnectionData *)_dcd)->srpc, number, value);
}

void devconnection_on_register_result(
    TDeviceConnectionData *dcd,
    TSD_SuplaRegisterDeviceResult *register_device_result) {
  switch (register_device_result->result_code) {
    case SUPLA_RESULTCODE_BAD_CREDENTIALS:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_ERR,
                             "Bad credentials!");
      break;

    case SUPLA_RESULTCODE_TEMPORARILY_UNAVAILABLE:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Temporarily unavailable!");
      break;

    case SUPLA_RESULTCODE_LOCATION_CONFLICT:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_ERR,
                             "Location conflict!");
      break;

    case SUPLA_RESULTCODE_CHANNEL_CONFLICT:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_ERR,
                             "Channel conflict!");
      break;
    case SUPLA_RESULTCODE_TRUE:
      dcd->registered = 1;
      dcd->connection_start_time = time(NULL);
      dcd->server_activity_timeout = register_device_result->activity_timeout;

      channelio_setcalback_on_channel_value_changed(
          &devconnection_channel_valuechanged,
          &devconnection_channel_extendedValuechanged, dcd);

      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_DEBUG,
                             "Registered and ready.");

      devconnection_send_all_channel_states(dcd);

      break;
    case SUPLA_RESULTCODE_DEVICE_DISABLED:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Device is disabled!");
      break;

    case SUPLA_RESULTCODE_LOCATION_DISABLED:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Location is disabled!");
      break;

    case SUPLA_RESULTCODE_DEVICE_LIMITEXCEEDED:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Device limit exceeded!");
      break;

    case SUPLA_RESULTCODE_GUID_ERROR:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Incorrect device GUID!");
      break;

    case SUPLA_RESULTCODE_REGISTRATION_DISABLED:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Registration disabled!");
      break;

    case SUPLA_RESULTCODE_AUTHKEY_ERROR:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "Incorrect AuthKey!");
      break;

    case SUPLA_RESULTCODE_NO_LOCATION_AVAILABLE:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "No location available!");
      break;

    case SUPLA_RESULTCODE_USER_CONFLICT:
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_NOTICE,
                             "User conflict!");
      break;
  }
}

void devconnection_channel_set_value(TDeviceConnectionData *dcd,
                                     TSD_SuplaChannelNewValue *new_value) {
  char Success = channelio_set_value(new_value->ChannelNumber, new_value->value,
                                     new_value->DurationMS) == 1
                     ? 1
                     : 0;
  srpc_ds_async_set_channel_result(dcd->srpc, new_value->ChannelNumber,
                                   new_value->SenderID, Success);
}

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

static void devconnection_get_ip_mac(void *ssd, in_addr_t *out_ip,
                                     unsigned char out_mac[6]) {
  *out_ip = 0;
  memset(out_mac, 0, 6);

  int fd = ssocket_get_fd(ssd);
  in_addr_t sock_ip = 0;
  if (fd >= 0) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(fd, (struct sockaddr *)&sin, &len) == 0 &&
        sin.sin_family == AF_INET) {
      sock_ip = sin.sin_addr.s_addr;
      *out_ip = sock_ip;
    }
  }

  struct ifaddrs *ifap = NULL;
  if (getifaddrs(&ifap) != 0 || ifap == NULL) {
    return;
  }

  int found_mac = 0;
  if (sock_ip != 0) {
    for (struct ifaddrs *ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        if (sa->sin_addr.s_addr == sock_ip) {
          int sock = socket(AF_INET, SOCK_DGRAM, 0);
          if (sock >= 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
              memcpy(out_mac, ifr.ifr_hwaddr.sa_data, 6);
              found_mac = 1;
            }
            close(sock);
          }
          break;
        }
      }
    }
  }

  if (!found_mac) {
    for (struct ifaddrs *ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
        if (strcmp(ifa->ifa_name, "lo") != 0) {
          struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
          if (*out_ip == 0) {
            *out_ip = sa->sin_addr.s_addr;
          }
          int sock = socket(AF_INET, SOCK_DGRAM, 0);
          if (sock >= 0) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
              memcpy(out_mac, ifr.ifr_hwaddr.sa_data, 6);
              found_mac = 1;
            }
            close(sock);
          }
          break;
        }
      }
    }
  }

  freeifaddrs(ifap);
}

static void devconnection_fill_device_state(TDeviceConnectionData *dcd,
                                            TDSC_ChannelState *state) {
  in_addr_t ip = 0;
  unsigned char mac[6] = {0};
  devconnection_get_ip_mac(dcd->ssd, &ip, mac);
  if (ip != 0) {
    state->IPv4 = ip;
    state->Fields |= SUPLA_CHANNELSTATE_FIELD_IPV4;
  }
  char has_mac = 0;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0) {
      has_mac = 1;
      break;
    }
  }
  if (has_mac) {
    memcpy(state->MAC, mac, 6);
    state->Fields |= SUPLA_CHANNELSTATE_FIELD_MAC;
  }

  struct sysinfo si;
  if (sysinfo(&si) == 0) {
    state->Uptime = (unsigned int)si.uptime;
    state->Fields |= SUPLA_CHANNELSTATE_FIELD_UPTIME;
  }

  if (dcd->connection_start_time > 0) {
    time_t now = time(NULL);
    if (now >= dcd->connection_start_time) {
      state->ConnectionUptime =
          (unsigned int)(now - dcd->connection_start_time);
      state->Fields |= SUPLA_CHANNELSTATE_FIELD_CONNECTIONUPTIME;
    }
  }
}

void devconnection_send_all_channel_states(TDeviceConnectionData *dcd) {
  if (dcd == NULL || dcd->srpc == NULL || dcd->registered != 1) return;

  int count = channelio_channel_count();
  for (int a = 0; a < count; a++) {
    unsigned char ch_num = channelio_get_channel_number(a);
    TDSC_ChannelState state;
    memset(&state, 0, sizeof(TDSC_ChannelState));
    state.ReceiverID = 0;
    state.ChannelNumber = ch_num;

    channelio_get_channel_state(ch_num, &state);
    devconnection_fill_device_state(dcd, &state);

    srpc_csd_async_channel_state_result(dcd->srpc, &state);
  }
}

void devconnection_channel_get_channel_state(
    TDeviceConnectionData *dcd, TCSD_ChannelStateRequest *request) {
  TDSC_ChannelState state;
  memset(&state, 0, sizeof(TDSC_ChannelState));
  state.ReceiverID = request->SenderID;
  state.ChannelNumber = request->ChannelNumber;

  channelio_get_channel_state(request->ChannelNumber, &state);
  devconnection_fill_device_state(dcd, &state);

  srpc_csd_async_channel_state_result(dcd->srpc, &state);
}

void devconnection_channel_calcfg_request(
    TDeviceConnectionData *dcd, TSD_DeviceCalCfgRequest *cal_cfg_request) {
  TSD_DeviceCalCfgRequest_B *test =
      (TSD_DeviceCalCfgRequest_B *)cal_cfg_request;
}

void devconnection_on_remote_call_received(void *_srpc, unsigned int rr_id,
                                           unsigned int call_type, void *_dcd,
                                           unsigned char proto_version) {
  TsrpcReceivedData rd;

  char result;

  TDeviceConnectionData *dcd = (TDeviceConnectionData *)_dcd;

  if (SUPLA_RESULT_TRUE == (result = srpc_getdata(_srpc, &rd, 0))) {
    switch (rd.call_type) {
      case SUPLA_SDC_CALL_VERSIONERROR:
        devconnection_on_version_error(dcd, rd.data.sdc_version_error);
        break;
      case SUPLA_SD_CALL_REGISTER_DEVICE_RESULT:
        devconnection_on_register_result(dcd,
                                         rd.data.sd_register_device_result);
        break;
      case SUPLA_SD_CALL_CHANNEL_SET_VALUE:
        devconnection_channel_set_value(dcd, rd.data.sd_channel_new_value);
        break;
      case SUPLA_SD_CALL_DEVICE_CALCFG_REQUEST:
        devconnection_channel_calcfg_request(dcd,
                                             rd.data.sd_device_calcfg_request);
        break;

      case SUPLA_CSD_CALL_GET_CHANNEL_STATE:
        devconnection_channel_get_channel_state(
            dcd, rd.data.csd_channel_state_request);
        break;
    }
    srpc_rd_free(&rd);
  } else if (result == SUPLA_RESULT_DATA_ERROR) {
    supla_log(LOG_DEBUG, "DATA ERROR!");
  }
}

void devconnection_register(TDeviceConnectionData *dcd) {
  char *Email = scfg_string(CFG_EMAIL);

  if (Email == NULL || strnlen(Email, SUPLA_EMAIL_MAXSIZE) == 0) {
    TDS_SuplaRegisterDevice_C srd;
    memset(&srd, 0, sizeof(TDS_SuplaRegisterDevice_C));

    srd.channel_count = 0;
    srd.LocationID = scfg_int(CFG_LOCATION_ID);
    snprintf(srd.LocationPWD, SUPLA_LOCATION_PWD_MAXSIZE, "%s",
             scfg_string(CFG_LOCATION_PWD));
    snprintf(srd.Name, SUPLA_DEVICE_NAME_MAXSIZE, "%s",
             scfg_string(CFG_DEVNAME));
    snprintf(srd.ServerName, SUPLA_SERVER_NAME_MAXSIZE, "%s",
             scfg_string(CFG_SERVER_HOST));
    snprintf(srd.SoftVer, SUPLA_SOFTVER_MAXSIZE, SOFTWARE_VERSION);

    memcpy(srd.GUID, DEVICE_GUID, SUPLA_GUID_SIZE);

    channelio_channels_to_srd_b(&srd.channel_count, srd.channels);

    srpc_ds_async_registerdevice_c(dcd->srpc, &srd);

  } else {
    TDS_SuplaRegisterDevice_E srd;
    memset(&srd, 0, sizeof(TDS_SuplaRegisterDevice_E));

    srd.channel_count = 0;

    snprintf(srd.Email, SUPLA_EMAIL_MAXSIZE, "%s", Email);
    snprintf(srd.Name, SUPLA_DEVICE_NAME_MAXSIZE, "%s",
             scfg_string(CFG_DEVNAME));
    snprintf(srd.ServerName, SUPLA_SERVER_NAME_MAXSIZE, "%s",
             scfg_string(CFG_SERVER_HOST));
    snprintf(srd.SoftVer, SUPLA_SOFTVER_MAXSIZE, "%s", SOFTWARE_VERSION);

    memcpy(srd.GUID, DEVICE_GUID, SUPLA_GUID_SIZE);
    memcpy(srd.AuthKey, DEVICE_AUTHKEY, SUPLA_AUTHKEY_SIZE);

    channelio_channels_to_srd_c(&srd.channel_count, srd.channels);

    srpc_ds_async_registerdevice_e(dcd->srpc, &srd);
  }
}

void devconnection_ping(TDeviceConnectionData *dcd) {
  struct timeval now;

  if (dcd->server_activity_timeout > 0) {
    gettimeofday(&now, NULL);

    if ((now.tv_sec - dcd->last_call.tv_sec) >=
        (dcd->server_activity_timeout - 5)) {
      srpc_dcs_async_ping_server(dcd->srpc);
    }
  }
}

void devconnection_execute(void *user_data, void *sthread) {
  TDeviceConnectionData dcd;
  memset(&dcd, 0, sizeof(TDeviceConnectionData));

  dcd.sthread = sthread;
  char ssl = scfg_bool(CFG_SERVER_SSLENABLED);
  dcd.ssd = ssocket_client_init(
      scfg_string(CFG_SERVER_HOST),
      scfg_int(ssl == 1 ? CFG_SERVER_SSLPORT : CFG_SERVER_TCPPORT), ssl);

  if (dcd.ssd) {
    while (devconnection_isterminated(sthread) == 0) {
      supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_DEBUG,
                             "Connecting...");

      if (ssocket_client_connect(dcd.ssd, scfg_string(CFG_STATE_FILE), NULL) ==
          0) {
        usleep(5000000);

      } else {
        TEventHandler *eh = eh_init();
        TsrpcParams srpc_params;
        srpc_params_init(&srpc_params);
        srpc_params.user_params = &dcd;
        srpc_params.data_read = &devconnection_socket_read;
        srpc_params.data_write = &devconnection_socket_write;
        srpc_params.on_remote_call_received =
            &devconnection_on_remote_call_received;
        srpc_params.before_async_call = &devconnection_before_async_call;
        srpc_params.eh = eh;
        dcd.srpc = srpc_init(&srpc_params);
        srpc_set_proto_version(dcd.srpc, scfg_int(CFG_PROTO));

        eh_add_fd(eh, ssocket_get_fd(dcd.ssd));

        dcd.registered = 0;
        dcd.connection_start_time = 0;

        while (devconnection_isterminated(sthread) == 0) {
          if (dcd.registered == 0) {
            supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_DEBUG,
                                   "Register in progress...");
            dcd.registered = -1;
            devconnection_register(&dcd);

          } else if (dcd.registered == 1) {
#ifdef __SINGLE_THREAD
            channelio_iterate();
#endif

            devconnection_ping(&dcd);
          }

          if (srpc_iterate(dcd.srpc) == SUPLA_RESULT_FALSE) {
            supla_log(LOG_DEBUG, "srpc_iterate(srpc) == SUPLA_RESULT_FALSE");
            break;
          }

#ifdef __SINGLE_THREAD
          eh_wait(eh, 10000);
#else
          eh_wait(eh, 1000000);
#endif
        }

        if (dcd.registered == 1) {
          supla_write_state_file(scfg_string(CFG_STATE_FILE), LOG_DEBUG,
                                 "Disconnected");
        }
        dcd.connection_start_time = 0;

        srpc_free(dcd.srpc);
        eh_free(eh);
      }

      channelio_setcalback_on_channel_value_changed(NULL, NULL, NULL);
    }

    ssocket_free(dcd.ssd);

  } else {
    supla_log(LOG_ERR, "Can't initialize ssocket!");
  }
}

void *devconnection_start(void) {
#ifdef __SINGLE_THREAD

  devconnection_execute(NULL, NULL);
  return NULL;

#else

  Tsthread_params stp;
  stp.execute = devconnection_execute;
  stp.finish = NULL;
  stp.user_data = NULL;
  stp.free_on_finish = 0;
  stp.initialize = NULL;

  return sthread_run(&stp);

#endif
}

void devconnection_stop(void *dconn_thread) {
  if (dconn_thread != NULL) sthread_twf(dconn_thread);
}
