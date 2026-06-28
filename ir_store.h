#pragma once
#include <map>
#include <string>
#include <vector>
#include <FS.h>

namespace ir_store {
  struct Slot {
    int id;
    std::string protocol;
    std::string code;
    std::string name;
    std::string raw_json;
    int carrier_hz;
    bool active;
  };

  static std::map<std::string, std::string> keymap;
  static std::vector<Slot> slots;
  static int next_slot_id = 1;

  static const char* KEYMAP_FILE = "/keymap.txt";
  static const char* SLOTS_FILE = "/slots.txt";
  static bool fs_ready = false;

  std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n'\"");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n'\"");
    return s.substr(a, b - a + 1);
  }

  std::string uint64_to_bin(uint64_t v) {
    if (v == 0) return "0";
    std::string o;
    bool st = false;
    for (int i = 63; i >= 0; i--) {
      bool b = (v >> i) & 1ULL;
      if (b) st = true;
      if (st) o += b ? '1' : '0';
    }
    return o;
  }

  bool begin_fs() {
    if (fs_ready) return true;
    fs_ready = SPIFFS.begin();
    if (!fs_ready) {
      ESP_LOGW("irstore", "SPIFFS mount failed, formatting...");
      SPIFFS.format();
      fs_ready = SPIFFS.begin();
    }
    ESP_LOGI("irstore", "SPIFFS mount %s", fs_ready ? "OK" : "FAILED");
    return fs_ready;
  }

  // ===== Keymap =====
  void keymap_load_blob(const std::string& blob) {
    keymap.clear();
    size_t p = 0;
    while (p < blob.size()) {
      size_t e = blob.find('\n', p);
      if (e == std::string::npos) e = blob.size();
      std::string line = trim(blob.substr(p, e - p));
      if (!line.empty()) {
        size_t sep = line.find('=');
        if (sep != std::string::npos && sep > 0) {
          std::string k = trim(line.substr(0, sep));
          std::string v = trim(line.substr(sep + 1));
          if (!k.empty() && !v.empty()) keymap[k] = v;
        }
      }
      p = e + 1;
    }
  }

  std::string keymap_to_blob() {
    std::string o;
    for (auto& kv : keymap) o += kv.first + "=" + kv.second + "\n";
    return o;
  }

  bool keymap_load_fs() {
    keymap.clear();
    if (!begin_fs()) return false;
    if (!SPIFFS.exists(KEYMAP_FILE)) return true;
    File f = SPIFFS.open(KEYMAP_FILE, "r");
    if (!f) return false;
    std::string d;
    while (f.available()) d += (char)f.read();
    f.close();
    keymap_load_blob(d);
    ESP_LOGI("irstore", "Keymap loaded: %u entries", (unsigned)keymap.size());
    return true;
  }

  bool keymap_save_fs() {
    if (!begin_fs()) return false;
    std::string d = keymap_to_blob();
    yield();
    File f = SPIFFS.open(KEYMAP_FILE, "w");
    if (!f) return false;
    size_t w = f.print(d.c_str());
    f.flush(); f.close();
    yield();
    return w == d.size();
  }

  std::string keymap_get(const std::string& raw) {
    std::string c = trim(raw);
    auto it = keymap.find(c);
    if (it != keymap.end()) return it->second;
    const std::string pre = "RCSWITCH:";
    if (c.rfind(pre, 0) == 0) {
      size_t pos = c.find(':', pre.size());
      if (pos != std::string::npos) {
        it = keymap.find(c.substr(pos + 1));
        if (it != keymap.end()) return it->second;
      }
    }
    return "";
  }

  bool keymap_set(const std::string& raw_k, const std::string& raw_v) {
    std::string k = trim(raw_k), v = trim(raw_v);
    if (k.empty() || v.empty()) return false;
    keymap[k] = v;
    return keymap_save_fs();
  }

  bool keymap_remove(const std::string& raw_k) {
    std::string k = trim(raw_k);
    auto it = keymap.find(k);
    if (it == keymap.end()) return false;
    keymap.erase(it);
    return keymap_save_fs();
  }

  size_t keymap_size() { return keymap.size(); }

  std::string keymap_summary(size_t max_items = 8) {
    if (keymap.empty()) return "（无映射）";
    std::string o;
    size_t i = 0;
    for (auto& kv : keymap) {
      if (i >= max_items) break;
      if (!o.empty()) o += " | ";
      o += kv.first + "→" + kv.second;
      i++;
    }
    if (keymap.size() > max_items) o += " | ...共" + std::to_string(keymap.size()) + "条";
    return o;
  }

  void keymap_log_all() {
    ESP_LOGI("irstore", "Keymap: %u entries", (unsigned)keymap.size());
    for (auto& kv : keymap) ESP_LOGI("irstore", "  %s -> %s", kv.first.c_str(), kv.second.c_str());
  }

  std::string keymap_find_by_value(const std::string& v) {
    std::string val = trim(v);
    for (auto& kv : keymap) {
      if (kv.second == val) return kv.first;
    }
    return "";
  }

  std::string keymap_to_json() {
    std::string j = "[";
    size_t i = 0;
    for (auto& kv : keymap) {
      if (i > 0) j += ",";
      j += "{\"ir\":\"" + kv.first + "\",\"val\":\"" + kv.second + "\"}";
      i++;
    }
    j += "]";
    return j;
  }

  // ===== Dynamic Slots =====
  void slots_init() {
    slots.clear();
    next_slot_id = 1;
  }

  std::string slot_to_line(const Slot& s);
  bool slots_save_fs();

  Slot* find_slot(int id) {
    for (auto& s : slots) if (s.id == id) return &s;
    return nullptr;
  }

  int add_slot(const std::string& proto, const std::string& code,
               const std::string& name, const std::string& raw, int carrier) {
    Slot s;
    s.id = next_slot_id++;
    s.protocol = proto;
    s.code = code;
    s.name = name.empty() ? (proto + "_" + code) : name;
    s.raw_json = raw;
    s.carrier_hz = carrier;
    s.active = true;
    slots.push_back(s);
    slots_save_fs();
    return s.id;
  }

  bool delete_slot(int id) {
    for (auto it = slots.begin(); it != slots.end(); ++it) {
      if (it->id == id) { slots.erase(it); slots_save_fs(); return true; }
    }
    return false;
  }

  bool rename_slot(int id, const std::string& new_name) {
    Slot* s = find_slot(id);
    if (!s) return false;
    s->name = new_name.empty() ? s->code : new_name;
    return slots_save_fs();
  }

  Slot* find_slot_by_name(const std::string& name) {
    for (auto& s : slots) if (s.active && s.name == name) return &s;
    return nullptr;
  }

  bool delete_slot_by_name(const std::string& name) {
    for (auto it = slots.begin(); it != slots.end(); ++it) {
      if (it->active && it->name == name) { slots.erase(it); slots_save_fs(); return true; }
    }
    return false;
  }

  std::string slot_to_line(const Slot& s) {
    return std::to_string(s.id) + "|" + s.protocol + "|" + s.code + "|" + s.name + "|" + std::to_string(s.carrier_hz) + "|" + s.raw_json;
  }

  bool slot_from_line(const std::string& line, Slot& s) {
    std::vector<std::string> p;
    std::string cur;
    for (char c : line) {
      if (c == '|') { p.push_back(cur); cur.clear(); }
      else cur += c;
    }
    p.push_back(cur);
    if (p.size() < 6) return false;
    s.id = atoi(p[0].c_str());
    s.protocol = p[1];
    s.code = p[2];
    s.name = p[3];
    s.carrier_hz = atoi(p[4].c_str());
    s.raw_json = p.size() > 5 ? p[5] : "";
    s.active = (s.protocol != "NONE" && !s.code.empty());
    return true;
  }

  bool slots_load_fs() {
    slots_init();
    if (!begin_fs()) return false;
    if (!SPIFFS.exists(SLOTS_FILE)) return true;
    File f = SPIFFS.open(SLOTS_FILE, "r");
    if (!f) return false;
    std::string d;
    while (f.available()) d += (char)f.read();
    f.close();
    int max_id = 0;
    size_t p = 0;
    while (p < d.size()) {
      size_t e = d.find('\n', p);
      if (e == std::string::npos) e = d.size();
      std::string line = trim(d.substr(p, e - p));
      if (!line.empty()) {
        Slot s;
        if (slot_from_line(line, s) && s.active) {
          slots.push_back(s);
          if (s.id > max_id) max_id = s.id;
        }
      }
      p = e + 1;
    }
    next_slot_id = max_id + 1;
    ESP_LOGI("irstore", "Slots loaded: %u active, next_id=%d", (unsigned)slots.size(), next_slot_id);
    return true;
  }

  bool slots_save_fs() {
    if (!begin_fs()) return false;
    std::string d;
    for (auto& s : slots) if (s.active) d += slot_to_line(s) + "\n";
    yield();
    File f = SPIFFS.open(SLOTS_FILE, "w");
    if (!f) return false;
    size_t w = f.print(d.c_str());
    f.flush(); f.close();
    yield();
    ESP_LOGI("irstore", "Slots saved: %u bytes, %u slots", (unsigned)w, (unsigned)slots.size());
    return w == d.size();
  }

  int slots_count() { return (int)slots.size(); }

  std::vector<Slot*> get_active_slots() {
    std::vector<Slot*> v;
    for (auto& s : slots) if (s.active) v.push_back(&s);
    return v;
  }

  std::string slot_summary(int id) {
    Slot* s = find_slot(id);
    if (!s || !s->active) return "（未绑定）";
    return s->name + " [" + s->protocol + "]";
  }

  std::vector<int> parse_raw_json(const std::string& json) {
    std::vector<int> v;
    size_t a = json.find('[');
    size_t b = json.find(']');
    if (a == std::string::npos || b == std::string::npos || b <= a) return v;
    size_t i = a + 1;
    std::string n;
    while (i < b) {
      char c = json[i];
      if (c == ',' || c == ' ') {
        if (!n.empty()) { v.push_back(atoi(n.c_str())); n.clear(); }
      } else if (c == '-' || (c >= '0' && c <= '9')) n += c;
      i++;
    }
    if (!n.empty()) v.push_back(atoi(n.c_str()));
    return v;
  }

  std::string slots_to_json() {
    std::string j = "[";
    for (size_t i = 0; i < slots.size(); i++) {
      if (i > 0) j += ",";
      j += "{\"id\":" + std::to_string(slots[i].id) +
           ",\"name\":\"" + slots[i].name + "\"" +
           ",\"proto\":\"" + slots[i].protocol + "\"}";
    }
    j += "]";
    return j;
  }

  std::string find_slot_by_ir_code(const std::string& ir_code) {
    if (ir_code.rfind("NEC:", 0) == 0) {
      size_t p1 = ir_code.find(':', 4);
      if (p1 != std::string::npos) {
        std::string hex_addr = ir_code.substr(4, p1-4);
        std::string hex_cmd = ir_code.substr(p1+1);
        uint16_t addr = (uint16_t)strtol(hex_addr.c_str(), nullptr, 16);
        uint16_t cmd = (uint16_t)strtol(hex_cmd.c_str(), nullptr, 16);
        std::string slot_code = std::to_string(addr) + ":" + std::to_string(cmd);
        for (auto& s : slots) {
          if (s.active && s.code == slot_code) return s.name;
        }
      }
    }
    if (ir_code.rfind("RCSWITCH:", 0) == 0) {
      size_t p = ir_code.find(':', 9);
      if (p != std::string::npos) {
        std::string code = ir_code.substr(p+1);
        for (auto& s : slots) {
          if (s.active && s.code == code) return s.name;
        }
      }
    }
    return "";
  }

  void init() {
    begin_fs();
    keymap_load_fs();
    slots_load_fs();
  }
}
