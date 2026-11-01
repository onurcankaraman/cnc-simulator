#include "adapters/protocols.hpp"
#include <cstring>
#include <fstream>
#include <mosquitto.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <snap7.h>
#include <stdexcept>
namespace cnc {
static std::string secret(const char *variable) {
  auto path = env(variable);
  std::ifstream f(path);
  std::string s;
  std::getline(f, s);
  return s;
}
static UA_NodeId node(const std::string &id) {
  return UA_NODEID_STRING(2, const_cast<char *>(id.c_str()));
}
static void good(UA_StatusCode rc) {
  if (rc != UA_STATUSCODE_GOOD)
    throw std::runtime_error(std::string("OPC UA: ") + UA_StatusCode_name(rc));
}
static UA_ByteString bytes(const std::string &f) {
  std::ifstream in(f, std::ios::binary);
  std::string b((std::istreambuf_iterator<char>(in)), {});
  if (b.empty())
    throw std::runtime_error("missing OPC certificate/key: " + f);
  UA_ByteString s;
  UA_ByteString_allocBuffer(&s, b.size());
  memcpy(s.data, b.data(), b.size());
  return s;
}
struct Protocols::Impl {
  S7Object s7 = 0;
  UA_Server *ua = nullptr;
  mosquitto *mq = nullptr;
  std::mutex lock;
  std::array<std::vector<uint8_t>, 4> db;
  std::atomic<bool> mqtt{false}, connected{false};
  std::atomic<uint64_t> rejects{0}, drops{0};
  MachineSnapshot latest;
  std::string topic = "plant/demo/cnc/CNC-01/";
  int last_state = -1;
  uint64_t last_tools = 0;
  static int S7API read(void *p, int, int op, PS7Tag t, void *dest) {
    auto &self = *static_cast<Impl *>(p);
    if (op == OperationWrite) {
      self.rejects++;
      return 1;
    }
    if (t->Area != S7AreaDB || t->DBNumber < 1 || t->DBNumber > 4 || t->Start < 0 || t->Size < 0)
      return 1;
    int width = t->WordLen == S7WLByte                                ? 1
                : t->WordLen == S7WLWord                              ? 2
                : (t->WordLen == S7WLDWord || t->WordLen == S7WLReal) ? 4
                                                                      : 0;
    if (!width)
      return 1;
    std::lock_guard<std::mutex> g(self.lock);
    auto &b = self.db[t->DBNumber - 1];
    size_t n = size_t(t->Size) * width;
    if (size_t(t->Start) > b.size() || n > b.size() - t->Start)
      return 1;
    memcpy(dest, b.data() + t->Start, n);
    return 0;
  }
  void send(std::string suffix, const Json &value, int qos, bool retained) {
    if (!mqtt) {
      if (suffix == "events")
        drops++;
      return;
    }
    auto s = value.dump();
    int rc =
        mosquitto_publish(mq, nullptr, (topic + suffix).c_str(), s.size(), s.data(), qos, retained);
    if (rc != MOSQ_ERR_SUCCESS)
      drops++;
  }
  void variable(const std::string &id, const std::string &parent, const std::string &label,
                const UA_DataType *type) {
    UA_VariableAttributes a = UA_VariableAttributes_default;
    a.displayName =
        UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(label.c_str()));
    a.dataType = type->typeId;
    a.accessLevel = UA_ACCESSLEVELMASK_READ;
    a.userAccessLevel = UA_ACCESSLEVELMASK_READ;
    good(UA_Server_addVariableNode(
        ua, node(id), node(parent), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(2, const_cast<char *>(label.c_str())),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), a, nullptr, nullptr));
  }
  void object(const std::string &id, const std::string &parent, const std::string &label) {
    UA_ObjectAttributes a = UA_ObjectAttributes_default;
    a.displayName =
        UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(label.c_str()));
    auto par = parent.empty() ? UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER) : node(parent);
    good(UA_Server_addObjectNode(ua, node(id), par, UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                 UA_QUALIFIEDNAME(2, const_cast<char *>(label.c_str())),
                                 UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), a, nullptr,
                                 nullptr));
  }
  const UA_DataType *type(Kind k) {
    switch (k) {
    case Kind::U8:
      return &UA_TYPES[UA_TYPES_BYTE];
    case Kind::UINT16:
      return &UA_TYPES[UA_TYPES_UINT16];
    case Kind::UINT32:
      return &UA_TYPES[UA_TYPES_UINT32];
    case Kind::REAL32:
      return &UA_TYPES[UA_TYPES_FLOAT];
    default:
      return &UA_TYPES[UA_TYPES_STRING];
    }
  }
  void update(const MachineSnapshot &s) {
    for (auto &t : tags) {
      UA_DataValue v;
      UA_DataValue_init(&v);
      v.hasValue = true;
      v.hasSourceTimestamp = true;
      v.sourceTimestamp = UA_DATETIME_UNIX_EPOCH + s.ts_ms * 10000;
      v.hasStatus = true;
      v.status = int(s.data["data_quality_code"]) && std::string(t.group) == "Live"
                     ? UA_STATUSCODE_UNCERTAINSUBNORMAL
                     : UA_STATUSCODE_GOOD;
      auto &j = s.data[t.key];
      uint32_t u32 = 0;
      uint16_t u16 = 0;
      uint8_t u8 = 0;
      float f = 0;
      std::string str;
      UA_String us;
      if (t.kind == Kind::REAL32) {
        f = j.get<float>();
        UA_Variant_setScalar(&v.value, &f, type(t.kind));
      } else if (t.kind == Kind::U8) {
        u8 = j.get<uint8_t>();
        UA_Variant_setScalar(&v.value, &u8, type(t.kind));
      } else if (t.kind == Kind::UINT16) {
        u16 = j.get<uint16_t>();
        UA_Variant_setScalar(&v.value, &u16, type(t.kind));
      } else if (t.kind == Kind::UINT32) {
        u32 = j.get<uint32_t>();
        UA_Variant_setScalar(&v.value, &u32, type(t.kind));
      } else {
        str = j.get<std::string>();
        us = UA_STRING(const_cast<char *>(str.c_str()));
        UA_Variant_setScalar(&v.value, &us, type(t.kind));
      }
      good(UA_Server_writeDataValue(ua, node("Machine.CNC-01." + std::string(t.key)), v));
    }
  }
};
Protocols::Protocols(const MachineSnapshot &initial) : p_(std::make_unique<Impl>()) {
  auto &p = *p_;
  p.latest = initial;
  p.db = encode_s7(initial);
  p.s7 = Srv_Create();
  uint16_t port = 1102;
  int max = 8;
  Srv_SetParam(p.s7, p_u16_LocalPort, &port);
  Srv_SetParam(p.s7, p_i32_MaxClients, &max);
  Srv_SetRWAreaCallback(p.s7, Impl::read, &p);
  Srv_SetMask(p.s7, mkEvent, 0);
  if (Srv_StartTo(p.s7, env("LISTEN_ADDRESS", "0.0.0.0").c_str()))
    throw std::runtime_error("S7 listen failed");
  UA_ServerConfig conf{};
  auto c = &conf;
  bool insecure = env("ALLOW_INSECURE_OPCUA", "false") == "true";
  if (insecure)
    good(UA_ServerConfig_setMinimal(c, 4840, nullptr));
  else {
    auto cert = bytes(env("OPCUA_CERT", "/run/secrets/server.der")),
         key = bytes(env("OPCUA_KEY", "/run/secrets/server-key.der"));
    std::vector<UA_ByteString> trust;
    auto client = env("OPCUA_CLIENT_CERT");
    if (!client.empty())
      trust.push_back(bytes(client));
    good(UA_ServerConfig_setDefaultWithSecurityPolicies(c, 4840, &cert, &key, trust.data(),
                                                        trust.size(), nullptr, 0, nullptr, 0));
    c->securityPolicyNoneDiscoveryOnly = true;
    UA_ByteString_clear(&cert);
    UA_ByteString_clear(&key);
    for (auto &b : trust)
      UA_ByteString_clear(&b);
  }
  UA_String_clear(&c->applicationDescription.applicationUri);
  c->applicationDescription.applicationUri = UA_STRING_ALLOC("urn:digitheta:cnc-simulator:server");
  UA_LocalizedText_clear(&c->applicationDescription.applicationName);
  c->applicationDescription.applicationName =
      UA_LOCALIZEDTEXT_ALLOC("en-US", "Synthetic CNC Simulator");
  c->maxSessions = 10;
  c->maxSubscriptions = 10;
  c->maxSubscriptionsPerSession = 3;
  c->maxMonitoredItems = 2000;
  c->maxMonitoredItemsPerSubscription = 200;
  c->tcpMaxMsgSize = 1024 * 1024;
  c->tcpMaxChunks = 16;
  p.ua = UA_Server_newWithConfig(c);
  if (!p.ua)
    throw std::runtime_error("UA allocation");
  auto ns = UA_Server_addNamespace(p.ua, "urn:digitheta:cnc-simulator:machine:v1");
  if (ns != 2)
    throw std::runtime_error("OPC namespace must be 2");
  p.object("Machine.CNC-01", "", "CNC-01");
  for (auto g : {"Live", "Production", "Tools", "Alarms", "Metadata"})
    p.object("Machine.CNC-01/" + std::string(g), "Machine.CNC-01", g);
  for (int i = 1; i <= 12; i++) {
    char name[20];
    snprintf(name, sizeof(name), "Tool%02d", i);
    p.object("Machine.CNC-01/Tools/" + std::string(name), "Machine.CNC-01/Tools", name);
  }
  for (auto &t : tags) {
    std::string parent = "Machine.CNC-01/" + std::string(t.group);
    std::string key = t.key;
    if (key.rfind("tool_", 0) == 0 && key.size() > 8 && isdigit(key[5]) && isdigit(key[6]))
      parent += "/Tool" + key.substr(5, 2);
    p.variable("Machine.CNC-01." + key, parent, key, p.type(t.kind));
  }
  p.variable("Machine.CNC-01.Metadata.DataOrigin", "Machine.CNC-01/Metadata", "DataOrigin",
             &UA_TYPES[UA_TYPES_STRING]);
  UA_String origin = UA_STRING(const_cast<char *>("Synthetic"));
  UA_Variant v;
  UA_Variant_init(&v);
  UA_Variant_setScalar(&v, &origin, &UA_TYPES[UA_TYPES_STRING]);
  good(UA_Server_writeValue(p.ua, node("Machine.CNC-01.Metadata.DataOrigin"), v));
  p.update(initial);
  good(UA_Server_run_startup(p.ua));
  mosquitto_lib_init();
  p.mq = mosquitto_new("cnc-simulator", true, &p);
  if (!p.mq)
    throw std::runtime_error("MQTT allocation");
  auto password = secret("MQTT_PASSWORD_FILE");
  if (password.empty())
    throw std::runtime_error("MQTT password required");
  mosquitto_username_pw_set(p.mq, env("MQTT_USER", "simulator").c_str(), password.c_str());
  auto offline = envelope(initial, {{"online", false}}).dump();
  mosquitto_will_set(p.mq, (p.topic + "availability").c_str(), offline.size(), offline.data(), 1,
                     true);
  mosquitto_reconnect_delay_set(p.mq, 1, 60, true);
  mosquitto_max_inflight_messages_set(p.mq, 8);
  mosquitto_connect_callback_set(p.mq, [](mosquitto *, void *arg, int rc) {
    auto &x = *static_cast<Impl *>(arg);
    x.mqtt = rc == 0;
    x.connected = rc == 0;
  });
  mosquitto_disconnect_callback_set(
      p.mq, [](mosquitto *, void *arg, int) { static_cast<Impl *>(arg)->mqtt = false; });
  mosquitto_connect_async(p.mq, env("MQTT_HOST", "mqtt-broker").c_str(), 1883, 20);
  mosquitto_loop_start(p.mq);
}
Protocols::~Protocols() {
  if (!p_)
    return;
  auto &p = *p_;
  p.send("availability", envelope(p.latest, {{"online", false}}), 1, true);
  if (p.mq) {
    mosquitto_disconnect(p.mq);
    mosquitto_loop_stop(p.mq, true);
    mosquitto_destroy(p.mq);
    mosquitto_lib_cleanup();
  }
  if (p.ua) {
    UA_Server_run_shutdown(p.ua);
    UA_Server_delete(p.ua);
  }
  if (p.s7) {
    Srv_Stop(p.s7);
    Srv_Destroy(&p.s7);
  }
}
void Protocols::publish(const MachineSnapshot &s, const Json &events) {
  auto &p = *p_;
  p.latest = s;
  auto bytes = encode_s7(s);
  {
    std::lock_guard<std::mutex> g(p.lock);
    p.db = std::move(bytes);
  }
  p.update(s);
  bool re = p.connected.exchange(false);
  p.send("telemetry", envelope(s, s.data), 0, false);
  if (re || int(s.data["state_code"]) != p.last_state || s.sequence % 30 == 0) {
    p.send("state", envelope(s, s.data), 1, true);
    p.last_state = s.data["state_code"];
  }
  if (re || s.sequence % 60 == 0 || !events.empty()) {
    p.send("tools", envelope(s, {{"tools", s.tools}}), 1, true);
    p.last_tools = s.sequence;
  }
  if (re || s.sequence % 10 == 0)
    p.send("availability", envelope(s, {{"online", true}}), 1, true);
  for (auto &e : events)
    p.send("events", envelope(s, e), 1, false);
}
void Protocols::pump() {
  UA_Server_run_iterate(p_->ua, false);
}
Json Protocols::health() const {
  return {{"s7", true},
          {"opcua", true},
          {"mqtt", p_->mqtt.load()},
          {"s7_write_rejections", p_->rejects.load()},
          {"mqtt_dropped", p_->drops.load()}};
}
} // namespace cnc
