#include "event_logger.h"
#include "config.h"
#include "storage_manager.h"
#include "time_manager.h"
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

static LogEvent _events[MAX_PENDING_EVENTS];
static int _eventCount = 0;
static unsigned long _nextId = 1;

void eventInit() {
  memset(_events, 0, sizeof(_events));
  _eventCount = 0;
  _nextId = 1;

  if (!LittleFS.exists(PATH_EVENTS)) {
    Serial.println(F("[EVENT] events.json bulunamadi, sifirdan baslatildi."));
    return;
  }

  File f = LittleFS.open(PATH_EVENTS, "r");
  if (!f) {
    Serial.println(F("[EVENT] events.json acilamadi!"));
    return;
  }

  // Akis temelli satir satir okuma - Bellek (heap) patlamasini onler
  _eventCount = 0;
  StaticJsonDocument<256> itemDoc;

  while (f.available() && _eventCount < MAX_PENDING_EVENTS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // nextId satiri
    if (line.indexOf("\"nextId\"") >= 0) {
      int idx = line.indexOf(':');
      if (idx > 0) {
        String numPart = line.substring(idx + 1);
        numPart.replace(",", "");
        numPart.trim();
        _nextId = (unsigned long)numPart.toInt();
      }
      continue;
    }

    // Olay JSON nesnesi
    if (line.startsWith("{") && line.indexOf("\"uid\"") >= 0) {
      if (line.endsWith(",")) line.remove(line.length() - 1);
      itemDoc.clear();
      DeserializationError err = deserializeJson(itemDoc, line);
      if (!err) {
        _events[_eventCount].id = itemDoc["id"] | 0UL;
        const char* uid = itemDoc["uid"] | "";
        strncpy(_events[_eventCount].uid, uid, sizeof(_events[_eventCount].uid) - 1);
        _events[_eventCount].uid[sizeof(_events[_eventCount].uid) - 1] = '\0';

        const char* name = itemDoc["name"] | "";
        strncpy(_events[_eventCount].name, name, sizeof(_events[_eventCount].name) - 1);
        _events[_eventCount].name[sizeof(_events[_eventCount].name) - 1] = '\0';

        _events[_eventCount].type = (EventType)(itemDoc["type"] | 0);
        _events[_eventCount].timestamp = itemDoc["ts"] | 0UL;

        const char* date = itemDoc["date"] | "";
        strncpy(_events[_eventCount].date, date, sizeof(_events[_eventCount].date) - 1);
        _events[_eventCount].date[sizeof(_events[_eventCount].date) - 1] = '\0';

        _events[_eventCount].synced = itemDoc["synced"] | false;

        if (_events[_eventCount].id >= _nextId) {
          _nextId = _events[_eventCount].id + 1;
        }
        _eventCount++;
      }
    }
  }

  f.close();
  if (_nextId == 0) _nextId = 1;
  Serial.printf("[EVENT] Akis modunda %d olay yuklendi. Sonraki ID: %lu\n", _eventCount, _nextId);
}

void eventLog(const char* uid, const char* name, EventType type, unsigned long timestamp) {
  // 1. Kapasite dolduysa once 7 gunden eski senkronize kayitlari temizle
  if (_eventCount >= MAX_PENDING_EVENTS) {
    eventCleanup();
  }

  // 2. Hala doluysa once en eski senkronize olayi cikar
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
      // Yer kalmadi ve hic senkronize kayit yok: Puantaj kaybi olmamasi icin en eski kaydi arsiv dosyasina yedekle
      File arch = LittleFS.open(PATH_ARCHIVE, "a");
      bool archOk = false;
      if (arch) {
        char escArchName[64];
        jsonEscape(_events[0].name, escArchName, sizeof(escArchName));
        char escArchUid[48];
        jsonEscape(_events[0].uid, escArchUid, sizeof(escArchUid));
        size_t bytesWritten = arch.printf("{\"id\":%lu,\"uid\":\"%s\",\"name\":\"%s\",\"type\":%d,\"ts\":%lu,\"date\":\"%s\"}\n",
                    _events[0].id, escArchUid, escArchName, (int)_events[0].type, _events[0].timestamp, _events[0].date);
        arch.flush();
        arch.close();
        archOk = (bytesWritten > 0);
      }
      if (archOk) {
        Serial.printf("[EVENT] UYARI: Olay arabellegi doldu! Olay #%lu %s dosyasina yedeklendi.\n", _events[0].id, PATH_ARCHIVE);
        for (int i = 0; i < _eventCount - 1; i++) {
          _events[i] = _events[i + 1];
        }
        _eventCount--;
      } else {
        Serial.println(F("[EVENT] KRITIK HATA: Arsiv dosyasina yazilamadi! Puantaj kaybi olmamasi icin eski kayit atilmadi."));
        return;
      }
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

  Serial.printf("[EVENT] Olay kaydedildi: %s - %s\n", ev.name, eventTypeToStr(type));
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
  eventMarkSyncedNoSave(id);
  eventSave();
}

void eventMarkSyncedNoSave(unsigned long id) {
  for (int i = 0; i < _eventCount; i++) {
    if (_events[i].id == id) {
      _events[i].synced = true;
      return;
    }
  }
}

void eventCleanup() {
  unsigned long now = timeNow();
  const unsigned long SEVEN_DAYS = 7UL * 24UL * 3600UL;

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
  // ATOMIK AKIS (STREAMING) YAZMA:
  // RAM'de devasa 96KB JSON tahsis etmez, doğrudan dosyaya stream olarak yazar!
  String tmpPath = String(PATH_EVENTS) + ".tmp";
  File f = LittleFS.open(tmpPath.c_str(), "w");
  if (!f) {
    Serial.println(F("[EVENT] HATA: Gecici dosya olusturulamadi!"));
    return;
  }

  f.print("{\"nextId\":");
  f.print(_nextId);
  f.print(",\"events\":[\n");

  char lineBuf[384];
  char escName[64];
  char escUid[48];
  for (int i = 0; i < _eventCount; i++) {
    jsonEscape(_events[i].name, escName, sizeof(escName));
    jsonEscape(_events[i].uid, escUid, sizeof(escUid));
    snprintf(lineBuf, sizeof(lineBuf),
             "{\"id\":%lu,\"uid\":\"%s\",\"name\":\"%s\",\"type\":%d,\"ts\":%lu,\"date\":\"%s\",\"synced\":%s}%s\n",
             _events[i].id,
             escUid,
             escName,
             (int)_events[i].type,
             _events[i].timestamp,
             _events[i].date,
             _events[i].synced ? "true" : "false",
             (i < _eventCount - 1) ? "," : "");
    f.print(lineBuf);
  }

  f.print("]}\n");
  f.flush();
  f.close();

  // ATOMIK YENIDEN ADLANDIRMA:
  // LittleFS lfs_rename hedef dosyayi atomik olarak degistirir.
  // Eski dosyayi onceden silmeyiz boylece guc kesintisinde olay kayitlari kaybolmaz.
  if (!LittleFS.rename(tmpPath.c_str(), PATH_EVENTS)) {
    String bakPath = String(PATH_EVENTS) + ".bak";
    LittleFS.remove(bakPath.c_str());
    LittleFS.rename(PATH_EVENTS, bakPath.c_str());
    if (!LittleFS.rename(tmpPath.c_str(), PATH_EVENTS)) {
      Serial.println(F("[EVENT] HATA: events.json atomik yeniden adlandirilamadi!"));
      LittleFS.rename(bakPath.c_str(), PATH_EVENTS);
      LittleFS.remove(tmpPath.c_str());
      return;
    }
    LittleFS.remove(bakPath.c_str());
  }
}

int eventTotalCount() {
  return _eventCount;
}
