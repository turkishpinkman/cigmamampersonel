#include "employee_manager.h"
#include "config.h"
#include "storage_manager.h"
#include <ArduinoJson.h>
#include <string.h>

static Employee _employees[MAX_EMPLOYEES];

void employeeInit() {
  memset(_employees, 0, sizeof(_employees));

  // 1. Personel temel listesini yukle
  if (storageExists(PATH_EMPLOYEES)) {
    String json = storageRead(PATH_EMPLOYEES);
    if (json.length() > 0) {
      DynamicJsonDocument doc(8192);
      DeserializationError err = deserializeJson(doc, json);
      if (!err) {
        JsonArray arr = doc["employees"].as<JsonArray>();
        int idx = 0;
        for (JsonObject obj : arr) {
          if (idx >= MAX_EMPLOYEES) break;

          const char* uid = obj["uid"] | "";
          const char* name = obj["name"] | "";
          bool active = obj["active"] | true;

          if (active && strlen(uid) > 0) {
            memset(&_employees[idx], 0, sizeof(Employee));
            strncpy(_employees[idx].uid, uid, sizeof(_employees[idx].uid) - 1);
            _employees[idx].uid[sizeof(_employees[idx].uid) - 1] = '\0';

            strncpy(_employees[idx].name, name, sizeof(_employees[idx].name) - 1);
            _employees[idx].name[sizeof(_employees[idx].name) - 1] = '\0';

            _employees[idx].state = STATE_DISARIDA;
            _employees[idx].lastEventTime = 0;
            _employees[idx].breakStartTime = 0;
            _employees[idx].breakCount = 0;
            _employees[idx].active = true;
            idx++;
          }
        }
      } else {
        Serial.printf("[EMPLOYEE] Personel JSON ayristirma hatasi: %s\n", err.c_str());
      }
    }
  }

  // 2. Personel anlik durumlarini yukle (varsa)
  if (storageExists(PATH_STATES)) {
    String json = storageRead(PATH_STATES);
    if (json.length() > 0) {
      DynamicJsonDocument doc(8192);
      DeserializationError err = deserializeJson(doc, json);
      if (!err) {
        JsonArray arr = doc["states"].as<JsonArray>();
        for (JsonObject obj : arr) {
          const char* uid = obj["uid"] | "";
          if (strlen(uid) > 0) {
            int idx = employeeFind(uid);
            if (idx != -1) {
              _employees[idx].state = (EmployeeState)(obj["state"] | (uint8_t)STATE_DISARIDA);
              _employees[idx].lastEventTime = obj["lastEvent"] | 0UL;
              _employees[idx].breakStartTime = obj["breakStart"] | 0UL;
              _employees[idx].breakCount = obj["breakCount"] | 0;
            }
          }
        }
      } else {
        Serial.printf("[EMPLOYEE] Durum JSON ayristirma hatasi: %s\n", err.c_str());
      }
    }
  }

  Serial.printf("[EMPLOYEE] %d personel yuklendi.\n", employeeCount());
}

int employeeFind(const char* uid) {
  if (uid == nullptr || strlen(uid) == 0) return -1;

  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (_employees[i].active && strcmp(_employees[i].uid, uid) == 0) {
      return i;
    }
  }
  return -1;
}

Employee* employeeGet(int index) {
  if (index < 0 || index >= MAX_EMPLOYEES) return nullptr;
  if (!_employees[index].active) return nullptr;
  return &_employees[index];
}

bool employeeAdd(const char* uid, const char* name) {
  if (uid == nullptr || name == nullptr) return false;
  if (strlen(uid) == 0 || strlen(name) == 0) return false;

  // Ayni UID ile baska aktif personel var mi kontrol et
  if (employeeFind(uid) != -1) {
    return false;
  }

  // Ilk bos (inaktif) slotu bul
  int slot = -1;
  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (!_employees[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    return false; // Slot dolu
  }

  memset(&_employees[slot], 0, sizeof(Employee));
  strncpy(_employees[slot].uid, uid, sizeof(_employees[slot].uid) - 1);
  _employees[slot].uid[sizeof(_employees[slot].uid) - 1] = '\0';

  strncpy(_employees[slot].name, name, sizeof(_employees[slot].name) - 1);
  _employees[slot].name[sizeof(_employees[slot].name) - 1] = '\0';

  _employees[slot].state = STATE_DISARIDA;
  _employees[slot].lastEventTime = 0;
  _employees[slot].breakStartTime = 0;
  _employees[slot].breakCount = 0;
  _employees[slot].active = true;

  employeeSave();
  return true;
}

bool employeeRemove(int index) {
  if (index < 0 || index >= MAX_EMPLOYEES) return false;
  if (!_employees[index].active) return false;

  _employees[index].active = false;
  employeeSave();
  employeeSaveStates();
  return true;
}

void employeeSetState(int index, EmployeeState state, unsigned long timestamp) {
  if (index < 0 || index >= MAX_EMPLOYEES || !_employees[index].active) return;

  _employees[index].state = state;
  _employees[index].lastEventTime = timestamp;

  if (state == STATE_MOLADA) {
    _employees[index].breakStartTime = timestamp;
    _employees[index].breakCount++;
  } else if (state == STATE_DISARIDA || state == STATE_CALISIYOR) {
    _employees[index].breakStartTime = 0;
  }

  employeeSaveStates();
}

int employeeCount() {
  int count = 0;
  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (_employees[i].active) {
      count++;
    }
  }
  return count;
}

int employeeActiveCount() {
  int count = 0;
  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (_employees[i].active && (_employees[i].state == STATE_CALISIYOR || _employees[i].state == STATE_MOLADA)) {
      count++;
    }
  }
  return count;
}

Employee* employeeGetAll() {
  return _employees;
}

int employeeGetTotalSlots() {
  return MAX_EMPLOYEES;
}

void employeeSave() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("employees");

  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (_employees[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["uid"] = _employees[i].uid;
      obj["name"] = _employees[i].name;
      obj["active"] = true;
    }
  }

  String output;
  serializeJson(doc, output);
  storageWrite(PATH_EMPLOYEES, output);
}

void employeeSaveStates() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("states");

  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (_employees[i].active) {
      JsonObject obj = arr.createNestedObject();
      obj["uid"] = _employees[i].uid;
      obj["state"] = (uint8_t)_employees[i].state;
      obj["lastEvent"] = _employees[i].lastEventTime;
      obj["breakStart"] = _employees[i].breakStartTime;
      obj["breakCount"] = _employees[i].breakCount;
    }
  }

  String output;
  serializeJson(doc, output);
  storageWrite(PATH_STATES, output);
}

void employeeResetDailyCounters() {
  for (int i = 0; i < MAX_EMPLOYEES; i++) {
    if (_employees[i].active) {
      _employees[i].breakCount = 0;
    }
  }
  employeeSaveStates();
}
