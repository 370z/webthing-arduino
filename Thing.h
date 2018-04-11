#ifndef MOZILLA_IOT_THING_H
#define MOZILLA_IOT_THING_H

enum ThingPropertyType {
  BOOLEAN,
  NUMBER,
  STRING
};

union ThingPropertyValue {
  bool boolean;
  float number;
  char* string;
};

class ThingProperty {
public:
  String id;
  String description;
  ThingPropertyType type;
  ThingProperty* next = nullptr;

  ThingProperty(const char* id_, const char* description_, ThingPropertyType type_):
    id(id_),
    description(description_),
    type(type_) {
  }

  void setValue(ThingPropertyValue newValue) {
    this->value = newValue;
  }

  ThingPropertyValue getValue() {
    return this->value;
  }

private:
  ThingPropertyValue value = {false};
};

class ThingDevice {
public:
  String id;
  String name;
  String type;
  ThingDevice* next = nullptr;
  ThingProperty* firstProperty = nullptr;
  ThingProperty* lastProperty = nullptr;

  ThingDevice(const char* _id, const char* _name, const char* _type):
    id(_id),
    name(_name),
    type(_type) {
  }

  void addProperty(ThingProperty* property) {
    if (lastProperty == nullptr) {
      firstProperty = property;
      lastProperty = property;
    } else {
      lastProperty->next = property;
      lastProperty = property;
    }
  }
};

#endif
