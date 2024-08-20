#include "TrafficParser.h"
#include "JsonListener.h"

extern void storeGeometry(double lat, double lon, int edges);
extern void drawGeometry(String cs, int elements);

void TrafficListener::whitespace(char c) {}

void TrafficListener::startDocument() {
  nest = 0;
  arr_nest = 0;
  idx = 0;
  elements = 0;
}

void TrafficListener::key(String key) {
  //  Serial.printf("key(%d %d): %s\n", nest, arr_nest, key.c_str());
  last_key = key;
}

void TrafficListener::value(String value) {
  //  Serial.printf("value(%d %d): %s\n", nest, arr_nest, value.c_str());
  if (last_key == "type") {
    if (nest == 1) {
      type = value;
    } else if (nest == 2) {
      feature_type = value;
    } else if (nest == 3) {
      geometry_type = value;
    }
  } else if (last_key == "target") {
    TrafficListener::target = value;
  } else if (last_key == "cs") {
    TrafficListener::cs = value;
  }
  if (last_key == "coordinates") {
    if (geometry_type == "MultiPolygon") {
      if (cs == "40" || cs == "401" || cs == "402") {
        if (nest == 3 && arr_nest == 5) {
          if (idx == 0) {
            lon = value.toDouble();
            idx = 1;
          } else {
            lat = value.toDouble();
            idx = 0;

            storeGeometry(lat, lon, elements);
            elements++;
          }
        }
      }
    }
  }
}

void TrafficListener::startArray() {
  if (nest == 3 && arr_nest == 3) {
    idx = 0;
    elements = 0;
  }
  arr_nest++;
}

void TrafficListener::endArray() {
  arr_nest--;
  if (nest == 3 && arr_nest == 3) {
    //    Serial.printf("end array\n");
    drawGeometry(cs, elements);
  }
}

void TrafficListener::startObject() { nest++; }

void TrafficListener::endObject() { nest--; }

void TrafficListener::endDocument() {}
