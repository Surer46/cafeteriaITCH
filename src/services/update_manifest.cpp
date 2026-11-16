#include "app/services/update_manifest.hpp"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace app::services {

namespace {

// ---------------------------------------------------------------------------------------------
// JSON estricto y mínimo (objetos, arreglos, cadenas, enteros no negativos, true/false/null)
// ---------------------------------------------------------------------------------------------
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type{Type::Null};
    bool b{false};
    std::uint64_t n{0};
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json* find(const std::string& key) const {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : t_(text) {}

    bool parse(Json& out) {
        skip_ws();
        if (!value(out, 0)) return false;
        skip_ws();
        return pos_ == t_.size();  // nada de basura después
    }

private:
    void skip_ws() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\n' || t_[pos_] == '\r' || t_[pos_] == '\t')) ++pos_;
    }
    bool eat(char c) {
        if (pos_ < t_.size() && t_[pos_] == c) { ++pos_; return true; }
        return false;
    }
    bool literal(const char* word) {
        std::size_t i = 0;
        while (word[i]) {
            if (pos_ + i >= t_.size() || t_[pos_ + i] != word[i]) return false;
            ++i;
        }
        pos_ += i;
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > 8 || pos_ >= t_.size()) return false;
        const char c = t_[pos_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') { out.type = Json::Type::String; return string(out.s); }
        if (c == 't') { out.type = Json::Type::Bool; out.b = true; return literal("true"); }
        if (c == 'f') { out.type = Json::Type::Bool; out.b = false; return literal("false"); }
        if (c == 'n') { out.type = Json::Type::Null; return literal("null"); }
        if (c >= '0' && c <= '9') return number(out);
        return false;
    }

    bool number(Json& out) {
        // Solo enteros no negativos, sin ceros a la izquierda y de a lo sumo 15 dígitos.
        const std::size_t start = pos_;
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        const std::size_t len = pos_ - start;
        if (len == 0 || len > 15) return false;
        if (len > 1 && t_[start] == '0') return false;
        if (pos_ < t_.size() && (t_[pos_] == '.' || t_[pos_] == 'e' || t_[pos_] == 'E')) return false;
        out.type = Json::Type::Number;
        out.n = std::strtoull(t_.substr(start, len).c_str(), nullptr, 10);
        return true;
    }

    static void append_utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& v) {
        if (pos_ + 4 > t_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[pos_ + i];
            unsigned d;
            if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            v = (v << 4) | d;
        }
        pos_ += 4;
        return true;
    }

    bool string(std::string& out) {
        if (!eat('"')) return false;
        out.clear();
        while (pos_ < t_.size()) {
            const unsigned char c = static_cast<unsigned char>(t_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;  // control sin escapar
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos_ >= t_.size()) return false;
            const char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // par sustituto
                        if (!(pos_ + 1 < t_.size() && t_[pos_] == '\\' && t_[pos_ + 1] == 'u')) return false;
                        pos_ += 2;
                        unsigned lo = 0;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    if (cp == 0) return false;
                    append_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;  // sin comilla de cierre
    }

    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        if (!eat('[')) return false;
        skip_ws();
        if (eat(']')) return true;
        while (true) {
            Json item;
            skip_ws();
            if (!value(item, depth + 1)) return false;
            out.arr.push_back(std::move(item));
            if (out.arr.size() > 64) return false;
            skip_ws();
            if (eat(',')) continue;
            return eat(']');
        }
    }

    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        if (!eat('{')) return false;
        skip_ws();
        if (eat('}')) return true;
        while (true) {
            skip_ws();
            std::string key;
            if (!string(key)) return false;
            for (const auto& kv : out.obj) if (kv.first == key) return false;  // clave repetida
            skip_ws();
            if (!eat(':')) return false;
            skip_ws();
            Json v;
            if (!value(v, depth + 1)) return false;
            out.obj.emplace_back(std::move(key), std::move(v));
            if (out.obj.size() > 32) return false;
            skip_ws();
            if (eat(',')) continue;
            return eat('}');
        }
    }

    const std::string& t_;
    std::size_t pos_{0};
};

bool is_lower_hex(const std::string& s, std::size_t len) {
    if (s.size() != len) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool valid_date(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (std::size_t i = 0; i < 10; ++i) if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

ManifestResult fail(ManifestError e, std::string detail) {
    ManifestResult r;
    r.error = e;
    r.detail = std::move(detail);
    return r;
}

} // namespace

bool parse_update_version(const std::string& text, UpdateVersion& out) {
    int parts[3] = {0, 0, 0};
    std::size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t start = pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
        const std::size_t len = pos - start;
        if (len == 0 || len > 4) return false;
        if (len > 1 && text[start] == '0') return false;
        parts[i] = std::atoi(text.substr(start, len).c_str());
        if (i < 2) {
            if (pos >= text.size() || text[pos] != '.') return false;
            ++pos;
        }
    }
    if (pos != text.size()) return false;
    out.major = parts[0]; out.minor = parts[1]; out.patch = parts[2];
    return true;
}

int compare_update_versions(const UpdateVersion& a, const UpdateVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

bool decode_signature_file(const std::vector<std::uint8_t>& file_bytes, std::vector<std::uint8_t>& signature) {
    if (file_bytes.size() > kMaxSignatureFileBytes) return false;
    if (file_bytes.size() == 64) {
        // Crudo (64 bytes) o, por casualidad, 64 caracteres hexadecimales: no es una firma válida de todos modos.
        signature = file_bytes;
        return true;
    }
    std::string hex;
    for (std::uint8_t c : file_bytes) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        hex += static_cast<char>(c);
    }
    if (hex.size() != 128) return false;
    signature.clear();
    for (std::size_t i = 0; i < 64; ++i) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { signature.clear(); return false; }
        signature.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

ManifestResult verify_and_parse_manifest(
    const std::vector<std::uint8_t>& manifest_bytes,
    const std::vector<std::uint8_t>& signature_file_bytes,
    const std::vector<PublicKey64>& keys,
    const std::string& installed_version) {

    if (keys.empty()) return fail(ManifestError::NoKeys, "no hay claves públicas configuradas");
    if (manifest_bytes.empty() || manifest_bytes.size() > kMaxManifestBytes) return fail(ManifestError::TooLarge, "tamaño del manifiesto");

    // 1. La firma, sobre los bytes exactos recibidos. Nada se interpreta antes.
    std::vector<std::uint8_t> signature;
    if (!decode_signature_file(signature_file_bytes, signature)) return fail(ManifestError::BadSignature, "archivo de firma ilegible");
    if (!verify_signature_any(manifest_bytes, signature, keys)) return fail(ManifestError::BadSignature, "la firma no verifica");

    // 2. JSON estricto.
    const std::string text(manifest_bytes.begin(), manifest_bytes.end());
    Json root;
    if (!Parser(text).parse(root) || root.type != Json::Type::Object) return fail(ManifestError::BadJson, "JSON mal formado");

    // 3. Campos exactos: ni de más ni de menos.
    static const char* const kTop[] = {"schema", "product", "arch", "version", "published", "notes", "installer"};
    if (root.obj.size() != 7) return fail(ManifestError::BadFields, "número de campos");
    for (const char* k : kTop) if (!root.find(k)) return fail(ManifestError::BadFields, std::string("falta ") + k);

    const Json* schema = root.find("schema");
    const Json* product = root.find("product");
    const Json* arch = root.find("arch");
    const Json* version = root.find("version");
    const Json* published = root.find("published");
    const Json* notes = root.find("notes");
    const Json* installer = root.find("installer");
    if (schema->type != Json::Type::Number || schema->n != 1) return fail(ManifestError::BadFields, "schema");
    if (product->type != Json::Type::String || arch->type != Json::Type::String || version->type != Json::Type::String ||
        published->type != Json::Type::String || notes->type != Json::Type::Array || installer->type != Json::Type::Object) {
        return fail(ManifestError::BadFields, "tipos");
    }
    if (product->s != "CafeteriaITCH") return fail(ManifestError::WrongProduct, "producto");
    if (arch->s != "x64") return fail(ManifestError::WrongArch, "arquitectura");

    UpdateVersion new_version;
    if (!parse_update_version(version->s, new_version)) return fail(ManifestError::BadFields, "versión");
    if (!valid_date(published->s)) return fail(ManifestError::BadFields, "fecha");

    UpdateManifest m;
    m.version = version->s;
    m.published = published->s;
    if (notes->arr.empty() || notes->arr.size() > 20) return fail(ManifestError::BadFields, "número de notas");
    for (const auto& n : notes->arr) {
        if (n.type != Json::Type::String || n.s.empty() || n.s.size() > 400) return fail(ManifestError::BadFields, "nota");
        m.notes.push_back(n.s);
    }

    if (installer->obj.size() != 3) return fail(ManifestError::BadFields, "campos del instalador");
    const Json* name = installer->find("name");
    const Json* size = installer->find("size");
    const Json* sha = installer->find("sha256");
    if (!name || !size || !sha || name->type != Json::Type::String || size->type != Json::Type::Number ||
        sha->type != Json::Type::String) {
        return fail(ManifestError::BadFields, "instalador");
    }
    // El nombre está fijado por la versión: nada de rutas ni nombres arbitrarios.
    if (name->s != "CafeteriaITCH-" + m.version + "-instalador-win64.exe") return fail(ManifestError::BadFields, "nombre del instalador");
    if (size->n == 0 || size->n > kMaxInstallerBytes) return fail(ManifestError::BadFields, "tamaño del instalador");
    if (!is_lower_hex(sha->s, 64)) return fail(ManifestError::BadFields, "hash del instalador");
    m.installer_name = name->s;
    m.installer_size = size->n;
    m.installer_sha256 = sha->s;

    // 4. Nunca se baja de versión (ni se repite).
    UpdateVersion installed;
    if (!parse_update_version(installed_version, installed)) return fail(ManifestError::BadFields, "versión instalada");
    ManifestResult ok;
    ok.manifest = std::move(m);
    if (compare_update_versions(new_version, installed) <= 0) {
        ok.error = ManifestError::NotNewer;
        ok.detail = "versión menor o igual a la instalada";
    }
    return ok;
}

} // namespace app::services
