// net_portal.h - Captive setup portal served while the setup hotspot is up.
//
// Small HTTP server at http://192.168.4.1 where the user picks the home
// network, enters its password and (optionally) a custom MQTT broker URI.
// A minimal DNS server resolves every name to the portal so phones pop up
// the captive-portal sheet automatically.
#ifndef NET_PORTAL_H
#define NET_PORTAL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t netPortalStart(void);
void netPortalStop(void);

#ifdef __cplusplus
}
#endif

#endif // NET_PORTAL_H
