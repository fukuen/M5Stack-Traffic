#pragma once

#include "JsonListener.h"

class TrafficListener : public JsonListener {

public:
  virtual void whitespace(char c);

  virtual void startDocument();

  virtual void key(String key);

  virtual void value(String value);

  virtual void endArray();

  virtual void endObject();

  virtual void endDocument();

  virtual void startArray();

  virtual void startObject();

  int nest = 0;
  int arr_nest = 0;
  int idx, elements = 0;
  double lat, lon;
  String last_key;
  String type;
  String feature_type;
  String geometry_type;
  String target;
  String cs;
};
