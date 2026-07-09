// wifi_dev_credentials.example.h - development WiFi credential seeding.
//
// Copy this file to wifi_dev_credentials.h (git-ignored) and fill in your
// network to skip the setup portal during development. The credentials are
// written to NVS on first boot only when none are stored yet.
//
// NEVER commit wifi_dev_credentials.h - it is listed in .gitignore.
#ifndef WIFI_DEV_CREDENTIALS_H
#define WIFI_DEV_CREDENTIALS_H

#define LUMINA_DEV_WIFI_SSID     "your-network-ssid"
#define LUMINA_DEV_WIFI_PASSWORD "your-network-password"

#endif // WIFI_DEV_CREDENTIALS_H
