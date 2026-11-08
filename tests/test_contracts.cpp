#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <random>
#include "app/contracts/money.hpp"
#include "app/contracts/result.hpp"
#include "app/contracts/dto.hpp"
#include "app/contracts/sale_service.hpp"
#include "app/domain/unit_converter.hpp"
#include "app/crypto/password_hasher.hpp"

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace app::contracts;
using namespace app::domain;
using namespace app::crypto;

void test_money_basics() {
    std::cout << "[TEST] Money: inicialización y formato..." << std::endl;

    Money m0 = Money::zero();
    TEST_CHECK(m0.cents() == 0, "m0 debe tener 0 centavos");
    TEST_CHECK(m0.to_string() == "$0.00", "Formato de $0.00 incorrecto");

    Money m1 = Money::from_cents(293000);
    TEST_CHECK(m1.cents() == 293000, "m1 debe tener 293000 centavos");
    TEST_CHECK(m1.to_string() == "$2,930.00", "Formato con coma de $2,930.00 incorrecto");

    Money m2 = Money::from_major_minor(50, 0);
    TEST_CHECK(m2.cents() == 5000, "m2 debe ser 5000 centavos");
    TEST_CHECK(m2.to_string() == "$50.00", "Formato de $50.00 incorrecto");

    Money m3 = Money::from_cents(5);
    TEST_CHECK(m3.to_string() == "$0.05", "Formato de 5 centavos debe ser $0.05");

    Money m4 = Money::from_cents(-1550);
    TEST_CHECK(m4.to_string() == "-$15.50", "Formato de importe negativo incorrecto");
}

void test_money_arithmetic() {
    std::cout << "[TEST] Money: aritmética y comparaciones..." << std::endl;

    Money a = Money::from_major_minor(30, 0);
    Money b = Money::from_major_minor(25, 0);

    Money sum = a + b;
    TEST_CHECK(sum.cents() == 5500, "Suma de 30 + 25 debe ser 55");

    Money diff = a - b;
    TEST_CHECK(diff.cents() == 500, "Resta de 30 - 25 debe ser 5");

    Money mult = b * 116;
    TEST_CHECK(mult.cents() == 290000, "Multiplicación 25 * 116 debe ser 2900.00");
    TEST_CHECK((a + mult).cents() == 293000, "30 + 116 * 25 = 2,930.00 (ejemplo maqueta)");

    TEST_CHECK(a > b, "30 debe ser mayor que 25");
    TEST_CHECK(b < a, "25 debe ser menor que 30");
    TEST_CHECK(a != b, "30 no es igual a 25");
    TEST_CHECK(a == Money::from_cents(3000), "30 debe ser igual a 3000 centavos");
}

void test_money_parsing() {
    std::cout << "[TEST] Money: parsing seguro y validación estricta desde cadenas..." << std::endl;

    // 1. Parsing válido tradicional
    Money parsed1 = Money::from_string("$2,930.00");
    TEST_CHECK(parsed1.cents() == 293000, "Parsing de $2,930.00 incorrecto");

    Money parsed2 = Money::from_string("50.00");
    TEST_CHECK(parsed2.cents() == 5000, "Parsing de 50.00 incorrecto");

    Money parsed3 = Money::from_string("$0.05");
    TEST_CHECK(parsed3.cents() == 5, "Parsing de $0.05 incorrecto");

    // 2. Money::try_parse con importes válidos
    auto res_valid1 = Money::try_parse("$2,930.00", false);
    TEST_CHECK(res_valid1.is_success() && res_valid1.value().cents() == 293000, "try_parse $2,930.00 debe ser 293000 centavos");

    auto res_valid2 = Money::try_parse("  $ 1,234.56  ", false);
    TEST_CHECK(res_valid2.is_success() && res_valid2.value().cents() == 123456, "try_parse con espacios debe ser 123456");

    auto res_valid3 = Money::try_parse("10.5", false);
    TEST_CHECK(res_valid3.is_success() && res_valid3.value().cents() == 1050, "try_parse 10.5 debe ser 1050 centavos");

    auto res_valid4 = Money::try_parse("100", false);
    TEST_CHECK(res_valid4.is_success() && res_valid4.value().cents() == 10000, "try_parse 100 debe ser 10000 centavos");

    // 3. Negativos: rechazados por defecto, admitidos con allow_negative=true
    auto res_neg_disallowed = Money::try_parse("-50.00", false);
    TEST_CHECK(!res_neg_disallowed.is_success(), "try_parse debe rechazar negativos cuando allow_negative=false");

    auto res_neg_allowed = Money::try_parse("-50.00", true);
    TEST_CHECK(res_neg_allowed.is_success() && res_neg_allowed.value().cents() == -5000, "try_parse debe admitir negativos cuando allow_negative=true");

    // 4. Importe válido seguido de texto inválido (debe rechazarse)
    auto res_invalid_text = Money::try_parse("200.00abc", false);
    TEST_CHECK(!res_invalid_text.is_success(), "try_parse debe rechazar '200.00abc'");

    auto res_trailing_symbol = Money::try_parse("100.00 $", false);
    TEST_CHECK(!res_trailing_symbol.is_success(), "try_parse debe rechazar '100.00 $'");

    auto res_letters = Money::try_parse("cien pesos", false);
    TEST_CHECK(!res_letters.is_success(), "try_parse debe rechazar texto");

    // 5. Precisión no admitida (> 2 decimales sin truncamiento silencioso)
    auto res_3_decimals = Money::try_parse("10.555", false);
    TEST_CHECK(!res_3_decimals.is_success(), "try_parse debe rechazar más de 2 decimales ('10.555')");

    auto res_small_decimal = Money::try_parse("0.001", false);
    TEST_CHECK(!res_small_decimal.is_success(), "try_parse debe rechazar '0.001'");

    // 6. Formatos malformados con punto decimal
    auto res_dot_no_digits = Money::try_parse("100.", false);
    TEST_CHECK(!res_dot_no_digits.is_success(), "try_parse debe rechazar punto sin dígitos ('100.')");

    auto res_multiple_dots = Money::try_parse("10.2.3", false);
    TEST_CHECK(!res_multiple_dots.is_success(), "try_parse debe rechazar múltiples puntos ('10.2.3')");

    auto res_only_dot = Money::try_parse(".", false);
    TEST_CHECK(!res_only_dot.is_success(), "try_parse debe rechazar '.' solo");

    // 7. Vacíos y espacios
    auto res_empty = Money::try_parse("", false);
    TEST_CHECK(!res_empty.is_success(), "try_parse debe rechazar cadena vacía");

    auto res_spaces = Money::try_parse("    ", false);
    TEST_CHECK(!res_spaces.is_success(), "try_parse debe rechazar solo espacios");

    auto res_symbol_only = Money::try_parse("$", false);
    TEST_CHECK(!res_symbol_only.is_success(), "try_parse debe rechazar '$' sin dígitos");

    // 8. Separadores ambiguos como '1,5' o '1,50': deben rechazarse estrictamente
    auto res_ambiguous_1_5 = Money::try_parse("1,5", false);
    TEST_CHECK(!res_ambiguous_1_5.is_success(), "try_parse debe rechazar '1,5' (separador ambiguo)");

    auto res_ambiguous_1_50 = Money::try_parse("1,50", false);
    TEST_CHECK(!res_ambiguous_1_50.is_success(), "try_parse debe rechazar '1,50' (agrupación incompleta)");

    auto res_ambiguous_1_2345 = Money::try_parse("1,2345", false);
    TEST_CHECK(!res_ambiguous_1_2345.is_success(), "try_parse debe rechazar '1,2345' (más de 3 dígitos tras coma)");

    auto res_leading_comma = Money::try_parse(",500", false);
    TEST_CHECK(!res_leading_comma.is_success(), "try_parse debe rechazar coma al inicio ',500'");

    auto res_double_comma = Money::try_parse("1,,000", false);
    TEST_CHECK(!res_double_comma.is_success(), "try_parse debe rechazar dobles comas '1,,000'");

    auto res_too_many_leading = Money::try_parse("1000,000", false);
    TEST_CHECK(!res_too_many_leading.is_success(), "try_parse debe rechazar más de 3 dígitos antes de la coma '1000,000'");

    // Agrupaciones de miles válidas
    auto res_valid_group1 = Money::try_parse("1,000.00", false);
    TEST_CHECK(res_valid_group1.is_success() && res_valid_group1.value().cents() == 100000, "try_parse '1,000.00' debe ser 100000 centavos");

    auto res_valid_group2 = Money::try_parse("1,234,567.89", false);
    TEST_CHECK(res_valid_group2.is_success() && res_valid_group2.value().cents() == 123456789, "try_parse '1,234,567.89' debe ser 123456789 centavos");

    auto res_plain_large = Money::try_parse("1234567.89", false);
    TEST_CHECK(res_plain_large.is_success() && res_plain_large.value().cents() == 123456789, "try_parse '1234567.89' debe ser 123456789 centavos");

    // 9. Control de desbordamientos aritméticos (sin excepciones no controladas)
    auto res_overflow = Money::try_parse("9999999999999999999999999999.00", false);
    TEST_CHECK(!res_overflow.is_success(), "try_parse debe rechazar desbordamiento numérico limpiamente");

    // 10. Representación y parsing seguro de límites extremos (INT64_MAX e INT64_MIN)
    Money max_money = Money::from_cents(std::numeric_limits<std::int64_t>::max());
    std::string max_str = max_money.to_string();
    auto parse_max = Money::try_parse(max_str, false);
    TEST_CHECK(parse_max.is_success() && parse_max.value().cents() == std::numeric_limits<std::int64_t>::max(),
               "Round-trip de INT64_MAX debe ser exacto sin desbordamiento");

    Money min_money = Money::from_cents(std::numeric_limits<std::int64_t>::min());
    std::string min_str = min_money.to_string();
    TEST_CHECK(min_str.find("-$92,233,720,368,547,758.08") != std::string::npos,
               "to_string de INT64_MIN debe formatearse correctamente sin undefined behavior");
    auto parse_min = Money::try_parse(min_str, true);
    TEST_CHECK(parse_min.is_success() && parse_min.value().cents() == std::numeric_limits<std::int64_t>::min(),
               "Round-trip de INT64_MIN debe ser exacto sin desbordamiento");
}

void test_ingredient_cost_calculation() {
    std::cout << "[TEST] UnitConverter: cálculo proporcional de costo (1 kg limón por $50)..." << std::endl;

    // 1 kg = 1,000,000 mg
    auto base_units_res = UnitConverter::to_base_units(1, "kg");
    TEST_CHECK(base_units_res.is_success(), "Conversión de 1 kg a mg debe ser exitosa");
    std::int64_t one_kg_mg = base_units_res.value();
    TEST_CHECK(one_kg_mg == 1000000, "1 kg debe ser 1,000,000 mg");

    Money cost_50_pesos = Money::from_major_minor(50, 0); // 5000 centavos

    // 1 gramo = 1,000 mg
    auto one_g_res = UnitConverter::to_base_units(1, "g");
    TEST_CHECK(one_g_res.is_success(), "Conversión de 1 g a mg debe ser exitosa");
    std::int64_t one_g_mg = one_g_res.value();
    TEST_CHECK(one_g_mg == 1000, "1 g debe ser 1,000 mg");

    // Costo para 1 gramo: DEBE SER EXACTAMENTE $0.05 (5 centavos), NUNCA 0 centavos
    auto cost_1g = UnitConverter::calculate_proportional_cost(cost_50_pesos, one_kg_mg, one_g_mg);
    TEST_CHECK(cost_1g.is_success(), "Cálculo de costo de 1 g debe ser exitoso");
    TEST_CHECK(cost_1g.value().cents() == 5, "1 g de limón por $50/kg debe costar 5 centavos");
    TEST_CHECK(cost_1g.value().to_string() == "$0.05", "El formato del costo de 1 g debe ser $0.05");

    // Costo para 10 gramos: debe ser $0.50 (50 centavos)
    auto cost_10g = UnitConverter::calculate_proportional_cost(cost_50_pesos, one_kg_mg, 10 * 1000);
    TEST_CHECK(cost_10g.is_success(), "Cálculo de costo de 10 g debe ser exitoso");
    TEST_CHECK(cost_10g.value().cents() == 50, "10 g deben costar 50 centavos");
    TEST_CHECK(cost_10g.value().to_string() == "$0.50", "El formato debe ser $0.50");

    // Costo para 1 kg entero: debe ser exactamente $50.00
    auto cost_full_kg = UnitConverter::calculate_proportional_cost(cost_50_pesos, one_kg_mg, one_kg_mg);
    TEST_CHECK(cost_full_kg.is_success(), "Cálculo de 1 kg debe ser exitoso");
    TEST_CHECK(cost_full_kg.value().cents() == 5000, "1 kg completo debe costar 5000 centavos ($50.00)");

    // Prueba de RationalCost struct
    auto rat_cost_res = UnitConverter::make_rational_cost(cost_50_pesos, one_kg_mg);
    TEST_CHECK(rat_cost_res.is_success(), "Creación de RationalCost debe ser exitosa");
    auto rat_cost = rat_cost_res.value();
    auto cost_from_struct = rat_cost.cost_for_quantity(one_g_mg);
    TEST_CHECK(cost_from_struct.is_success(), "Cálculo desde struct debe ser exitoso");
    TEST_CHECK(cost_from_struct.value().cents() == 5, "Struct RationalCost debe producir 5 centavos");

    // Validar unidades incompatibles
    TEST_CHECK(!UnitConverter::are_compatible("kg", "l"), "kg y l no son compatibles");
    TEST_CHECK(!UnitConverter::are_compatible("g", "pz"), "g y pz no son compatibles");
    TEST_CHECK(UnitConverter::are_compatible("kg", "g"), "kg y g son compatibles");
    TEST_CHECK(UnitConverter::are_compatible("ml", "litros"), "ml y litros son compatibles");

    // Rechazar cantidades negativas
    auto neg_units = UnitConverter::to_base_units(-5, "kg");
    TEST_CHECK(!neg_units.is_success(), "Cantidad negativa debe ser rechazada");
}

void test_password_hasher() {
    std::cout << "[TEST] PasswordHasher: derivación PBKDF2-HMAC-SHA256 y sal individual..." << std::endl;

    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(charset) - 2);

    std::string pwd;
    for (int i = 0; i < 16; ++i) pwd += charset[dist(gen)];

    std::string wrong_pwd;
    for (int i = 0; i < 18; ++i) wrong_pwd += charset[dist(gen)];
    while (wrong_pwd == pwd) {
        wrong_pwd += charset[dist(gen)];
    }

    std::string hash1 = PasswordHasher::hash_password(pwd, 1000); // 1000 iteraciones en test rápido
    std::string hash2 = PasswordHasher::hash_password(pwd, 1000);

    TEST_CHECK(hash1.rfind("pbkdf2_sha256$", 0) == 0, "Formato debe iniciar con pbkdf2_sha256$");
    TEST_CHECK(hash1 != hash2, "Dos hashes de la misma contraseña deben diferir por la sal individual");

    TEST_CHECK(PasswordHasher::verify_password(pwd, hash1), "Verificación con contraseña correcta debe ser true");
    TEST_CHECK(!PasswordHasher::verify_password(wrong_pwd, hash1), "Verificación con contraseña incorrecta debe ser false");
    TEST_CHECK(!PasswordHasher::verify_password(pwd, "hash_invalido"), "Hash mal formado debe ser rechazado");
}

void test_result_and_errors() {
    std::cout << "[TEST] Result y OperationResult..." << std::endl;

    auto op_ok = OperationResult::ok();
    TEST_CHECK(op_ok.is_success(), "op_ok debe ser exitoso");

    auto op_fail = OperationResult::fail(ErrorCode::Forbidden, "Acceso no autorizado");
    TEST_CHECK(!op_fail.is_success(), "op_fail no debe ser exitoso");
    TEST_CHECK(op_fail.error_code() == ErrorCode::Forbidden, "Código debe ser Forbidden");
    TEST_CHECK(op_fail.error_message() == "Acceso no autorizado", "Mensaje de error debe coincidir");

    auto res_val = Result<int>::ok(42);
    TEST_CHECK(res_val.is_success(), "res_val debe ser exitoso");
    TEST_CHECK(res_val.value() == 42, "Valor de res_val debe ser 42");

    auto res_err = Result<int>::fail(ErrorCode::NotFound, "No existe");
    TEST_CHECK(!res_err.is_success(), "res_err no debe ser exitoso");
    TEST_CHECK(res_err.error_code() == ErrorCode::NotFound, "Código debe ser NotFound");
    TEST_CHECK(res_err.value_or(99) == 99, "value_or debe retornar fallback");
}

void test_ticket_dto_formatting() {
    std::cout << "[TEST] TicketDto::to_formatted_text (anchos dinámicos, encabezados largos, importes grandes)..." << std::endl;

    TicketDto t;
    t.folio = "V-99999";
    t.created_at = "2026-09-30 21:00:00";
    t.business_name = "Cafetería Tecnológica ITCH II — Campus Central Especialidad Gastronómica";
    t.cashier_name = "Lic. Juan Francisco De La Mora Y Mendoza (Nombre histórico no disponible)";
    t.payment_method = PaymentMethod::Efectivo;

    TicketLineItemDto item1;
    item1.product_name = "Torta Especial Milanesa Gigante con Queso Oaxaca y Aguacate Extra";
    item1.quantity = 2;
    item1.unit_price = Money::from_major_minor(75, 50);
    item1.subtotal = Money::from_major_minor(151, 0);
    item1.notes = "Sin mayonesa, bien caliente, salsa roja aparte en contenedor cerrado";
    t.items.push_back(item1);

    TicketLineItemDto item2;
    item2.product_name = "Café Americano Grande";
    item2.quantity = 1;
    item2.unit_price = Money::from_major_minor(35, 0);
    item2.subtotal = Money::from_major_minor(35, 0);
    item2.notes = "Con leche deslactosada light y tres sobres de sustituto de azucar";
    t.items.push_back(item2);

    // Large quantity and large amount item
    TicketLineItemDto item_huge;
    item_huge.product_name = "Evento Especial Banquete Escolar Completo";
    item_huge.quantity = 1000000;
    item_huge.unit_price = Money::from_major_minor(200000, 0);
    item_huge.subtotal = Money::from_major_minor(200000000000LL, 0); // $200,000,000,000.00
    item_huge.notes = "Contrato gubernamental validado por consejo escolar";
    t.items.push_back(item_huge);

    t.subtotal = Money::from_major_minor(200000000186LL, 0);
    t.discount = Money::from_major_minor(100, 0);
    t.total = Money::from_major_minor(200000000086LL, 0);
    t.cash_received = Money::from_major_minor(250000000000LL, 0);
    t.change = Money::from_major_minor(49999999914LL, 0);

    std::vector<size_t> test_cols = {24, 32, 40, 48, 80};
    for (size_t cols : test_cols) {
        std::string formatted = t.to_formatted_text(cols);
        TEST_CHECK(!formatted.empty(), "El texto formateado no debe estar vacío");

        std::istringstream stream(formatted);
        std::string line;
        size_t line_num = 0;
        bool found_business = false;
        bool found_cashier = false;
        bool found_huge_sub = false;
        bool found_notes = false;

        while (std::getline(stream, line)) {
            line_num++;
            size_t w = TicketDto::utf8_column_width(line);
            if (w > cols) {
                std::cerr << "FAIL DETAIL: cols=" << cols << " line_num=" << line_num << " w=" << w << " line='" << line << "'\n";
            }
            TEST_CHECK(w <= cols, "Ninguna línea formateada debe exceder el ancho de columnas especificado");

            if (line.find("Cafetería Tecnológica") != std::string::npos) found_business = true;
            if (line.find("Lic. Juan") != std::string::npos) found_cashier = true;
            if (line.find("$200,000,000,000.00") != std::string::npos) found_huge_sub = true;
            if (line.find("Sin mayonesa") != std::string::npos) found_notes = true;

            TEST_CHECK(line.find("...") == std::string::npos, "Ninguna línea debe tener truncamiento silencioso con '...'");
        }

        TEST_CHECK(found_business, "Nombre de negocio debe estar presente en el ticket");
        TEST_CHECK(found_cashier, "Nombre de cajero debe estar presente en el ticket");
        TEST_CHECK(found_huge_sub, "Importe grande debe estar presente completo");
        TEST_CHECK(found_notes, "Notas deben estar presentes completas");
    }
}

void test_ticket_utf8_word_wrapping() {
    std::cout << "[TEST] TicketDto::wrap_text_words (puntos de código UTF-8, límites de acentos, reconstrucción sin pérdida)..." << std::endl;

    // Caso 1: Reproducción independiente auditada wrap_text_words("áá", 1)
    {
        auto lines = TicketDto::wrap_text_words("áá", 1);
        TEST_CHECK(lines.size() == 2, "Debe generar exactamente 2 líneas para 'áá' con max_cols=1");
        TEST_CHECK(lines[0] == "á", "Línea 0 debe ser 'á' completo");
        TEST_CHECK(lines[1] == "á", "Línea 1 debe ser 'á' completo");
        // Comprobar bytes exactos de 'á' en UTF-8 (0xC3, 0xA1)
        TEST_CHECK(lines[0].length() == 2, "La longitud en bytes de 'á' debe ser 2");
        TEST_CHECK(static_cast<unsigned char>(lines[0][0]) == 0xC3, "Byte 0 debe ser lead byte 0xC3");
        TEST_CHECK(static_cast<unsigned char>(lines[0][1]) == 0xA1, "Byte 1 debe ser continuation byte 0xA1");
        TEST_CHECK(TicketDto::utf8_column_width(lines[0]) == 1, "Ancho visual de 'á' debe ser 1");
        TEST_CHECK(TicketDto::utf8_column_width(lines[1]) == 1, "Ancho visual de 'á' debe ser 1");
    }

    // Caso 2: Acentos justo en el límite de partición
    {
        // "Cafetería": 'C'(1) 'a'(2) 'f'(3) 'e'(4) 't'(5) 'e'(6) 'r'(7) 'í'(8) 'a'(9)
        // Con max_cols = 4:
        // Chunk 1: "Cafe" (4)
        // Chunk 2: "terí" (4 cols, pero 'í' es multibyte de 2 bytes -> 5 bytes totales)
        // Chunk 3: "a" (1)
        auto lines = TicketDto::wrap_text_words("Cafetería", 4);
        TEST_CHECK(lines.size() == 3, "Debe dividir 'Cafetería' en 3 fragmentos");
        TEST_CHECK(lines[0] == "Cafe", "Fragmento 1 debe ser 'Cafe'");
        TEST_CHECK(lines[1] == "terí", "Fragmento 2 debe ser 'terí' con 'í' completo");
        TEST_CHECK(lines[2] == "a", "Fragmento 3 debe ser 'a'");
        TEST_CHECK(lines[1].length() == 5, "terí debe tener 5 bytes (4 letras, con 'í' de 2 bytes)");
        TEST_CHECK(TicketDto::utf8_column_width(lines[1]) == 4, "Ancho visual de 'terí' debe ser exactamente 4");

        // Reconstrucción sin pérdida
        std::string reconstructed = lines[0] + lines[1] + lines[2];
        TEST_CHECK(reconstructed == "Cafetería", "Reconstrucción debe ser idéntica a 'Cafetería'");
    }

    // Caso 3: Acento en el límite con max_cols = 5
    {
        // "Camión" (6 letras visuales: 'C','a','m','i','ó','n')
        // Con max_cols = 5:
        // Chunk 1: "Camió" (5 cols, 'ó' es multibyte de 2 bytes -> 6 bytes totales)
        // Chunk 2: "n" (1 col)
        auto lines = TicketDto::wrap_text_words("Camión", 5);
        TEST_CHECK(lines.size() == 2, "Debe dividir 'Camión' en 2 fragmentos");
        TEST_CHECK(lines[0] == "Camió", "Fragmento 1 debe ser 'Camió'");
        TEST_CHECK(lines[1] == "n", "Fragmento 2 debe ser 'n'");
        TEST_CHECK(TicketDto::utf8_column_width(lines[0]) == 5, "Ancho visual de 'Camió' debe ser 5");
        TEST_CHECK(lines[0] + lines[1] == "Camión", "Reconstrucción de 'Camión' sin pérdida");
    }

    // Caso 4: Palabras con caracteres multibyte de 3 bytes
    {
        // '€' es 3 bytes (0xE2, 0x82, 0xAC)
        std::string euro_word = "Precio€uro"; // 'P','r','e','c','i','o','€','u','r','o' -> 10 cols
        auto lines = TicketDto::wrap_text_words(euro_word, 7);
        TEST_CHECK(lines.size() == 2, "Debe dividir en 2 líneas");
        TEST_CHECK(lines[0] == "Precio€", "Línea 1 debe ser 'Precio€' con '€' completo");
        TEST_CHECK(lines[1] == "uro", "Línea 2 debe ser 'uro'");
        TEST_CHECK(lines[0] + lines[1] == euro_word, "Reconstrucción debe preservar el euro");
    }
}

int main() {
    std::cout << "=== PRUEBAS DE CONTRATOS, DOMINIO Y CRIPTOGRAFÍA (Debug y Release) ===" << std::endl;
    test_money_basics();
    test_money_arithmetic();
    test_money_parsing();
    test_ingredient_cost_calculation();
    test_password_hasher();
    test_result_and_errors();
    test_ticket_dto_formatting();
    test_ticket_utf8_word_wrapping();
    std::cout << "\n>>> TODAS LAS PRUEBAS DE CONTRATOS PASARON CON ÉXITO! <<<\n" << std::endl;
    return 0;
}

