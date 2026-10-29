#pragma once

// Copy to secrets.h (which git ignores) and fill in.

#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"

// The machine running `agentsmon serve`, with http.listen = "0.0.0.0:7788"
// and http.token set in ~/.agentsmon/config.toml
#define DAEMON_HOST "192.168.1.18"
#define DAEMON_PORT 7788
#define DAEMON_TOKEN "the-token-from-config.toml"
