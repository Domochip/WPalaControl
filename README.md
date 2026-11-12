![GitHub Release](https://img.shields.io/github/v/release/Domochip/WPalaControl)
![Home Assistant](https://img.shields.io/badge/home_assistant-2024.11+-blue.svg?logo=homeassistant)

# WPalaControl

**WPalaControl** is a Wi-Fi adapter (based on a D1 Mini) to control and monitor stoves using a **Fumis controller** (Palazzetti, Jotul, Godin, ...).

- 🔥 Power on/off, set point, power level, fans, silent mode, chrono programs, parameters
- 📊 Status, alarms, temperatures, counters, pellet consumption, delta pressure
- 🔌 Native HTTP API compatible with existing Palazzetti/Fumis apps and plugins
- 📡 MQTT with Home Assistant auto-discovery
- ⬆️ Online firmware update from GitHub (also as a Home Assistant Update entity)

## Contents

- [WPalaControl](#wpalacontrol)
  - [Contents](#contents)
  - [Quick start](#quick-start)
  - [Stove compatibility](#stove-compatibility)
  - [Home Automation compatibility](#home-automation-compatibility)
  - [Get your adapter](#get-your-adapter)
  - [Connect it to the stove](#connect-it-to-the-stove)
  - [First boot](#first-boot)
    - [Recovery Mode](#recovery-mode)
  - [Web interface](#web-interface)
    - [Status](#status)
    - [Config](#config)
      - [System](#system)
      - [WiFi](#wifi)
      - [PalaControl](#palacontrol)
      - [PalaControl - Home Automation](#palacontrol---home-automation)
    - [Firmware](#firmware)
  - [Use it](#use-it)
    - [HTTP](#http)
    - [MQTT](#mqtt)
  - [Command list](#command-list)
  - [Published MQTT values](#published-mqtt-values)
  - [Troubleshooting](#troubleshooting)

## Quick start

1. Check that your stove has a [Fumis controller](#stove-compatibility)
2. Plug the module into the stove with the provided cable (or the [right one](#connect-it-to-the-stove) if you built it yourself)
3. Connect to the `WPalaControl` Wi-Fi network (password: `password`)
4. In **Config**, enter your Wi-Fi credentials and save
5. Open <http://wpalacontrol.local>, then configure MQTT / Home Assistant if needed

## Stove compatibility

WPalaControl works with stoves using the **Fumis controller**, used by many manufacturers:

Palazzetti / Jotul / TurboFonte / Godin / Fonte Flamme / Invicta / Casatelli / Alpis / Faizen / HETA / ...

To check if your stove is compatible, look for one of these control panels or motherboards:

![Fumis control panel](img/fumis2.png)![Fumis motherboard](img/fumis.png)

## Home Automation compatibility

| | Integration | Preview |
| --- | --- | --- |
| <img src="https://www.home-assistant.io/images/home-assistant-logo-vertical.svg" alt="Logo Home Assistant" height="128"> | [MQTT Integration](https://www.home-assistant.io/integrations/mqtt/) (auto-discovery)<br>Most options and fan controls | ![Home Assistant MQTT card](img/hacard.png) |
| <img src="https://www.home-assistant.io/images/home-assistant-logo-vertical.svg" alt="Logo Home Assistant" height="128"> | [Palazzetti Integration](https://www.home-assistant.io/integrations/palazzetti)<br>(Home Assistant 2024.11 minimum)<br>Basic implementation for now | ![Home Assistant Palazzetti card](img/hacard-pala-integ.png) |
| <img src="https://raw.githubusercontent.com/jeedom/core/master/core/img/logo-jeedom-petit-nom-couleur-256x256.png" alt="Logo Jeedom" height="128"> | [Palazzetti plugin](https://www.jeedom.com/market/index.php?v=d&p=market_display&id=3104) | ![Jeedom widget](https://domotechdiscord.github.io/Docs/Palazzetti/images/widget_1.png) |

## Get your adapter

![Complete module](img/complete-module.jpg)

- 🛍️ **Ready to use**: I produce small batches, available here:
  [![Lectronz product](img/lectronz-medium.png)](https://lectronz.com/products/wpalacontrol)
- 🔧 **Build it yourself**: schematic, PCB, box and firmware are described in [BUILD](BUILD.md)

## Connect it to the stove

- ✅ If your stove has an accessible RJ12 connector, simply plug the cable in.
- 🔍 If not, you need an additional cable and a splitter: see [Splitter Cabling](SPLITTERCABLING.md).

If you bought your WPalaControl ready to use, the right cable is provided: simply use it.

> [!WARNING]
> If you built it yourself (or use another cable), use the correct cable for your hardware version (written on the PCB):
>
> - Hardware **V1.x**: **crossed** cable
> - Hardware **V2.x**: **straight** cable

![WPalaControl RJ12 pinout](img/rj11-pinout.png)

## First boot

On first boot, the module creates its own Wi-Fi network (AP mode):

| Firmware | SSID | Password |
| --- | --- | --- |
| 3.2.0 and higher | `WPalaControl` | `password` |
| 3.1.x and lower | `WirelessPalaControlXXXX` | `PasswordPalaControl` |

You should be redirected to the module page automatically. If not, go to <http://192.168.4.1>.

Once the module is connected to your Wi-Fi network, it is reachable at 👉 <http://wpalacontrol.local> 👈

> [!NOTE]
> The access point is only started when the module is not connected to your Wi-Fi network (first boot, wrong settings, network down).

### Recovery Mode

If the module can't be reached anymore (e.g. wrong Wi-Fi settings), you can start it in Recovery Mode: press the **RST** button of the D1 Mini **3 times in a row**, each press within 5 seconds of the previous one.

In Recovery Mode, the stored configuration is loaded and kept as is (stove communication and MQTT keep working), but the connection to your Wi-Fi network is disabled: the module only creates its own Wi-Fi network (AP mode, see [First boot](#first-boot)), reachable at <http://192.168.4.1>.

From there, fix the Wi-Fi settings in **Config** → **WiFi** and save them: the module leaves Recovery Mode and connects to your network immediately. A reboot without saving also leaves Recovery Mode, with the unchanged Wi-Fi settings.

> [!NOTE]
> Only presses of the RST button are counted: unplugging/replugging the module does not trigger Recovery Mode.

## Web interface

### Status

<img src="img/status.png" alt="Status page, top" width="400" style="vertical-align: top;"><img src="img/status2.png" alt="Status page, stove values" width="400">

- **System**: module serial number, firmware version, uptime, local time (NTP) and free memory. Logged crashes are listed if any
- **WiFi**: MAC address, Access Point status (and IP), Station status with connection state, IP, IP source (DHCP or Static IP), BSSID and signal strength (RSSI)
- **WPalaControl**:
  - **Hardware Version**: detected (or forced) hardware version of the module
  - **Stove infos**: the stove SN appears first, then all stove values are refreshed live every "Upload Period" (values that changed are shown in bold)
  - **Home Automation Status**: protocol, MQTT connection state and result of the last publish

### Config

![Config page: System and WiFi](img/config.png)

#### System

- **NTP Timezone**: your timezone, used for the module local time (default: Europe/Paris)
- **Reboot**: restart the module
- **Reset Configuration**: erase all settings (System, WiFi, PalaControl) and restart the module with its default configuration (after a confirmation). The module then starts its own Wi-Fi network (AP mode, see [First boot](#first-boot)) and is reachable at <http://192.168.4.1>

#### WiFi

- **SSID & Password**: credentials of your Wi-Fi network. Nearby networks are listed in the dropdown next to the SSID field (strongest signal first)
- **Hostname**: name of the module on the network
- **Static IP**: check to use a fixed IP configuration instead of DHCP
  - **IP, NetMask, Gateway, DNS 1 & 2**: fixed IP configuration (IP is required)

Leave the SSID empty to keep the module in AP mode (reachable at <http://192.168.4.1>).

![Config page: PalaControl](img/configMQTT.png)

#### PalaControl

- **Hardware detection**: hardware version of the module
  - **Auto** (default): version is detected automatically at boot
  - **V1.X** / **V2.X**: force the hardware version if auto-detection fails

#### PalaControl - Home Automation

- **Type**: enable/disable MQTT communication
- **Upload Period**: refresh interval of stove information (15 to 300 seconds)
- **Hostname, Port, Username, Password**: MQTT broker details (username and password are optional)
- **Base Topic**: prefix of all WPalaControl topics
- **MQTT Type**: defines MQTT topics and data structure:
  - **Generic**: raw values under the base topic (e.g. `{baseTopic}/T1` = `20.00`)
  - **Generic JSON**: a JSON per category (e.g. `{baseTopic}/TMPS` = `{"INFO":{"CMD":"GET TMPS","RSP":"OK"},...}`)
  - **Generic Categorized**: raw values under a category topic (e.g. `{baseTopic}/TMPS/T1` = `20.00`)
- **Home Assistant Discovery**: enable/disable Home Assistant auto-discovery
  - **0.2° SetPoint step**: Set Point is adjusted in Home Assistant by 0.2°C steps (using `SET+STPF`) instead of 1°C steps (using `SET+SETP`). Only shown if Home Assistant Discovery is enabled and the stove supports it (air stoves)

### Firmware

![Firmware page](img/firmware.png)

- **Current Firmware**: version installed on the module
- **Online Update**: latest release published on [GitHub](https://github.com/Domochip/WPalaControl/releases) (version, title, date, summary and link)
  - **Refresh**: check GitHub again for the latest release
  - **Update**: download and install the latest release (enabled only if a newer version is available)
- **Manual Update**: upload a firmware file downloaded from the [releases page](https://github.com/Domochip/WPalaControl/releases)
  - Expected file name: `WPalaControl.x.y.z.bin`
  - Older `WirelessPalaControl.*.bin` files are also accepted

> [!CAUTION]
> Do not power off the module during the update. It reboots automatically when the update is done.

- ℹ️ Online Update requires Internet access from the module (not available in AP mode). If you configured a static IP, make sure Gateway and DNS are set (not needed with DHCP).
- 🏠 With MQTT and Home Assistant Discovery enabled, a firmware **Update** entity is also available in Home Assistant (checked once a day).

## Use it

Commands are the same for HTTP and MQTT: see the [Command list](#command-list).

### HTTP

Send an HTTP GET request to the module:

```text
http://wpalacontrol.local/cgi-bin/sendmsg.lua?cmd={command}
```

Example: `http://wpalacontrol.local/cgi-bin/sendmsg.lua?cmd=GET+TMPS` returns:

```json
{"INFO":{"CMD":"GET TMPS","RSP":"OK"},"DATA":{"T1":20.6,"T2":21,"T3":18,"T4":0,"T5":0},"SUCCESS":true}
```

### MQTT

Once MQTT is configured, send commands to the `{baseTopic}/cmd` topic:

```bash
mosquitto_pub -h <broker> -t "WPalaControl/cmd" -m "SET+SETP+21"
```

The result is:

- published following the configured MQTT Type
- published on `{baseTopic}/result` in JSON format

Other topics published by the module:

| Topic | Content |
| --- | --- |
| `{baseTopic}/connected` | `0`: not connected / `1`: connected but no communication with the stove / `2`: module and stove connected |
| `{baseTopic}/Core` | Module status in JSON (manufacturer, model, sn, version, uptime, localtime, freeheap), refreshed every "Upload Period" |
| `{baseTopic}/WiFi` | Wi-Fi status in JSON (apmode, stationmode, stationip, bssid, rssi, ...), refreshed every "Upload Period" |
| `{baseTopic}/App` | WPalaControl status in JSON (hwversion, hwdetection, MQTT status, last publish result, ...), refreshed every "Upload Period" |
| `{baseTopic}/update` | Latest available firmware update information |
| `{baseTopic}/update/install` | Command topic to trigger a firmware update (used by Home Assistant) |

## Command list

⏲️: Published automatically every "Upload Period" (MQTT)  
✨: WPalaControl specific command

| Command | Description | Values | Notes |
| ------- | ----------- | ------ | :---: |
| `GET+STDT` | Get static data | | |
| `GET+ALLS` | Get all status data | | |
| `GET+STAT` | Get stove status | | ⏲️ |
| `GET+TMPS` | Get stove temperatures | | ⏲️ |
| `GET+FAND` | Get fan values | | ⏲️ |
| `GET+SETP` | Get current Set Point (desired temperature) | | ⏲️ |
| `GET+POWR` | Get current power | 1-5 | ⏲️ |
| `GET+CUNT` | Get counters | | |
| `GET+CNTR` | Get counters (same as `GET+CUNT`) | | ⏲️ |
| `GET+DPRS` | Get delta pressure data | | ⏲️ |
| `GET+TIME` | Get stove clock data | | ⏲️ |
| `GET+IOPT` | Get IO ports status | | |
| `GET+SERN` | Get stove Serial Number | | |
| `GET+MDVE` | Get stove model and firmware version | | |
| `GET+CHRD` | Get chrono data | | |
| `GET+PARM+92` | Get parameter (e.g. 92 = pellet type) | parameter number | |
| `GET+HPAR+57` | Get hidden parameter (e.g. 57 = % of pellet to feed for pellet type 3) | parameter number | |
| `BKP+PARM+CSV` | Get all parameters in a CSV file (HTTP only) | | ✨ |
| `BKP+PARM+JSON` | Get all parameters in a JSON file (HTTP only) | | ✨ |
| `BKP+HPAR+CSV` | Get all hidden parameters in a CSV file (HTTP only) | | ✨ |
| `BKP+HPAR+JSON` | Get all hidden parameters in a JSON file (HTTP only) | | ✨ |
| `CMD+ON` | Turn stove ON | | |
| `CMD+OFF` | Turn stove OFF | | |
| `SET+POWR+3` | Set power | 1-5 | |
| `SET+PWRU` | Increase power by 1 unit | | |
| `SET+PWRD` | Decrease power by 1 unit | | |
| `SET+SETP+20` | Set Set Point (desired temperature) | temperature | |
| `SET+STPF+19.8` | Set Set Point with 0.2°C precision (depends on your stove model) | temperature | ✨ |
| `SET+STPU` | Increase Set Point by 1 unit | | |
| `SET+STPD` | Decrease Set Point by 1 unit | | |
| `SET+RFAN+7` | Set Room Fan value | 0-5; 6=Max; 7=Auto | |
| `SET+FN2U` | Increase Room Fan by 1 unit | | |
| `SET+FN2D` | Decrease Room Fan by 1 unit | | |
| `SET+FN3L+0` | Set Room Fan 3 value | 0-5 | |
| `SET+FN4L+0` | Set Room Fan 4 value | 0-5 | |
| `SET+SLNT+0` | Set Silent mode | 0-1 | |
| `SET+TIME+2023-12-28+19:42:00` | Set stove date and time | year 2000-2099, month 1-12, day 1-31, hour 0-23, min 0-59, sec 0-59 | |
| `SET+CSST+0` | Set Chrono status | 0-1 | |
| `SET+CSTH+2+18` | Set Chrono Program start hour | prog 1-6, hour 0-23 | |
| `SET+CSTM+2+30` | Set Chrono Program start minute | prog 1-6, min 0-59 | |
| `SET+CSPH+2+22` | Set Chrono Program stop hour | prog 1-6, hour 0-23 | |
| `SET+CSPM+2+45` | Set Chrono Program stop minute | prog 1-6, min 0-59 | |
| `SET+CSET+2+19` | Set Chrono Program Set Point | prog 1-6, temperature | |
| `SET+CDAY+7+3+6` | Set Chrono Program for a week day (Day-Mem-Prog) | day 1-7, mem 1-3, prog 1-6 | |
| `SET+CPRD+1+19+18+30+22+45` | Set Chrono Program data (Prog-Temp-StartH-StartM-StopH-StopM) | prog 1-6, temperature, 0-23, 0-59, 0-23, 0-59 | |
| `SET+PARM+92+2` | Set parameter 92 to value 2 (pellet type) | parameter number, value (e.g. 1-3 for 92) | |
| `SET+HPAR+57+95` | Set hidden parameter 57 to value 95 (% of pellet to feed for pellet type 3) | parameter number, value | |

## Published MQTT values

Values published automatically every "Upload Period":

| Key | Meaning | Unit |
| --- | ------- | ---- |
| `STATUS`, `LSTATUS`, `FSTATUS` | Stove status | |
| `T1`, `T2`, `T3`, `T4`, `T5` | Stove temperatures | °C |
| `F1V`, `F2V`, `F2L`, `F2LF` | Fan values (meaning depends on your stove model) | |
| `F1RPM` | Fan 1 speed | rpm |
| `F3L`, `F4L` | Fan 3 / Fan 4 levels (if supported by the stove) | 0-5 |
| `F3S`, `F4S` | Fan 3 / Fan 4 values (if supported by the stove) | |
| `IGN` | Ignition counter | |
| `IGNERRORS` | Ignition error counter | |
| `POWERTIME` | Total heating time | hour:minute |
| `HEATTIME` | Unknown meaning | hour:minute |
| `SERVICETIME` | Heating time since last maintenance | hour:minute |
| `ONTIME` | Time since last power ON | hour:minute |
| `OVERTMPERRORS` | Overtemperature error counter | |
| `PQT` | Wood pellet consumption | kg |
| `STOVE_DATETIME` | Stove date and time | |
| `STOVE_WDAY` | Stove week day | |
| `SETP` | Current Set Point (desired temperature) | °C |
| `SECO` | ECO Mode restart temperature threshold (PAR50: 6 -> 0.6°C) | °C |
| `BECO` | ECO Mode active | |
| `PWR` | Current power | 1-5 |
| `FDR` | Feeder | |
| `DP_TARGET` | Delta pressure target | mPa |
| `DP_PRESS` | Actual delta pressure | mPa |

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Stove SN never appears on the Status page | Use the provided cable. If you built it yourself, check the cable type: **crossed** for hardware V1.x, **straight** for V2.x. Check also the **Hardware detection** setting |
| `wpalacontrol.local` does not open | Some devices (e.g. Android, some Windows setups) don't support mDNS: use the module IP address (shown in your router) |
| Wrong Wi-Fi settings, module unreachable | If it can't connect, the module starts its own access point: connect to it and go to <http://192.168.4.1>. You can also press the RST button 3 times to start in [Recovery Mode](#recovery-mode) |
| Online Update fails | The module needs Internet access. If you configured a static IP, make sure Gateway and DNS are set (not needed with DHCP) |
| Nothing published on MQTT | **Status** page → **Home Automation Status** shows the connection state and the last publish result |
