#include <s2e/ConfigFile.h>
#include <iomanip>

namespace s2e {
// Fixed 2 number Decimal
struct F2Dec {
  double value;
  int width;

  F2Dec(double _value, int _width = 2) : value(_value), width(_width) {}
  F2Dec(void* _value, int _width = 2)
      : value((uint64_t)_value), width(_width) {}
};

inline llvm::raw_ostream& operator<<(llvm::raw_ostream& out, const F2Dec& d) {
  int intpart = static_cast<int>(d.value);
  int decimalpart = static_cast<int>((d.value - intpart) * 100);
  out << intpart << "." << (decimalpart < 10 ? "0" : "") << decimalpart;
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const F2Dec& d) {
  out << std::fixed << std::setprecision(2) << d.value << std::defaultfloat;
  return out;
}

// IVA for TraceReplay
inline bool IVAgetBool(ConfigFile* cfg, const std::string& name,
                       bool* ok = nullptr, bool def = false) {
  return cfg->getBool(name, def, ok);
}
inline int64_t IVAgetInt(ConfigFile* cfg, const std::string& name,
                         bool* ok = nullptr, int64_t def = 0) {
  return cfg->getInt(name, def, ok);
}
inline double IVAgetDouble(ConfigFile* cfg, const std::string& name,
                           bool* ok = nullptr, double def = 0) {
  return cfg->getDouble(name, def, ok);
}
inline std::string IVAgetString(ConfigFile* cfg, const std::string& name,
                                bool* ok = nullptr,
                                const std::string& def = std::string()) {
  return cfg->getString(name, def, ok);
}

typedef std::vector<std::string> string_list;
inline string_list IVAgetStringList(ConfigFile* cfg, const std::string& name,
                                    bool* ok = nullptr,
                                    const string_list& def = string_list()) {
  return cfg->getStringList(name, def, ok);
}

typedef std::vector<uint64_t> integer_list;
inline integer_list IVAgetIntegerList(
    ConfigFile* cfg, const std::string& name, bool* ok = nullptr,
    const integer_list& def = integer_list()) {
  return cfg->getIntegerList(name, def, ok);
}

// init and check config when init
// val: the value to store config
// str: name in config file
// type: val type
#define config_init(val, str, type)                                         \
  val = IVAget##type(cfg, getConfigKey() + "." + #str, &ok);                \
  if (!ok) {                                                                \
    getWarningsStream() << "failed to load " << #type << " config " << #str \
                        << " to " << #val << "\n";                          \
    return;                                                                 \
  }
}  // namespace s2e
