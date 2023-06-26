#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sta, CONFIG_LOG_DEFAULT_LEVEL);

#include <nrfx_clock.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/init.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>


#include <qspi_if.h>

#include "net_private.h"

#include "wifi_sta.h"

#include "deviceinformation.h"


#define CONFIG_STA_SAMPLE_SSID1 "VRT-Telemetry"
#define CONFIG_STA_SAMPLE_SSID2 "motog8"
#define CONFIG_STA_SAMPLE_PASSWORD1 "TJJC2233"
#define CONFIG_STA_SAMPLE_PASSWORD2 "TJJC2233"
#define REDUNDANCY true


//! Wifi thread priority level
#define WIFI_STACK_SIZE 4096
//! Wifi thread priority level
#define WIFI_PRIORITY 4

//! WiFi stack definition
K_THREAD_STACK_DEFINE(WIFI_STACK, WIFI_STACK_SIZE);
//! Variable to identify the Wifi thread
static struct k_thread wifiThread;



#define WIFI_SHELL_MODULE "wifi"

#define WIFI_SHELL_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

#define MAX_SSID_LEN        32
#define DHCP_TIMEOUT        70
#define CONNECTION_TIMEOUT  30
#define STATUS_POLLING_MS   300



static struct net_mgmt_event_callback wifi_shell_mgmt_cb;
static struct net_mgmt_event_callback net_shell_mgmt_cb;

/*
*  context struct from deviceInformation.h
*  
*/

tContext context;



static int cmd_wifi_status(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = { 0 };

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
				sizeof(struct wifi_iface_status))) {
		LOG_INF("Status request failed");

		return -ENOEXEC;
	}

	LOG_INF("==================");
	LOG_INF("State: %s", wifi_state_txt(status.state));

	if (status.state >= WIFI_STATE_ASSOCIATED) {
		uint8_t mac_string_buf[sizeof("xx:xx:xx:xx:xx:xx")];

		LOG_INF("Interface Mode: %s",
		       wifi_mode_txt(status.iface_mode));
		LOG_INF("Link Mode: %s",
		       wifi_link_mode_txt(status.link_mode));
		LOG_INF("SSID: %-32s", status.ssid);
		LOG_INF("BSSID: %s",
		       net_sprint_ll_addr_buf(
				status.bssid, WIFI_MAC_ADDR_LEN,
				mac_string_buf, sizeof(mac_string_buf)));
		LOG_INF("Band: %s", wifi_band_txt(status.band));
		LOG_INF("Channel: %d", status.channel);
		LOG_INF("Security: %s", wifi_security_txt(status.security));
		LOG_INF("MFP: %s", wifi_mfp_txt(status.mfp));
		LOG_INF("RSSI: %d", status.rssi);
	}
	return 0;
}

static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status =(const struct wifi_status *) cb->info;

	if (context.connected) 
	{
		return;
	}

	if (status->status) 
	{
		LOG_ERR("Connection failed (%d)", status->status);
	} 
	else 
	{
		LOG_INF("Connected");
		context.connected = true;
	}

	context.connect_result = true;
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status = (const struct wifi_status *) cb->info;

	if (!context.connected) 
	{
		return;
	}

	if (context.disconnect_requested) 
	{
		LOG_INF("Disconnection request %s (%d)", status->status ? "failed" : "done", status->status);
		context.disconnect_requested = false;
	} 
	else 
	{
		LOG_INF("Received Disconnected");
		context.connected = false;
		context.ip_assigned=false;
	}
    memset(&context, 0, sizeof(context));
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) 
	{
	case NET_EVENT_WIFI_CONNECT_RESULT:
		handle_wifi_connect_result(cb);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		handle_wifi_disconnect_result(cb);
		break;
	default:
		break;
	}
}

static void print_dhcp_ip(struct net_mgmt_event_callback *cb)
{
	/* Get DHCP info from struct net_if_dhcpv4 and print */
	const struct net_if_dhcpv4 *dhcpv4 = cb->info;
	const struct in_addr *addr = &dhcpv4->requested_ip;
	char dhcp_info[128];

	net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));

	LOG_INF("DHCP IP address: %s", dhcp_info);
	context.ip_assigned=true;
}
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) 
	{
	case NET_EVENT_IPV4_DHCP_BOUND:
		print_dhcp_ip(cb);
		break;
	default:
		break;
	}
}

static int __wifi_args_to_params1(struct wifi_connect_req_params *params)
{
	params->timeout = SYS_FOREVER_MS;

	/* SSID */
	params->ssid = CONFIG_STA_SAMPLE_SSID1;
	params->ssid_length = strlen(params->ssid);

#if defined(CONFIG_STA_KEY_MGMT_WPA2)
	params->security = 1;
#elif defined(CONFIG_STA_KEY_MGMT_WPA2_256)
	params->security = 2;
#elif defined(CONFIG_STA_KEY_MGMT_WPA3)
	params->security = 3;
#else
	params->security = 0;
#endif

#if !defined(CONFIG_STA_KEY_MGMT_NONE)
	params->psk = CONFIG_STA_SAMPLE_PASSWORD1;
	params->psk_length = strlen(params->psk);
#endif
	params->channel = WIFI_CHANNEL_ANY;

	/* MFP (optional) */
	params->mfp = WIFI_MFP_OPTIONAL;

	return 0;
}

static int wifi_connect1(void)
{
	struct net_if *iface = net_if_get_default();
	static struct wifi_connect_req_params cnx_params;

	context.connected = false;
	context.ip_assigned = false;
	context.connect_result = false;
	__wifi_args_to_params1(&cnx_params);

	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(struct wifi_connect_req_params))) 
	{
		LOG_ERR("Connection request failed");
		return -ENOEXEC;
	}

	LOG_INF("Connection requested");

	return 0;
}

static int __wifi_args_to_params2(struct wifi_connect_req_params *params)
{
	params->timeout = SYS_FOREVER_MS;

	/* SSID */
	params->ssid = CONFIG_STA_SAMPLE_SSID2;
	params->ssid_length = strlen(params->ssid);

#if defined(CONFIG_STA_KEY_MGMT_WPA2)
	params->security = 1;
#elif defined(CONFIG_STA_KEY_MGMT_WPA2_256)
	params->security = 2;
#elif defined(CONFIG_STA_KEY_MGMT_WPA3)
	params->security = 3;
#else
	params->security = 0;
#endif

#if !defined(CONFIG_STA_KEY_MGMT_NONE)
	params->psk = CONFIG_STA_SAMPLE_PASSWORD2;
	params->psk_length = strlen(params->psk);
#endif
	params->channel = WIFI_CHANNEL_ANY;

	/* MFP (optional) */
	params->mfp = WIFI_MFP_OPTIONAL;

	return 0;
}

static int wifi_connect2(void)
{
	struct net_if *iface = net_if_get_default();
	static struct wifi_connect_req_params cnx_params;

	context.connected = false;
	context.ip_assigned = false;
	context.connect_result = false;
	__wifi_args_to_params2(&cnx_params);

	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(struct wifi_connect_req_params))) 
	{
		LOG_ERR("Connection request failed");
		return -ENOEXEC;
	}

	LOG_INF("Connection requested");

	return 0;
}

int bytes_from_str(const char *str, uint8_t *bytes, size_t bytes_len)
{
	size_t i;
	char byte_str[3];

	if (strlen(str) != bytes_len * 2) {
		LOG_ERR("Invalid string length: %zu (expected: %d)\n",
			strlen(str), bytes_len * 2);
		return -EINVAL;
	}

	for (i = 0; i < bytes_len; i++) {
		memcpy(byte_str, str + i * 2, 2);
		byte_str[2] = '\0';
		bytes[i] = strtol(byte_str, NULL, 16);
	}

	return 0;
}




/*! Wifi_Sta implements the task WiFi Stationing.
* 
* @brief Wifi_Stationing makes the complete connection process, while 
*       printing by LOG commands the connection status.
*       This function is used on an independent thread.
*/
void Wifi_Sta( void )
{

	memset(&context, 0, sizeof(context));

	net_mgmt_init_event_callback(&wifi_shell_mgmt_cb, wifi_mgmt_event_handler, WIFI_SHELL_MGMT_EVENTS);

	net_mgmt_add_event_callback(&wifi_shell_mgmt_cb);

	net_mgmt_init_event_callback(&net_shell_mgmt_cb, net_mgmt_event_handler, NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&net_shell_mgmt_cb);

	k_sleep(K_SECONDS(1));

	while (1) 
    {
		wifi_connect1();

		for (int i = 0; i < CONNECTION_TIMEOUT; i++) 
		{
			k_sleep(K_MSEC(STATUS_POLLING_MS));

			cmd_wifi_status();

			if (context.connect_result) 
			{
				break;
			}
		}
		while(context.connected) 
		{
			k_msleep(100);
		} 
		if (!context.connect_result) 
		{
			LOG_INF("Connection Timed Out");
		}

		wifi_connect2();

		for (int i = 0; i < CONNECTION_TIMEOUT; i++) 
		{
			k_sleep(K_MSEC(STATUS_POLLING_MS));

			cmd_wifi_status();

			if (context.connect_result) 
			{
				break;
			}
		}
		if (context.connected) 
		{
			k_sleep(K_FOREVER);
		} 
		else if (!context.connect_result) 
		{
			LOG_INF("Connection Timed Out");
		}
	}
}

/*! Task_Wifi_Stationing_Init initializes the task Wifi Stationing.
*
* @brief Wifi Stationing initialization
*/
void Task_Wifi_Sta_Init( void ){
    
	k_thread_create	(&wifiThread,
					WIFI_STACK,										        
					WIFI_STACK_SIZE,
					(k_thread_entry_t)Wifi_Sta,
					NULL,
					NULL,
					NULL,
					WIFI_PRIORITY,
					0,
					K_NO_WAIT);	

	 k_thread_name_set(&wifiThread, "wifiStationing");
	 k_thread_start(&wifiThread);
}
