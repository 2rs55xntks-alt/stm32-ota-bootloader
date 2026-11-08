#ifndef APP_SECRETS_H
#define APP_SECRETS_H

/* Copy this file to secrets.h and replace every placeholder. */

#define WIFI_SSID              "YOUR_WIFI_SSID"
#define WIFI_PASSWORD          "YOUR_WIFI_PASSWORD"

#define ONENET_PRODUCT_ID      "YOUR_PRODUCT_ID"
#define ONENET_DEVICE_ID       "YOUR_DEVICE_ID"

/*
 * Generate a new token from the OneNET console and paste the complete
 * value after "Authorization:" here.
 */
#define ONENET_OTA_AUTH_VALUE \
  "version=2018-10-31&res=products%2FYOUR_PRODUCT_ID&et=YOUR_EXPIRE_TIME" \
  "&method=md5&sign=YOUR_SIGNATURE"

#endif /* APP_SECRETS_H */

