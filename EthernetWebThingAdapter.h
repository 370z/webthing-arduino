/**
 * EthernetThingAdapter.h
 *
 * Exposes the Web Thing API based on provided ThingDevices.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#if !defined(ESP32) && !defined(ESP8266)

#include <Arduino.h>

#if defined(STM32F7xx)
#include <LwIP.h>
#include <STM32Ethernet.h>
#else
#include <Ethernet.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <Dhcp.h>
#include <Dns.h>
#define CONFIG_MDNS 1
#endif

#ifdef CONFIG_MDNS
#include <ArduinoMDNS.h>
EthernetUDP udp;
MDNS mdns(udp);
#endif

#include <ArduinoJson.h>

#define WITHOUT_WS 1
#include "Thing.h"

#ifndef LARGE_JSON_DOCUMENT_SIZE
#ifdef LARGE_JSON_BUFFERS
#define LARGE_JSON_DOCUMENT_SIZE 4096
#else
#define LARGE_JSON_DOCUMENT_SIZE 1024
#endif
#endif

#ifndef SMALL_JSON_DOCUMENT_SIZE
#ifdef LARGE_JSON_BUFFERS
#define SMALL_JSON_DOCUMENT_SIZE 1024
#else
#define SMALL_JSON_DOCUMENT_SIZE 256
#endif
#endif

static const bool DEBUG = false;

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_PUT, HTTP_OPTIONS };

enum ReadState {
  STATE_READ_METHOD,
  STATE_READ_URI,
  STATE_DISCARD_HTTP11,
  STATE_DISCARD_HEADERS_PRE_HOST,
  STATE_READ_HOST,
  STATE_DISCARD_HEADERS_POST_HOST,
  STATE_READ_CONTENT
};

class WebThingAdapter {
public:
  WebThingAdapter(String _name, uint32_t _ip, uint16_t _port = 80)
      : name(_name), port(_port), server(_port)
#ifdef CONFIG_MDNS
        ,
        mdns(udp)
#endif
  {
    ip = "";
    for (int i = 0; i < 4; i++) {
      ip += _ip & 0xff;
      if (i < 3) {
        ip += '.';
      }
      _ip >>= 8;
    }
  }

  void begin() {
    name.toLowerCase();
#ifdef CONFIG_MDNS
    mdns.begin(Ethernet.localIP(), name.c_str());

    mdns.addServiceRecord("_webthing", port, MDNSServiceTCP, "\x06path=/");
#endif
    server.begin();
  }

  void update() {
#ifdef CONFIG_MDNS
    mdns.run();
#endif
    if (!client) {
      EthernetClient client = server.available();
      if (!client) {
        return;
      }
      if (DEBUG) {
        Serial.println("New client available");
      }
      this->client = client;
    }

    if (!client.connected()) {
      if (DEBUG) {
        Serial.println("Client disconnected");
      }
      resetParser();
      client.stop();
      return;
    }

    char c = client.read();
    if (c == 255 || c == -1) {
      if (state == STATE_READ_CONTENT) {
        handleRequest();
        resetParser();
      }

      retries += 1;
      if (retries > 5000) {
        if (DEBUG) {
          Serial.println("Giving up on client");
        }
        resetParser();
        client.stop();
      }
      return;
    }

    switch (state) {
    case STATE_READ_METHOD:
      if (c == ' ') {
        if (methodRaw == "GET") {
          method = HTTP_GET;
        } else if (methodRaw == "PUT") {
          method = HTTP_PUT;
        } else if (methodRaw == "OPTIONS") {
          method = HTTP_OPTIONS;
        } else {
          method = HTTP_ANY;
        }
        state = STATE_READ_URI;
      } else {
        methodRaw += c;
      }
      break;

    case STATE_READ_URI:
      if (c == ' ') {
        state = STATE_DISCARD_HTTP11;
      } else {
        uri += c;
      }
      break;

    case STATE_DISCARD_HTTP11:
      if (c == '\r') {
        state = STATE_DISCARD_HEADERS_PRE_HOST;
      }
      break;

    case STATE_DISCARD_HEADERS_PRE_HOST:
      if (c == '\r') {
        break;
      }
      if (c == '\n') {
        headerRaw = "";
        break;
      }
      if (c == ':') {
        if (headerRaw.equalsIgnoreCase("Host")) {
          state = STATE_READ_HOST;
        }
        break;
      }

      headerRaw += c;
      break;

    case STATE_READ_HOST:
      if (c == '\r') {
        returnsAndNewlines = 1;
        state = STATE_DISCARD_HEADERS_POST_HOST;
        break;
      }
      if (c == ' ') {
        break;
      }
      host += c;
      break;

    case STATE_DISCARD_HEADERS_POST_HOST:
      if (c == '\r' || c == '\n') {
        returnsAndNewlines += 1;
      } else {
        returnsAndNewlines = 0;
      }
      if (returnsAndNewlines == 4) {
        state = STATE_READ_CONTENT;
      }
      break;

    case STATE_READ_CONTENT:
      content += c;
      break;
    }
  }

  void addDevice(ThingDevice *device) {
    if (this->lastDevice == nullptr) {
      this->firstDevice = device;
      this->lastDevice = device;
    } else {
      this->lastDevice->next = device;
      this->lastDevice = device;
    }
  }

private:
  String name, ip;
  uint16_t port;
  EthernetServer server;
  EthernetClient client;
#ifdef CONFIG_MDNS
  EthernetUDP udp;
  MDNS mdns;
#endif

  ReadState state = STATE_READ_METHOD;
  String uri = "";
  HTTPMethod method = HTTP_ANY;
  String content = "";
  String methodRaw = "";
  String host = "";
  String headerRaw = "";
  int returnsAndNewlines = 0;
  int retries = 0;

  ThingDevice *firstDevice = nullptr, *lastDevice = nullptr;

  bool verifyHost() {
    int colonIndex = host.indexOf(':');
    if (colonIndex >= 0) {
      host.remove(colonIndex);
    }
    if (host == name + ".local") {
      return true;
    }
    if (host == ip) {
      return true;
    }
    if (host == "localhost") {
      return true;
    }
    return false;
  }

  void handleRequest() {
    if (DEBUG) {
      Serial.print("handleRequest: ");
      Serial.print("method: ");
      Serial.println(method);
      Serial.print("uri: ");
      Serial.println(uri);
      Serial.print("host: ");
      Serial.println(host);
      Serial.print("content: ");
      Serial.println(content);
    }

    if (!verifyHost()) {
      client.println("HTTP/1.1 403 Forbidden");
      client.println("Connection: close");
      client.println();
      delay(1);
      client.stop();
      return;
    }

    if (uri == "/") {
      handleThings();
      return;
    }

    ThingDevice *device = this->firstDevice;
    while (device != nullptr) {
      String deviceBase = "/things/" + device->id;

      if (uri.startsWith(deviceBase)) {
        if (uri == deviceBase) {
          if (method == HTTP_GET || method == HTTP_OPTIONS) {
            handleThing(device);
          } else {
            handleError();
          }
          return;
        } else if (uri == deviceBase + "/properties") {
          handleThingPropertiesGet(device->firstProperty);
        } else if (uri == deviceBase + "/events") {
          handleThingEventsGet(device);
        } else {
          ThingProperty *property = device->firstProperty;
          while (property != nullptr) {
            String propertyBase = deviceBase + "/properties/" + property->id;
            if (uri == propertyBase) {
              if (method == HTTP_GET || method == HTTP_OPTIONS) {
                handleThingPropertyGet(property);
              } else if (method == HTTP_PUT) {
                handleThingPropertyPut(property);
              } else {
                handleError();
              }
              return;
            }
            property = (ThingProperty *)property->next;
          }

          ThingEvent *event = device->firstEvent;
          while (event != nullptr) {
            String eventBase = deviceBase + "/events/" + event->id;
            if (uri == eventBase) {
              if (method == HTTP_GET || method == HTTP_OPTIONS) {
                handleThingEventGet(device, event);
              } else {
                handleError();
              }
              return;
            }
            event = (ThingEvent *)event->next;
          }
        }
      }
      device = device->next;
    }
    handleError();
  }

  void sendOk() { client.println("HTTP/1.1 200 OK"); }

  void sendHeaders() {
    client.println("Access-Control-Allow-Origin: *");
    client.println("Access-Control-Allow-Methods: PUT, GET, OPTIONS");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
  }

  void handleThings() {
    sendOk();
    sendHeaders();

    DynamicJsonDocument buf(LARGE_JSON_DOCUMENT_SIZE);
    JsonArray things = buf.to<JsonArray>();
    ThingDevice *device = this->firstDevice;
    while (device != nullptr) {
      JsonObject descr = things.createNestedObject();
      device->serialize(descr);
      descr["href"] = "/things/" + device->id;
      device = device->next;
    }

    serializeJson(things, client);
    delay(1);
    client.stop();
  }

  void handleThing(ThingDevice *device) {
    sendOk();
    sendHeaders();

    DynamicJsonDocument buf(LARGE_JSON_DOCUMENT_SIZE);
    JsonObject descr = buf.to<JsonObject>();
    device->serialize(descr);

    serializeJson(descr, client);
    delay(1);
    client.stop();
  }

  void handleThingPropertyGet(ThingItem *item) {
    sendOk();
    sendHeaders();

    DynamicJsonDocument doc(SMALL_JSON_DOCUMENT_SIZE);
    JsonObject prop = doc.to<JsonObject>();
    item->serialize(prop);
    serializeJson(prop, client);
    delay(1);
    client.stop();
  }

  void handleThingEventGet(ThingDevice *device, ThingItem *item) {
    sendOk();
    sendHeaders();

    DynamicJsonDocument doc(SMALL_JSON_DOCUMENT_SIZE);
    JsonArray queue = doc.to<JsonArray>();
    device->serializeEventQueue(queue, item->id);
    serializeJson(queue, client);
    delay(1);
    client.stop();
  }

  void handleThingPropertiesGet(ThingItem *rootItem) {
    sendOk();
    sendHeaders();

    DynamicJsonDocument doc(LARGE_JSON_DOCUMENT_SIZE);
    JsonObject prop = doc.to<JsonObject>();
    ThingItem *item = rootItem;
    while (item != nullptr) {
      item->serialize(prop);
      item = item->next;
    }
    serializeJson(prop, client);
    delay(1);
    client.stop();
  }

  void handleThingEventsGet(ThingDevice *device) {
    sendOk();
    sendHeaders();

    DynamicJsonDocument doc(LARGE_JSON_DOCUMENT_SIZE);
    JsonArray queue = doc.to<JsonArray>();
    device->serializeEventQueue(queue);
    serializeJson(queue, client);
    delay(1);
    client.stop();
  }

  void setThingProperty(const JsonObject newProp, ThingProperty *property) {
    const JsonVariant newValue = newProp[property->id];

    switch (property->type) {
    case NO_STATE: {
      break;
    }
    case BOOLEAN: {
      ThingDataValue value;
      value.boolean = newValue.as<bool>();
      property->setValue(value);
      break;
    }
    case NUMBER: {
      ThingDataValue value;
      value.number = newValue.as<double>();
      property->setValue(value);
      break;
    }
    case INTEGER: {
      ThingDataValue value;
      value.integer = newValue.as<signed long long>();
      property->setValue(value);
      break;
    }
    case STRING:
      *(property->getValue().string) = newValue.as<String>();
      break;
    }
  }

  void handleThingPropertyPut(ThingProperty *property) {
    sendOk();
    sendHeaders();
    DynamicJsonDocument newBuffer(SMALL_JSON_DOCUMENT_SIZE);
    auto error = deserializeJson(newBuffer, content);
    if (error) { // unable to parse json
      handleError();
      return;
    }
    JsonObject newProp = newBuffer.as<JsonObject>();

    setThingProperty(newProp, property);

    serializeJson(newProp, client);
    delay(1);
    client.stop();
  }

  void handleError() {
    client.println("HTTP/1.1 400 Bad Request");
    sendHeaders();
    delay(1);
    client.stop();
  }

  void resetParser() {
    state = STATE_READ_METHOD;
    method = HTTP_ANY;
    methodRaw = "";
    headerRaw = "";
    host = "";
    uri = "";
    content = "";
    retries = 0;
  }
};

#endif // neither ESP32 nor ESP8266 defined
