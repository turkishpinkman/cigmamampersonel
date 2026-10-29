#include "event_logger.h"
#include "config.h"
#include "storage_manager.h"
#include "time_manager.h"
#include <ArduinoJson.h>
#include <string.h>

static LogEvent _events[MAX_PENDING_EVENTS];
static int _eventCount = 0;
static unsigned long _nextId = 1;

void eventInit() {
  memset(_events, 0, sizeof(_events));
  _eventCount = 0;
  _nextId = 1;

  if (storageExists(PATH_EVENTS)) {
    String json = storageRead(PATH_EVENTS);
    if (json.length() > 0) {
      size_t docSize = json.length() * 2 + 1024;
      if (docSize < 4096) docSize = 4096;
      DynamicJsonDocument doc(docSize);
      DeserializationError err = deserializeJson(doc, json);
      if (!err) {
        if (doc.containsKey("nextId")) {
          _nextId = doc["nextId"] | 1UL;
        }

        JsonArray arr = doc["events"].as<JsonArray>();
        _eventCount = 0;
        for (JsonObject obj : arr) {
          if (_eventCount >= MAX_PENDING_EVENTS) break;

          _events[_eventCount].id = obj["id"] | 0UL;

          const char* uid = obj["uid"] | "";
          strncpy(_events[_eventCount].uid, uid, sizeof(_events[_eventCount].uid) - 1);
          _events[_eventCount].uid[sizeof(_events[_eventCount].uid) - 1] = '\0';

          const char* name = obj["name"] | "";
          strncpy(_events[_eventCount].name, name, sizeof(_events[_eventCount].name) - 1);
          _events[_eventCount].name[sizeof(_events[_eventCount].name) - 1] = '\0';

          _events[_eventCount].type = (EventType)(obj["type"] | 0);
          _events[_eventCount].timestamp = obj["ts"] | 0UL;

          const char* date = obj["date"] | "";
          strncpy(_events[_eventCount].date, date, sizeof(_events[_eventCount].date) - 1);
          _events[_eventCount].date[sizeof(_events[_eventCount].date) - 1] = '\0';

          _events[_eventCount].synced = obj["synced"] | false;

          if (_events[_eventCount].id >= _nextId) {
            _nextId = _events[_eventCount].id + 1;
          }
          _eventCount++;
        }

        if (_nextId == 0) _nextId = 1;

        Serial.printf("[EVENT] %d olay yuklendi. Sonraki ID: %lu\n", _eventCount, _nextId);
      } else {
        Serial.printf("[EVENT] JSON ayristirma hatasi: %s\n", err.c_str());
      }
    }
  }
}

void eventLog(const char* uid, const char* name, EventType type, unsigned long timestamp) {
  // 1. Kapasite dolduysa once 7 gunden eski senkronize kayitlari temizle
  if (_eventCount >= MAX_PENDING_EVENTS) {
    eventCleanup();
  }

  // 2. Hala doluysa once en eski senkronize olayi cikar, hic senkronize yoksa en eski olayi cikar
  if (_eventCount >= MAX_PENDING_EVENTS) {
    int oldestSyncedIdx = -1;
    for (int i = 0; i < _eventCount; i++) {
      if (_events[i].synced) {
        oldestSyncedIdx = i;
        break;
      }
    }

    if (oldestSyncedIdx != -1) {
      for (int i = oldestSyncedIdx; i < _eventCount - 1; i++) {
        _events[i] = _events[i + 1];
      }
      _eventCount--;
    } else {
      // Tum olaylar senkronize edilmemis olsa bile yer acmak icin en eski olayi cikar
      for (int i = 0; i < _eventCount - 1; i++) {
        _events[i] = _events[i + 1];
      }
      _eventCount--;
    }
  }

  // 3. Yeni olayi doldur
  LogEvent& ev = _events[_eventCount];
  memset(&ev, 0, sizeof(LogEvent));

  ev.id = _nextId++;
  if (_nextId == 0) _nextId = 1;

  if (uid != nullptr) {
    strncpy(ev.uid, uid, sizeof(ev.uid) - 1);
    ev.uid[sizeof(ev.uid) - 1] = '\0';
  }

  if (name != nullptr) {
    strncpy(ev.name, name, sizeof(ev.name) - 1);
    ev.name[sizeof(ev.name) - 1] = '\0';
  }

  ev.type = type;
  ev.timestamp = timestamp;
  dateFormat(ev.date, timestamp);
  ev.synced = false;

  _eventCount++;

  eventSave();

  Serial.printf("Olay kaydedildi: %s - %s\n", ev.name, eventTypeToStr(type));
}

int eventPendingCount() {
  int count = 0;
  for (int i = 0; i < _eventCount; i++) {
    if (!_events[i].synced) {
      count++;
    }
  }
  return count;
}

LogEvent* eventGetPending(int& count) {
  count = _eventCount;
  return _events;
}

void eventMarkSynced(unsigned long id) {
  for (int i = 0; i < _eventCount; i++) {
    if (_events[i].id == id) {
      if (!_events[i].synced) {
        _events[i].synced = true;
        eventSave();
      }
      return;
    }
  }
}

void eventCleanup() {
  unsigned long now = timeNow();
  const unsigned long SEVEN_DAYS = 7UL * 24UL * 3600UL; // 604800 saniye

  bool checkTime = (now > SEVEN_DAYS);
  unsigned long cutoff = checkTime ? (now - SEVEN_DAYS) : 0;

  int writeIdx = 0;
  bool changed = false;

  for (int readIdx = 0; readIdx < _eventCount; readIdx++) {
    bool shouldRemove = false;
    if (_events[readIdx].synced) {
      if (checkTime && _events[readIdx].timestamp < cutoff) {
        shouldRemove = true;
      }
    }

    if (!shouldRemove) {
      if (writeIdx != readIdx) {
        _events[writeIdx] = _events[readIdx];
      }
      writeIdx++;
    } else {
      changed = true;
    }
  }

  if (changed) {
    _eventCount = writeIdx;
    eventSave();
    Serial.printf("[EVENT] Temizlik yapildi. Kalan olay: %d\n", _eventCount);
  }
}

void eventSave() {
  size_t docSize = 256 + (size_t)_eventCount * 192;
  DynamicJsonDocument doc(docSize);

  doc["nextId"] = _nextId;
  JsonArray arr = doc.createNestedArray("events");

  for (int i = 0; i < _eventCount; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["id"] = _events[i].id;
    obj["uid"] = _events[i].uid;
    obj["name"] = _events[i].name;
    obj["type"] = (uint8_t)_events[i].type;
    obj["ts"] = _events[i].timestamp;
    obj["date"] = _events[i].date;
    obj["synced"] = _events[i].synced;
  }

  String output;
  serializeJson(doc, output);
  storageWrite(PATH_EVENTS, output);
}

int eventTotalCount() {
  return _eventCount;
}
