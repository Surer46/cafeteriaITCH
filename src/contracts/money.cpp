#include "app/contracts/money.hpp"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <vector>
#include <limits>
#include <cctype>

namespace app::contracts {

std::string Money::to_string() const {
    bool negative = cents_ < 0;
    std::uint64_t abs_cents = 0;
    if (negative) {
        if (cents_ == std::numeric_limits<std::int64_t>::min()) {
            abs_cents = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ULL;
        } else {
            abs_cents = static_cast<std::uint64_t>(-cents_);
        }
    } else {
        abs_cents = static_cast<std::uint64_t>(cents_);
    }

    std::uint64_t pesos = abs_cents / 100;
    std::uint64_t rem_cents = abs_cents % 100;

    std::string pesos_str = std::to_string(pesos);
    std::string with_commas;
    int count = 0;
    for (int i = static_cast<int>(pesos_str.length()) - 1; i >= 0; --i) {
        with_commas.push_back(pesos_str[i]);
        count++;
        if (count % 3 == 0 && i > 0) {
            with_commas.push_back(',');
        }
    }
    std::reverse(with_commas.begin(), with_commas.end());

    std::ostringstream oss;
    if (negative) {
        oss << "-";
    }
    oss << "$" << with_commas << "."
        << std::setw(2) << std::setfill('0') << rem_cents;

    return oss.str();
}

Result<Money> Money::try_parse(const std::string& str, bool allow_negative) {
    if (str.empty()) {
        return Result<Money>::fail(ErrorCode::InvalidInput, "El importe no puede estar vacío");
    }

    // 1. Eliminar espacios iniciales y finales
    size_t start = 0;
    while (start < str.length() && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++;
    }
    if (start >= str.length()) {
        return Result<Money>::fail(ErrorCode::InvalidInput, "El importe no puede consistir únicamente de espacios");
    }

    size_t end = str.length();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        end--;
    }

    // 2. Signo opcional
    bool is_neg = false;
    if (str[start] == '-') {
        if (!allow_negative) {
            return Result<Money>::fail(ErrorCode::InvalidInput, "No se permiten importes monetarios negativos");
        }
        is_neg = true;
        start++;
    } else if (str[start] == '+') {
        start++;
    }

    // Espacios opcionales entre signo y símbolo/dígitos
    while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++;
    }

    // 3. Símbolo de moneda '$' opcional
    if (start < end && str[start] == '$') {
        start++;
        while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
            start++;
        }
    }

    if (start >= end) {
        return Result<Money>::fail(ErrorCode::InvalidInput, "No se encontraron dígitos en el importe");
    }

    // 4. Separar parte entera y parte fraccionaria decimal por '.'
    // Reglas de formato monetario admitido:
    // - El punto '.' es el único separador decimal admisible (1 o 2 dígitos, ej. .50).
    // - La coma ',' es el separador de miles opcional. Si se utiliza, debe delimitar
    //   bloques estrictos de 3 dígitos (ej. 1,000.00). Separadores ambiguos como '1,5' o '1,50'
    //   son estrictamente rechazados para evitar interpretaciones erróneas.
    std::string int_part;
    std::string frac_part;
    bool in_fraction = false;

    for (size_t i = start; i < end; ++i) {
        char ch = str[i];
        if (ch == '.') {
            if (in_fraction) {
                return Result<Money>::fail(ErrorCode::InvalidInput, "Formato inválido: múltiples puntos decimales");
            }
            in_fraction = true;
        } else if (in_fraction) {
            frac_part.push_back(ch);
        } else {
            int_part.push_back(ch);
        }
    }

    std::string cents_str;
    if (in_fraction) {
        if (frac_part.empty()) {
            return Result<Money>::fail(ErrorCode::InvalidInput, "Formato inválido: punto decimal sin dígitos");
        }
        if (frac_part.length() > 2) {
            return Result<Money>::fail(ErrorCode::InvalidInput, "Precisión no admitida: el importe no puede tener más de 2 decimales");
        }
        for (char c : frac_part) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return Result<Money>::fail(ErrorCode::InvalidInput, "Formato inválido: caracteres no numéricos en la parte decimal");
            }
        }
        cents_str = frac_part;
        if (cents_str.length() == 1) {
            cents_str.push_back('0');
        }
    } else {
        cents_str = "00";
    }

    // Validar parte entera y agrupación de comas
    std::vector<std::string> chunks;
    std::string current_chunk;
    for (char c : int_part) {
        if (c == ',') {
            chunks.push_back(current_chunk);
            current_chunk.clear();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            current_chunk.push_back(c);
        } else {
            return Result<Money>::fail(ErrorCode::InvalidInput, "Formato inválido: contiene caracteres no permitidos");
        }
    }
    chunks.push_back(current_chunk);

    std::string pesos_str;
    if (chunks.size() == 1) {
        if (chunks[0].empty()) {
            if (in_fraction) {
                pesos_str = "0";
            } else {
                return Result<Money>::fail(ErrorCode::InvalidInput, "No se encontraron dígitos en el importe");
            }
        } else {
            pesos_str = chunks[0];
        }
    } else {
        // Se usaron comas separadoras de miles: validación estricta de agrupación
        if (chunks[0].empty() || chunks[0].length() > 3) {
            return Result<Money>::fail(
                ErrorCode::InvalidInput,
                "Formato inválido: separación de miles incorrecta antes de la primera coma (ej. 1,000.00)"
            );
        }
        for (size_t k = 1; k < chunks.size(); ++k) {
            if (chunks[k].length() != 3) {
                return Result<Money>::fail(
                    ErrorCode::InvalidInput,
                    "Formato inválido: los separadores de miles deben agrupar exactamente 3 dígitos (ej. 1,000.00; separadores ambiguos como 1,5 rechazados)"
                );
            }
        }
        for (const auto& chk : chunks) {
            pesos_str += chk;
        }
    }

    // 5. Control estricto de límites numéricos y desbordamientos (compatible con INT64_MIN y INT64_MAX)
    std::uint64_t max_cents = is_neg
        ? (static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ULL)
        : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

    std::uint64_t pesos = 0;
    for (char d : pesos_str) {
        int val = d - '0';
        if (pesos > (max_cents / 100ULL - val) / 10ULL) {
            return Result<Money>::fail(ErrorCode::InvalidInput, "Desbordamiento numérico: el importe excede el límite permitido");
        }
        pesos = pesos * 10ULL + static_cast<std::uint64_t>(val);
    }

    std::uint64_t total_cents = pesos * 100ULL;
    std::uint64_t cents = static_cast<std::uint64_t>((cents_str[0] - '0') * 10 + (cents_str[1] - '0'));
    if (total_cents > max_cents - cents) {
        return Result<Money>::fail(ErrorCode::InvalidInput, "Desbordamiento numérico en centavos");
    }
    total_cents += cents;

    std::int64_t result_cents = 0;
    if (is_neg) {
        if (total_cents == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ULL) {
            result_cents = std::numeric_limits<std::int64_t>::min();
        } else {
            result_cents = -static_cast<std::int64_t>(total_cents);
        }
    } else {
        result_cents = static_cast<std::int64_t>(total_cents);
    }

    return Result<Money>::ok(Money::from_cents(result_cents));
}

Money Money::from_string(const std::string& str) {
    auto res = try_parse(str, true);
    return res.is_success() ? res.value() : Money::zero();
}

} // namespace app::contracts
