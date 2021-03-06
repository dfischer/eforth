{{opcodes}}

// For now, default on several options.
#define ENABLE_SPIFFS_SUPPORT
#define ENABLE_WIFI_SUPPORT
#define ENABLE_MDNS_SUPPORT
#define ENABLE_WEBSERVER_SUPPORT
#define ENABLE_SDCARD_SUPPORT
#define ENABLE_I2C_SUPPORT

// For now assume only boards with PSRAM (ESP32-CAM)
// will want SerialBluetooth (very large) and camera support.
// Other boards can support these if they're set to a larger
// parition size. But it's unclear the best way to configure this.
#ifdef BOARD_HAS_PSRAM
# define ENABLE_CAMERA_SUPPORT
# define ENABLE_SERIAL_BLUETOOTH_SUPPORT
#endif

#ifdef ENABLE_WEBSERVER_SUPPORT
# include "WebServer.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#if defined(ESP32)
# define HEAP_SIZE (100 * 1024)
# define STACK_SIZE 512
#elif defined(ESP8266)
# define HEAP_SIZE (40 * 1024)
# define STACK_SIZE 512
#else
# define HEAP_SIZE 2 * 1024
# define STACK_SIZE 32
#endif

#define PUSH(v) (DUP, tos = (cell_t) (v))

#define PLATFORM_OPCODE_LIST \
  /* Memory Allocation */ \
  X("MALLOC", MALLOC, tos = (cell_t) malloc(tos)) \
  X("SYSFREE", FREE, free((void *) tos); DROP) \
  X("REALLOC", REALLOC, tos = (cell_t) realloc((void *) *sp, tos); --sp) \
  X("heap_caps_malloc", HEAP_CAPS_MALLOC, tos = (cell_t) heap_caps_malloc(*sp, tos); --sp) \
  X("heap_caps_free", HEAP_CAPS_FREE, heap_caps_free((void *) tos); DROP) \
  X("heap_caps_realloc", HEAP_CAPS_REALLOC, \
      tos = (cell_t) heap_caps_realloc((void *) sp[-1], *sp, tos); sp -= 2) \
  /* Serial */ \
  X("Serial.begin", SERIAL_BEGIN, Serial.begin(tos); DROP) \
  X("Serial.end", SERIAL_END, Serial.end()) \
  X("Serial.available", SERIAL_AVAILABLE, DUP; tos = Serial.available()) \
  X("Serial.readBytes", SERIAL_READ_BYTES, tos = Serial.readBytes((uint8_t *) *sp, tos); --sp) \
  X("Serial.write", SERIAL_WRITE, tos = Serial.write((const uint8_t *) *sp, tos); --sp) \
  X("Serial.flush", SERIAL_FLUSH, Serial.flush()) \
  /* Pins and PWM */ \
  X("pinMode", PIN_MODE, pinMode(*sp, tos); --sp; DROP) \
  X("digitalWrite", DIGITAL_WRITE, digitalWrite(*sp, tos); --sp; DROP) \
  X("digitalRead", DIGITAL_READ, tos = (cell_t) digitalRead(tos)) \
  X("analogRead", ANALOG_READ, tos = (cell_t) analogRead(tos)) \
  X("ledcSetup", LEDC_SETUP, \
      tos = (cell_t) (1000000 * ledcSetup(sp[-1], *sp / 1000.0, tos)); sp -= 2) \
  X("ledcAttachPin", ATTACH_PIN, ledcAttachPin(*sp, tos); --sp; DROP) \
  X("ledcDetachPin", DETACH_PIN, ledcDetachPin(tos); DROP) \
  X("ledcRead", LEDC_READ, tos = (cell_t) ledcRead(tos)) \
  X("ledcReadFreq", LEDC_READ_FREQ, tos = (cell_t) (1000000 * ledcReadFreq(tos))) \
  X("ledcWrite", LEDC_WRITE, ledcWrite(*sp, tos); --sp; DROP) \
  X("ledcWriteTone", LEDC_WRITE_TONE, \
      tos = (cell_t) (1000000 * ledcWriteTone(*sp, tos / 1000.0)); --sp) \
  X("ledcWriteNote", LEDC_WRITE_NOTE, \
      tos = (cell_t) (1000000 * ledcWriteNote(sp[-1], (note_t) *sp, tos)); sp -=2) \
  /* General System */ \
  X("MS", MS, delay(tos); DROP) \
  X("TERMINATE", TERMINATE, exit(tos)) \
  /* File words */ \
  X("R/O", R_O, PUSH(O_RDONLY)) \
  X("R/W", R_W, PUSH(O_RDWR)) \
  X("W/O", W_O, PUSH(O_WRONLY)) \
  X("BIN", BIN, ) \
  X("CLOSE-FILE", CLOSE_FILE, tos = close(tos); tos = tos ? errno : 0) \
  X("FLUSH-FILE", FLUSH_FILE, fsync(tos); /* fsync has no impl and returns ENOSYS :-( */ tos = 0) \
  X("OPEN-FILE", OPEN_FILE, cell_t mode = tos; DROP; cell_t len = tos; DROP; \
    memcpy(filename, (void *) tos, len); filename[len] = 0; \
    tos = open(filename, mode, 0777); PUSH(tos < 0 ? errno : 0)) \
  X("CREATE-FILE", CREATE_FILE, cell_t mode = tos; DROP; cell_t len = tos; DROP; \
    memcpy(filename, (void *) tos, len); filename[len] = 0; \
    tos = open(filename, mode | O_CREAT | O_TRUNC); PUSH(tos < 0 ? errno : 0)) \
  X("DELETE-FILE", DELETE_FILE, cell_t len = tos; DROP; \
    memcpy(filename, (void *) tos, len); filename[len] = 0; \
    tos = unlink(filename); tos = tos ? errno : 0) \
  X("WRITE-FILE", WRITE_FILE, cell_t fd = tos; DROP; cell_t len = tos; DROP; \
    tos = write(fd, (void *) tos, len); tos = tos != len ? errno : 0) \
  X("READ-FILE", READ_FILE, cell_t fd = tos; DROP; cell_t len = tos; DROP; \
    tos = read(fd, (void *) tos, len); PUSH(tos < 0 ? errno : 0)) \
  X("FILE-POSITION", FILE_POSITION, \
    tos = (cell_t) lseek(tos, 0, SEEK_CUR); PUSH(tos < 0 ? errno : 0)) \
  X("REPOSITION-FILE", REPOSITION_FILE, cell_t fd = tos; DROP; \
    tos = (cell_t) lseek(fd, tos, SEEK_SET); tos = tos < 0 ? errno : 0) \
  X("RESIZE-FILE", RESIZE_FILE, cell_t fd = tos; DROP; tos = ResizeFile(fd, tos)) \
  X("FILE-SIZE", FILE_SIZE, struct stat st; w = fstat(tos, &st); \
    tos = (cell_t) st.st_size; PUSH(w < 0 ? errno : 0)) \
  OPTIONAL_SPIFFS_SUPPORT \
  OPTIONAL_WIFI_SUPPORT \
  OPTIONAL_MDNS_SUPPORT \
  OPTIONAL_WEBSERVER_SUPPORT \
  OPTIONAL_SDCARD_SUPPORT \
  OPTIONAL_I2C_SUPPORT \
  OPTIONAL_SERIAL_BLUETOOTH_SUPPORT \
  OPTIONAL_CAMERA_SUPPORT \

#ifndef ENABLE_SPIFFS_SUPPORT
// Provide a default failing SPIFFS.begin
# define OPTIONAL_SPIFFS_SUPPORT \
  X("SPIFFS.begin", SPIFFS_BEGIN, sp -= 2; tos = 0)
#else
# include "SPIFFS.h"
# define OPTIONAL_SPIFFS_SUPPORT \
  /* SPIFFS */ \
  X("SPIFFS.begin", SPIFFS_BEGIN, \
      tos = SPIFFS.begin(sp[-1], (const char *) *sp, tos); sp -=2) \
  X("SPIFFS.end", SPIFFS_END, SPIFFS.end()) \
  X("SPIFFS.format", SPIFFS_FORMAT, DUP; tos = SPIFFS.format()) \
  X("SPIFFS.totalBytes", SPIFFS_TOTAL_BYTES, DUP; tos = SPIFFS.totalBytes()) \
  X("SPIFFS.usedBytes", SPIFFS_USED_BYTES, DUP; tos = SPIFFS.usedBytes())
#endif

#ifndef ENABLE_CAMERA_SUPPORT
# define OPTIONAL_CAMERA_SUPPORT
#else
# include "esp_camera.h"
# define OPTIONAL_CAMERA_SUPPORT \
  /* Camera */ \
  X("esp_camera_init", ESP_CAMERA_INIT, \
      tos = esp_camera_init((const camera_config_t *) tos)) \
  X("esp_camera_deinit", ESP_CAMERA_DEINIT, \
      DUP; tos = esp_camera_deinit()) \
  X("esp_camera_fb_get", ESP_CAMERA_FB_GET, \
      DUP; tos = (cell_t) esp_camera_fb_get()) \
  X("esp_camera_db_return", ESP_CAMERA_FB_RETURN, \
      esp_camera_fb_return((camera_fb_t *) tos); DROP) \
  X("esp_camera_sensor_get", ESP_CAMERA_SENSOR_GET, \
      DUP; tos = (cell_t) esp_camera_sensor_get())
#endif

#ifndef ENABLE_SDCARD_SUPPORT
# define OPTIONAL_SDCARD_SUPPORT
#else
# include "SD_MMC.h"
# define OPTIONAL_SDCARD_SUPPORT \
  /* SD_MMC */ \
  X("SD_MMC.begin", SD_MMC_BEGIN, \
      tos = SD_MMC.begin((const char *) *sp, tos); --sp) \
  X("SD_MMC.end", SD_MMC_END, SD_MMC.end()) \
  X("SD_MMC.cardType", SD_MMC_CARD_TYPE, DUP; tos = SD_MMC.cardType()) \
  X("SD_MMC.totalBytes", SD_MMC_TOTAL_BYTES, DUP; tos = SD_MMC.totalBytes()) \
  X("SD_MMC.usedBytes", SD_MMC_USED_BYTES, DUP; tos = SD_MMC.usedBytes())
#endif

#ifndef ENABLE_I2C_SUPPORT
# define OPTIONAL_I2C_SUPPORT
#else
# include <Wire.h>
# define OPTIONAL_I2C_SUPPORT \
  /* Wire */ \
  X("Wire.begin", WIRE_BEGIN, tos = (cell_t) Wire.begin(*sp, tos); --sp) \
  X("Wire.setClock", WIRE_SET_CLOCK, Wire.setClock(tos); DROP) \
  X("Wire.getClock", WIRE_GET_CLOCK, DUP; tos = (cell_t) Wire.getClock()) \
  X("Wire.setTimeout", WIRE_SET_TIMEOUT, Wire.setTimeout(tos); DROP) \
  X("Wire.getTimeout", WIRE_GET_TIMEOUT, DUP; tos = (cell_t) Wire.getTimeout()) \
  X("Wire.lastError", WIRE_LAST_ERROR, DUP; tos = (cell_t) Wire.lastError()) \
  X("Wire.getErrorText", WIRE_GET_ERROR_TEXT, tos = (cell_t) Wire.getErrorText(tos)) \
  X("Wire.beginTransmission", WIRE_BEGIN_TRANSMISSION, Wire.beginTransmission(tos); DROP) \
  X("Wire.endTransmission", WIRE_END_TRANSMISSION, tos = (cell_t) Wire.endTransmission(tos)) \
  X("Wire.requestFrom", WIRE_REQUEST_FROM, tos = (cell_t) Wire.requestFrom(sp[-1], *sp, tos); sp -= 2) \
  X("Wire.writeTransmission", WIRE_WRITE_TRANSMISSION, \
      tos = (cell_t) Wire.writeTransmission(sp[-2], (uint8_t *) sp[-1], *sp, tos); sp -=3) \
  X("Wire.readTransmission", WIRE_READ_TRANSMISSION, \
      tos = (cell_t) Wire.readTransmission(sp[-3], (uint8_t *) sp[-2], sp[-1], *sp, (uint32_t *) tos); sp -=4) \
  X("Wire.write", WIRE_WRITE, tos = Wire.write((uint8_t *) *sp, tos); --sp) \
  X("Wire.available", WIRE_AVAILABLE, DUP; tos = Wire.available()) \
  X("Wire.read", WIRE_READ, DUP; tos = Wire.read()) \
  X("Wire.peek", WIRE_PEEK, DUP; tos = Wire.peek()) \
  X("Wire.busy", WIRE_BUSY, DUP; tos = Wire.busy()) \
  X("Wire.flush", WIRE_FLUSH, Wire.flush())
#endif

#ifndef ENABLE_SERIAL_BLUETOOTH_SUPPORT
# define OPTIONAL_SERIAL_BLUETOOTH_SUPPORT
#else
# include "esp_bt_device.h"
# include "BluetoothSerial.h"
# define OPTIONAL_SERIAL_BLUETOOTH_SUPPORT \
  /* SerialBT */ \
  X("SerialBT.new", SERIALBT_NEW, DUP; tos = (cell_t) new BluetoothSerial()) \
  X("SerialBT.delete", SERIALBT_DELETE, delete (BluetoothSerial *) tos; DROP) \
  X("SerialBT.begin", SERIALBT_BEGIN, \
      tos = ((BluetoothSerial *) tos)->begin((const char *) sp[-1], *sp); sp -= 2) \
  X("SerialBT.end", SERIALBT_END, ((BluetoothSerial *) tos)->end(); DROP) \
  X("SerialBT.available", SERIALBT_AVAILABLE, tos = ((BluetoothSerial *) tos)->available()) \
  X("SerialBT.readBytes", SERIALBT_READ_BYTES, \
      tos = ((BluetoothSerial *) tos)->readBytes((uint8_t *) sp[-1], *sp); sp -= 2) \
  X("SerialBT.write", SERIALBT_WRITE, \
      tos = ((BluetoothSerial *) tos)->write((const uint8_t *) sp[-1], *sp); sp -= 2) \
  X("SerialBT.flush", SERIALBT_FLUSH, ((BluetoothSerial *) tos)->flush(); DROP) \
  X("SerialBT.hasClient", SERIALBT_HAS_CLIENT, tos = ((BluetoothSerial *) tos)->hasClient()) \
  X("SerialBT.enableSSP", SERIALBT_ENABLE_SSP, ((BluetoothSerial *) tos)->enableSSP(); DROP) \
  X("SerialBT.setPin", SERIALBT_SET_PIN, tos = ((BluetoothSerial *) tos)->setPin((const char *) *sp); --sp) \
  X("SerialBT.unpairDevice", SERIALBT_UNPAIR_DEVICE, \
      tos = ((BluetoothSerial *) tos)->unpairDevice((uint8_t *) *sp); --sp) \
  X("SerialBT.connect", SERIALBT_CONNECT, tos = ((BluetoothSerial *) tos)->connect((const char *) *sp); --sp) \
  X("SerialBT.connectAddr", SERIALBT_CONNECT_ADDR, \
      tos = ((BluetoothSerial *) tos)->connect((uint8_t *) *sp); --sp) \
  X("SerialBT.disconnect", SERIALBT_DISCONNECT, tos = ((BluetoothSerial *) tos)->disconnect()) \
  X("SerialBT.connected", SERIALBT_CONNECTED, tos = ((BluetoothSerial *) tos)->connected(*sp); --sp) \
  X("SerialBT.isReady", SERIALBT_IS_READY, tos = ((BluetoothSerial *) tos)->isReady(sp[-1], *sp); sp -= 2) \
  /* Bluetooth */ \
  X("esp_bt_dev_get_address", ESP_BT_DEV_GET_ADDRESS, DUP; tos = (cell_t) esp_bt_dev_get_address())
#endif

#ifndef ENABLE_WIFI_SUPPORT
# define OPTIONAL_WIFI_SUPPORT
#else
# include <WiFi.h>
# include <WiFiClient.h>

static IPAddress ToIP(cell_t ip) {
  return IPAddress(ip & 0xff, ((ip >> 8) & 0xff), ((ip >> 16) & 0xff), ((ip >> 24) & 0xff));
}

static cell_t FromIP(IPAddress ip) {
  cell_t ret = 0;
  ret = (ret << 8) | ip[3];
  ret = (ret << 8) | ip[2];
  ret = (ret << 8) | ip[1];
  ret = (ret << 8) | ip[0];
  return ret;
}

# define OPTIONAL_WIFI_SUPPORT \
  /* WiFi */ \
  X("WiFi.config", WIFI_CONFIG, \
      WiFi.config(ToIP(sp[-2]), ToIP(sp[-1]), ToIP(*sp), ToIP(tos)); sp -= 3; DROP) \
  X("WiFi.begin", WIFI_BEGIN, \
      WiFi.begin((const char *) *sp, (const char *) tos); --sp; DROP) \
  X("WiFi.disconnect", WIFI_DISCONNECT, WiFi.disconnect()) \
  X("WiFi.status", WIFI_STATUS, DUP; tos = WiFi.status()) \
  X("WiFi.macAddress", WIFI_MAC_ADDRESS, WiFi.macAddress((uint8_t *) tos); DROP) \
  X("WiFi.localIP", WIFI_LOCAL_IPS, DUP; tos = FromIP(WiFi.localIP())) \
  X("WiFi.mode", WIFI_MODE, WiFi.mode((wifi_mode_t) tos); DROP) \
  X("WiFi.setTxPower", WIFI_SET_TX_POWER, WiFi.setTxPower((wifi_power_t) tos); DROP) \
  X("WiFi.getTxPower", WIFI_GET_TX_POWER, DUP; tos = (cell_t) WiFi.getTxPower())
#endif

#ifndef ENABLE_MDNS_SUPPORT
# define OPTIONAL_MDNS_SUPPORT
#else
# include <ESPmDNS.h>
# define OPTIONAL_MDNS_SUPPORT \
  /* mDNS */ \
  X("MDNS.begin", MDNS_BEGIN, tos = MDNS.begin((const char *) tos))
#endif

#ifndef ENABLE_WEBSERVER_SUPPORT
# define OPTIONAL_WEBSERVER_SUPPORT
#else
# include <WebServer.h>
# define OPTIONAL_WEBSERVER_SUPPORT \
  /* WebServer */ \
  X("WebServer.new", WEBSERVER_NEW, DUP; tos = (cell_t) new WebServer(tos)) \
  X("WebServer.delete", WEBSERVER_DELETE, delete (WebServer *) tos; DROP) \
  X("WebServer.begin", WEBSERVER_BEGIN, \
      WebServer *ws = (WebServer *) tos; DROP; ws->begin(tos); DROP) \
  X("WebServer.stop", WEBSERVER_STOP, \
      WebServer *ws = (WebServer *) tos; DROP; ws->stop()) \
  X("WebServer.on", WEBSERVER_ON, \
      InvokeWebServerOn((WebServer *) tos, (const char *) sp[-1], *sp); \
      sp -= 2; DROP) \
  X("WebServer.hasArg", WEBSERVER_HAS_ARG, \
      tos = ((WebServer *) tos)->hasArg((const char *) *sp); DROP) \
  X("WebServer.arg", WEBSERVER_ARG, \
      string_value = ((WebServer *) tos)->arg((const char *) *sp); \
      *sp = (cell_t) string_value.c_str(); tos = string_value.length()) \
  X("WebServer.argi", WEBSERVER_ARGI, \
      string_value = ((WebServer *) tos)->arg(*sp); \
      *sp = (cell_t) string_value.c_str(); tos = string_value.length()) \
  X("WebServer.argName", WEBSERVER_ARG_NAME, \
      string_value = ((WebServer *) tos)->argName(*sp); \
      *sp = (cell_t) string_value.c_str(); tos = string_value.length()) \
  X("WebServer.args", WEBSERVER_ARGS, tos = ((WebServer *) tos)->args()) \
  X("WebServer.setContentLength", WEBSERVER_SET_CONTENT_LENGTH, \
      ((WebServer *) tos)->setContentLength(*sp); --sp; DROP) \
  X("WebServer.sendHeader", WEBSERVER_SEND_HEADER, \
      ((WebServer *) tos)->sendHeader((const char *) sp[-2], (const char *) sp[-1], *sp); \
      sp -= 3; DROP) \
  X("WebServer.send", WEBSERVER_SEND, \
      ((WebServer *) tos)->send(sp[-2], (const char *) sp[-1], (const char *) *sp); \
      sp -= 3; DROP) \
  X("WebServer.sendContent", WEBSERVER_SEND_CONTENT, \
      ((WebServer *) tos)->sendContent((const char *) *sp); --sp; DROP) \
  X("WebServer.method", WEBSERVER_METHOD, \
      tos = (cell_t) ((WebServer *) tos)->method()) \
  X("WebServer.handleClient", WEBSERVER_HANDLE_CLIENT, \
      ((WebServer *) tos)->handleClient(); DROP)
#endif

static char filename[PATH_MAX];
static String string_value;

{{core}}
{{interp}}
{{boot}}

// Work around lack of ftruncate
static cell_t ResizeFile(cell_t fd, cell_t size) {
  struct stat st;
  char buf[256];
  cell_t t = fstat(fd, &st);
  if (t < 0) { return errno; }
  if (size < st.st_size) {
    // TODO: Implement truncation
    return ENOSYS;
  }
  cell_t oldpos = lseek(fd, 0, SEEK_CUR);
  if (oldpos < 0) { return errno; }
  t = lseek(fd, 0, SEEK_END);
  if (t < 0) { return errno; }
  memset(buf, 0, sizeof(buf));
  while (st.st_size < size) {
    cell_t len = sizeof(buf);
    if (size - st.st_size < len) {
      len = size - st.st_size;
    }
    t = write(fd, buf, len);
    if (t != len) {
      return errno;
    }
    st.st_size += t;
  }
  t = lseek(fd, oldpos, SEEK_SET);
  if (t < 0) { return errno; }
  return 0;
}

#ifdef ENABLE_WEBSERVER_SUPPORT
static void InvokeWebServerOn(WebServer *ws, const char *url, cell_t xt) {
  ws->on(url, [xt]() {
    cell_t code[2];
    code[0] = xt;
    code[1] = g_sys.YIELD_XT;
    cell_t stack[16];
    cell_t rstack[16];
    cell_t *rp = rstack;
    *++rp = (cell_t) (stack + 1);
    *++rp = (cell_t) code;
    ueforth_run(rp);
  });
}
#endif

void setup() {
  cell_t *heap = (cell_t *) malloc(HEAP_SIZE);
  ueforth_init(0, 0, heap, boot, sizeof(boot));
}

void loop() {
  g_sys.rp = ueforth_run(g_sys.rp);
}
