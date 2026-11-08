#include "app/services/sqlite_sale_service.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <limits>
#include <unordered_map>

namespace app::services {

namespace {

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string generate_id(const std::string& prefix) {
    static std::atomic<std::uint64_t> counter{0};
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::stringstream ss;
    ss << prefix << "-" << now_ms << "-" << ++counter;
    return ss.str();
}

contracts::PaymentMethod parse_payment_method(const std::string& str) {
    if (str == "TARJETA") return contracts::PaymentMethod::Tarjeta;
    if (str == "TRANSFERENCIA") return contracts::PaymentMethod::Transferencia;
    return contracts::PaymentMethod::Efectivo;
}

bool safe_multiply(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if (a > 0 && b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) return false;
    if (a < 0 && b < 0 && a < std::numeric_limits<std::int64_t>::max() / b) return false;
    if (a > 0 && b < 0 && b < std::numeric_limits<std::int64_t>::min() / a) return false;
    if (a < 0 && b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) return false;
    result = a * b;
    return true;
}

bool safe_add(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) return false;
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) return false;
    result = a + b;
    return true;
}

} // namespace

SqliteSaleService::SqliteSaleService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service,
    std::shared_ptr<contracts::ICartService> cart_service,
    std::shared_ptr<contracts::IShiftService> shift_service
) : db_(db),
    account_service_(std::move(account_service)),
    cart_service_(std::move(cart_service)),
    shift_service_(std::move(shift_service)) {}

contracts::OperationResult SqliteSaleService::check_permission_or_fail(std::string_view perm) const {
    if (!account_service_) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Servicio de cuentas no configurado"
        );
    }
    if (!account_service_->is_authenticated()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere inicio de sesión"
        );
    }
    if (!account_service_->has_permission(perm)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Permiso insuficiente: " + std::string(perm)
        );
    }
    return contracts::OperationResult::ok();
}

std::string SqliteSaleService::generate_next_folio() {
    std::int64_t count = 0;
    auto prep = db_.prepare("SELECT COUNT(*) FROM sales;");
    if (prep.is_success()) {
        auto stmt = std::move(prep.value());
        auto step = stmt.step();
        if (step.is_success() && step.value()) {
            count = stmt.get_int64(0);
        }
    }

    std::int64_t next_num = 10001 + count;
    while (true) {
        std::string candidate = "V-" + std::to_string(next_num);
        auto prep_chk = db_.prepare("SELECT COUNT(*) FROM sales WHERE folio = ?;");
        if (prep_chk.is_success()) {
            auto stmt_chk = std::move(prep_chk.value());
            stmt_chk.bind_text(1, candidate);
            auto step = stmt_chk.step();
            if (step.is_success() && step.value() && stmt_chk.get_int64(0) == 0) {
                return candidate;
            }
        } else {
            return candidate;
        }
        next_num++;
    }
}

contracts::Result<contracts::SaleConfirmationDto> SqliteSaleService::find_sale_by_idempotency_key(const std::string& key) {
    if (key.empty()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            contracts::ErrorCode::NotFound,
            "Clave vacía"
        );
    }

    const char* sql =
        "SELECT s.id, s.folio, s.total_cents, s.created_at, "
        "p.payment_method, p.cash_received_cents, p.change_cents "
        "FROM sales s "
        "LEFT JOIN payment_records p ON s.id = p.sale_id "
        "WHERE s.idempotency_key = ? LIMIT 1;";

    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, key);
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            contracts::ErrorCode::NotFound,
            "Venta no encontrada para la clave de idempotencia especificada"
        );
    }

    contracts::SaleConfirmationDto dto;
    dto.sale_id = stmt.get_text(0);
    dto.folio = stmt.get_text(1);
    dto.total = contracts::Money(stmt.get_int64(2));
    dto.created_at = stmt.get_text(3);
    dto.payment_method = parse_payment_method(stmt.is_null(4) ? "EFECTIVO" : stmt.get_text(4));
    dto.cash_received = stmt.is_null(5) ? dto.total : contracts::Money(stmt.get_int64(5));
    dto.change = stmt.is_null(6) ? contracts::Money::zero() : contracts::Money(stmt.get_int64(6));

    return contracts::Result<contracts::SaleConfirmationDto>::ok(dto);
}

contracts::Result<contracts::SaleConfirmationDto> SqliteSaleService::confirm_sale(
    contracts::PaymentMethod payment_method,
    contracts::Money cash_received,
    const std::string& idempotency_key
) {
    // 1. Exigir sesión y permiso en la capa de servicios
    auto perm_check = check_permission_or_fail("pos.sell");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            perm_check.error_code(),
            perm_check.error_message()
        );
    }

    auto cur_user = account_service_->current_user();
    if (!cur_user.is_success()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Usuario no autenticado o sesión inválida"
        );
    }
    const std::string user_id = cur_user.value().id;
    const std::string cashier_name = cur_user.value().display_name;

    // 2. Control de idempotencia previo: Si ya existe una venta confirmada para esta clave,
    // se revalida que pertenezca al usuario y contexto de caja autorizados y se recupera de inmediato
    // (INCLUSO si el turno ya fue cerrado posteriormente).
    if (!idempotency_key.empty()) {
        const char* sql_chk =
            "SELECT s.id, s.folio, s.total_cents, s.created_at, "
            "p.payment_method, p.cash_received_cents, p.change_cents, "
            "s.user_id, sh.register_id "
            "FROM sales s "
            "JOIN shifts sh ON s.shift_id = sh.id "
            "LEFT JOIN payment_records p ON s.id = p.sale_id "
            "WHERE s.idempotency_key = ? LIMIT 1;";

        auto prep_chk = db_.prepare(sql_chk);
        if (prep_chk.is_success()) {
            auto stmt_chk = std::move(prep_chk.value());
            stmt_chk.bind_text(1, idempotency_key);
            auto step_chk = stmt_chk.step();
            if (step_chk.is_success() && step_chk.value()) {
                std::string owner_user_id = stmt_chk.get_text(7);
                std::string owner_register_id = stmt_chk.get_text(8);

                // Comprobar contexto de autorización:
                // Una cuenta o caja distinta NO puede apropiarse ni recuperar ventas con claves ajenas.
                if (owner_user_id != user_id || owner_register_id != "reg-01") {
                    return contracts::Result<contracts::SaleConfirmationDto>::fail(
                        contracts::ErrorCode::Forbidden,
                        "La clave de idempotencia especificada pertenece a otro usuario o contexto de caja"
                    );
                }

                contracts::SaleConfirmationDto dto;
                dto.sale_id = stmt_chk.get_text(0);
                dto.folio = stmt_chk.get_text(1);
                dto.total = contracts::Money(stmt_chk.get_int64(2));
                dto.created_at = stmt_chk.get_text(3);
                dto.payment_method = parse_payment_method(stmt_chk.is_null(4) ? "EFECTIVO" : stmt_chk.get_text(4));
                dto.cash_received = stmt_chk.is_null(5) ? dto.total : contracts::Money(stmt_chk.get_int64(5));
                dto.change = stmt_chk.is_null(6) ? contracts::Money::zero() : contracts::Money(stmt_chk.get_int64(6));

                {
                    std::lock_guard<std::mutex> lock(last_confirmed_mutex_);
                    last_confirmed_folio_ = dto.folio;
                    last_confirmed_user_id_ = user_id;
                }

                return contracts::Result<contracts::SaleConfirmationDto>::ok(dto);
            }
        }
    }

    // 3. Bloqueo de confirmaciones simultáneas en curso con la misma clave
    if (!idempotency_key.empty()) {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        if (in_flight_keys_.find(idempotency_key) != in_flight_keys_.end()) {
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::Conflict,
                "Ya existe una confirmación de venta en curso con esta clave de idempotencia"
            );
        }
        in_flight_keys_.insert(idempotency_key);
    }

    struct KeyCleaner {
        std::mutex& mtx;
        std::unordered_set<std::string>& set;
        std::string key;
        ~KeyCleaner() {
            if (!key.empty()) {
                std::lock_guard<std::mutex> lock(mtx);
                set.erase(key);
            }
        }
    } cleaner{in_flight_mutex_, in_flight_keys_, idempotency_key};

    // 4. Iniciar transacción atómica estricta (BEGIN IMMEDIATE)
    auto tx_begin = db_.execute("BEGIN IMMEDIATE;");
    if (!tx_begin.is_success()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            tx_begin.error_code(),
            "Error al iniciar transacción atómica de venta: " + tx_begin.error_message()
        );
    }

    // 5. Revalidar turno activo dentro de la transacción
    std::string shift_id;
    {
        auto prep_s = db_.prepare("SELECT id FROM shifts WHERE is_open = 1 ORDER BY opened_at DESC LIMIT 1;");
        if (!prep_s.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_s.error_code(), prep_s.error_message());
        }
        auto stmt_s = std::move(prep_s.value());
        auto step_s = stmt_s.step();
        if (!step_s.is_success() || !step_s.value()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::ShiftClosed,
                "No hay un turno de caja abierto para registrar ventas. Debe abrir un turno primero."
            );
        }
        shift_id = stmt_s.get_text(0);
    }

    // 6. Consultar carrito y líneas activas del usuario y caja actual
    std::string cart_id;
    {
        auto prep_c = db_.prepare("SELECT id FROM carts WHERE user_id = ? AND register_id = ? LIMIT 1;");
        if (!prep_c.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_c.error_code(), prep_c.error_message());
        }
        auto stmt_c = std::move(prep_c.value());
        stmt_c.bind_text(1, user_id);
        stmt_c.bind_text(2, "reg-01");
        auto step_c = stmt_c.step();
        if (!step_c.is_success() || !step_c.value()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::InvalidState,
                "El carrito está vacío. Agregue productos antes de cobrar."
            );
        }
        cart_id = stmt_c.get_text(0);
    }

    struct ItemToConfirm {
        std::string line_id;
        std::string product_id;
        std::string product_name;
        std::int64_t quantity;
        std::int64_t unit_price_cents;
        std::int64_t subtotal_cents;
        std::string notes;
    };
    std::vector<ItemToConfirm> items;

    {
        auto prep_items = db_.prepare(
            "SELECT id, product_id, quantity, unit_price_cents, notes "
            "FROM cart_items WHERE cart_id = ? ORDER BY rowid ASC;"
        );
        if (!prep_items.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_items.error_code(), prep_items.error_message());
        }
        auto stmt_items = std::move(prep_items.value());
        stmt_items.bind_text(1, cart_id);
        while (true) {
            auto step = stmt_items.step();
            if (!step.is_success()) {
                db_.execute("ROLLBACK;");
                return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
            }
            if (!step.value()) break;

            ItemToConfirm it;
            it.line_id = stmt_items.get_text(0);
            it.product_id = stmt_items.get_text(1);
            it.quantity = stmt_items.get_int64(2);
            it.unit_price_cents = stmt_items.get_int64(3);
            it.notes = stmt_items.get_text(4);
            items.push_back(std::move(it));
        }
    }

    if (items.empty()) {
        db_.execute("ROLLBACK;");
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            contracts::ErrorCode::InvalidState,
            "El carrito está vacío. Agregue productos antes de cobrar."
        );
    }

    // 7. Revalidar productos, precios vigentes y desbordamientos aritméticos dentro de la transacción
    std::int64_t calculated_total_cents = 0;
    for (auto& it : items) {
        if (it.quantity <= 0) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "La cantidad de productos debe ser positiva"
            );
        }

        auto prep_p = db_.prepare("SELECT name, price_cents, is_active FROM products WHERE id = ?;");
        if (!prep_p.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_p.error_code(), prep_p.error_message());
        }
        auto stmt_p = std::move(prep_p.value());
        stmt_p.bind_text(1, it.product_id);
        auto step_p = stmt_p.step();
        if (!step_p.is_success() || !step_p.value()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::NotFound,
                "Producto no encontrado en catálogo: " + it.product_id
            );
        }

        it.product_name = stmt_p.get_text(0);
        std::int64_t catalog_price = stmt_p.get_int64(1);
        std::int64_t is_active = stmt_p.get_int64(2);
        stmt_p.reset();

        if (is_active != 1) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::Conflict,
                "El producto '" + it.product_name + "' ha sido dado de baja en el catálogo."
            );
        }

        if (catalog_price != it.unit_price_cents) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::Conflict,
                "El precio del producto '" + it.product_name + "' ha cambiado en el catálogo. Actualice su carrito."
            );
        }

        std::int64_t subtotal = 0;
        if (!safe_multiply(it.quantity, it.unit_price_cents, subtotal)) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al calcular subtotal de '" + it.product_name + "'"
            );
        }
        it.subtotal_cents = subtotal;

        if (!safe_add(calculated_total_cents, subtotal, calculated_total_cents)) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al calcular el total de la venta"
            );
        }
    }

    contracts::Money total = contracts::Money::from_cents(calculated_total_cents);
    contracts::Money change = contracts::Money::zero();

    // 8. Revalidar recetas y existencias de insumos agregadas para todo el pedido
    std::unordered_map<std::string, std::int64_t> ingredient_needed;
    std::unordered_map<std::string, std::string> ingredient_names;

    for (const auto& it : items) {
        const char* sql_rec =
            "SELECT ri.ingredient_id, ri.quantity_base_units, i.name "
            "FROM recipes r "
            "JOIN recipe_items ri ON r.id = ri.recipe_id "
            "JOIN ingredients i ON ri.ingredient_id = i.id "
            "WHERE r.id = ("
            "    SELECT id FROM recipes WHERE product_id = ? ORDER BY version DESC LIMIT 1"
            ");";

        auto prep_rec = db_.prepare(sql_rec);
        if (!prep_rec.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_rec.error_code(), prep_rec.error_message());
        }

        auto stmt_rec = std::move(prep_rec.value());
        stmt_rec.bind_text(1, it.product_id);
        while (true) {
            auto step_rec = stmt_rec.step();
            if (!step_rec.is_success()) {
                db_.execute("ROLLBACK;");
                return contracts::Result<contracts::SaleConfirmationDto>::fail(step_rec.error_code(), step_rec.error_message());
            }
            if (!step_rec.value()) break;

            std::string ing_id = stmt_rec.get_text(0);
            std::int64_t qty_per_portion = stmt_rec.get_int64(1);
            std::string ing_name = stmt_rec.get_text(2);

            std::int64_t consumed = 0;
            if (!safe_multiply(qty_per_portion, it.quantity, consumed)) {
                db_.execute("ROLLBACK;");
                return contracts::Result<contracts::SaleConfirmationDto>::fail(
                    contracts::ErrorCode::InvalidInput,
                    "Desbordamiento numérico en consumo de ingrediente: " + ing_name
                );
            }

            std::int64_t cur_needed = ingredient_needed[ing_id];
            if (!safe_add(cur_needed, consumed, ingredient_needed[ing_id])) {
                db_.execute("ROLLBACK;");
                return contracts::Result<contracts::SaleConfirmationDto>::fail(
                    contracts::ErrorCode::InvalidInput,
                    "Desbordamiento numérico en consumo total de ingrediente: " + ing_name
                );
            }
            ingredient_names[ing_id] = ing_name;
        }
    }

    // Comprobar existencias actuales de cada ingrediente contra el consumo total
    for (const auto& [ing_id, needed] : ingredient_needed) {
        auto prep_stk = db_.prepare("SELECT name, current_stock FROM ingredients WHERE id = ?;");
        if (!prep_stk.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_stk.error_code(), prep_stk.error_message());
        }
        auto stmt_stk = std::move(prep_stk.value());
        stmt_stk.bind_text(1, ing_id);
        auto step_stk = stmt_stk.step();
        if (!step_stk.is_success() || !step_stk.value()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::NotFound,
                "Insumo no encontrado: " + ing_id
            );
        }

        std::string name = stmt_stk.get_text(0);
        std::int64_t current_stock = stmt_stk.get_int64(1);
        stmt_stk.reset();

        if (current_stock < needed) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::InsufficientStock,
                "Existencias insuficientes para '" + name + "'. Disponible: " +
                std::to_string(current_stock) + ", Requerido: " + std::to_string(needed)
            );
        }
    }

    // 9. Validar método de pago y monto recibido
    if (payment_method == contracts::PaymentMethod::Efectivo) {
        if (cash_received < total) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "El monto recibido en efectivo (" + cash_received.to_string() + ") es menor al total (" + total.to_string() + ")"
            );
        }
        change = cash_received - total;
    } else {
        cash_received = total;
        change = contracts::Money::zero();
    }

    // 10. Inserciones atómicas de venta, líneas, inventario, pago, movimiento de caja y auditoría
    std::string sale_id = generate_id("sale");
    std::string folio = generate_next_folio();
    std::string now = current_timestamp();

    // 10.1 Inserción de venta
    {
        auto prep_sale = db_.prepare(
            "INSERT INTO sales (id, folio, shift_id, user_id, total_cents, idempotency_key, created_at, cashier_name) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"
        );
        if (!prep_sale.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_sale.error_code(), prep_sale.error_message());
        }
        auto stmt_s = std::move(prep_sale.value());
        stmt_s.bind_text(1, sale_id);
        stmt_s.bind_text(2, folio);
        stmt_s.bind_text(3, shift_id);
        stmt_s.bind_text(4, user_id);
        stmt_s.bind_int64(5, total.cents());
        if (!idempotency_key.empty()) {
            stmt_s.bind_text(6, idempotency_key);
        } else {
            stmt_s.bind_null(6);
        }
        stmt_s.bind_text(7, now);
        stmt_s.bind_text(8, cashier_name);

        auto step = stmt_s.step();
        if (!step.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
        }
    }

    // 10.2 Inserción de líneas de venta (instantánea inmutable)
    for (const auto& it : items) {
        std::string item_id = generate_id("sitem");
        auto prep_item = db_.prepare(
            "INSERT INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents, notes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"
        );
        if (!prep_item.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_item.error_code(), prep_item.error_message());
        }
        auto stmt_it = std::move(prep_item.value());
        stmt_it.bind_text(1, item_id);
        stmt_it.bind_text(2, sale_id);
        stmt_it.bind_text(3, it.product_id);
        stmt_it.bind_text(4, it.product_name);
        stmt_it.bind_int64(5, it.quantity);
        stmt_it.bind_int64(6, it.unit_price_cents);
        stmt_it.bind_int64(7, it.subtotal_cents);
        stmt_it.bind_text(8, it.notes);

        auto step = stmt_it.step();
        if (!step.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
        }
    }

    // 10.3 Descuento de inventario y movimientos de insumos
    for (const auto& [ing_id, needed] : ingredient_needed) {
        std::string mov_id = generate_id("mov");
        auto prep_mov = db_.prepare(
            "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) "
            "VALUES (?, ?, 'SALE', ?, ?, ?);"
        );
        if (!prep_mov.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_mov.error_code(), prep_mov.error_message());
        }
        auto stmt_m = std::move(prep_mov.value());
        stmt_m.bind_text(1, mov_id);
        stmt_m.bind_text(2, ing_id);
        stmt_m.bind_int64(3, -needed);
        stmt_m.bind_text(4, "Venta " + folio);
        stmt_m.bind_text(5, now);

        auto step_m = stmt_m.step();
        if (!step_m.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step_m.error_code(), step_m.error_message());
        }

        auto prep_upd = db_.prepare("UPDATE ingredients SET current_stock = current_stock - ? WHERE id = ?;");
        if (!prep_upd.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_upd.error_code(), prep_upd.error_message());
        }
        auto stmt_u = std::move(prep_upd.value());
        stmt_u.bind_int64(1, needed);
        stmt_u.bind_text(2, ing_id);

        auto step_u = stmt_u.step();
        if (!step_u.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step_u.error_code(), step_u.error_message());
        }
    }

    // 10.4 Inserción de pago declarado
    {
        std::string pay_id = generate_id("pay");
        auto prep_pay = db_.prepare(
            "INSERT INTO payment_records (id, sale_id, payment_method, amount_cents, cash_received_cents, change_cents, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);"
        );
        if (!prep_pay.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_pay.error_code(), prep_pay.error_message());
        }
        auto stmt_p = std::move(prep_pay.value());
        stmt_p.bind_text(1, pay_id);
        stmt_p.bind_text(2, sale_id);
        stmt_p.bind_text(3, contracts::to_string(payment_method));
        stmt_p.bind_int64(4, total.cents());
        stmt_p.bind_int64(5, cash_received.cents());
        stmt_p.bind_int64(6, change.cents());
        stmt_p.bind_text(7, now);

        auto step = stmt_p.step();
        if (!step.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
        }
    }

    // 10.5 Inserción de movimiento de caja en cash_movements
    {
        std::string cmov_id = generate_id("cmov");
        std::int64_t cash_impact = (payment_method == contracts::PaymentMethod::Efectivo) ? total.cents() : 0;
        std::string desc = "Venta " + folio + " [" + contracts::to_string(payment_method) + "]";

        auto prep_cmov = db_.prepare(
            "INSERT INTO cash_movements (id, shift_id, movement_type, amount_cents, description, created_at) "
            "VALUES (?, ?, 'SALE', ?, ?, ?);"
        );
        if (!prep_cmov.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_cmov.error_code(), prep_cmov.error_message());
        }
        auto stmt_cm = std::move(prep_cmov.value());
        stmt_cm.bind_text(1, cmov_id);
        stmt_cm.bind_text(2, shift_id);
        stmt_cm.bind_int64(3, cash_impact);
        stmt_cm.bind_text(4, desc);
        stmt_cm.bind_text(5, now);

        auto step = stmt_cm.step();
        if (!step.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
        }
    }

    // 10.6 Vaciar carrito de la base de datos (borrador se limpia solo ante éxito)
    {
        auto prep_clear = db_.prepare("DELETE FROM cart_items WHERE cart_id = ?;");
        if (!prep_clear.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_clear.error_code(), prep_clear.error_message());
        }
        auto stmt_cl = std::move(prep_clear.value());
        stmt_cl.bind_text(1, cart_id);
        auto step = stmt_cl.step();
        if (!step.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
        }
    }

    // 10.7 Registrar auditoría (sin secretos) dentro de la misma transacción
    {
        std::string audit_id = generate_id("audit");
        std::string details = "Venta confirmada folio " + folio + " por " + total.to_string() + " [" + contracts::to_string(payment_method) + "]";
        auto prep_aud = db_.prepare(
            "INSERT INTO audit_events (id, event_type, user_id, details, created_at) "
            "VALUES (?, 'SALE_CONFIRMED', ?, ?, ?);"
        );
        if (!prep_aud.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(prep_aud.error_code(), prep_aud.error_message());
        }
        auto stmt_a = std::move(prep_aud.value());
        stmt_a.bind_text(1, audit_id);
        stmt_a.bind_text(2, user_id);
        stmt_a.bind_text(3, details);
        stmt_a.bind_text(4, now);

        auto step = stmt_a.step();
        if (!step.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::SaleConfirmationDto>::fail(step.error_code(), step.error_message());
        }
    }

    // 11. Confirmar transacción
    auto tx_commit = db_.execute("COMMIT;");
    if (!tx_commit.is_success()) {
        db_.execute("ROLLBACK;");
        return contracts::Result<contracts::SaleConfirmationDto>::fail(tx_commit.error_code(), tx_commit.error_message());
    }

    {
        std::lock_guard<std::mutex> lock(last_confirmed_mutex_);
        last_confirmed_folio_ = folio;
        last_confirmed_user_id_ = user_id;
    }

    contracts::SaleConfirmationDto result;
    result.sale_id = sale_id;
    result.folio = folio;
    result.total = total;
    result.cash_received = cash_received;
    result.change = change;
    result.payment_method = payment_method;
    result.created_at = now;

    return contracts::Result<contracts::SaleConfirmationDto>::ok(result);
}

contracts::Result<contracts::SaleConfirmationDto> SqliteSaleService::get_sale_by_folio(const std::string& folio) {
    auto perm_check = check_permission_or_fail("pos.sell");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(perm_check.error_code(), perm_check.error_message());
    }

    const char* sql =
        "SELECT s.id, s.folio, s.total_cents, s.created_at, "
        "p.payment_method, p.cash_received_cents, p.change_cents "
        "FROM sales s "
        "LEFT JOIN payment_records p ON s.id = p.sale_id "
        "WHERE s.folio = ? LIMIT 1;";

    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, folio);
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) {
        return contracts::Result<contracts::SaleConfirmationDto>::fail(
            contracts::ErrorCode::NotFound,
            "Venta no encontrada con folio: " + folio
        );
    }

    contracts::SaleConfirmationDto dto;
    dto.sale_id = stmt.get_text(0);
    dto.folio = stmt.get_text(1);
    dto.total = contracts::Money(stmt.get_int64(2));
    dto.created_at = stmt.get_text(3);
    dto.payment_method = parse_payment_method(stmt.is_null(4) ? "EFECTIVO" : stmt.get_text(4));
    dto.cash_received = stmt.is_null(5) ? dto.total : contracts::Money(stmt.get_int64(5));
    dto.change = stmt.is_null(6) ? contracts::Money::zero() : contracts::Money(stmt.get_int64(6));

    return contracts::Result<contracts::SaleConfirmationDto>::ok(dto);
}

void SqliteSaleService::invalidate_session_state() {
    std::lock_guard<std::mutex> lock(last_confirmed_mutex_);
    last_confirmed_folio_.clear();
    last_confirmed_user_id_.clear();
}

contracts::Result<contracts::TicketDto> SqliteSaleService::get_ticket(const std::string& folio) {
    if (!account_service_ || !account_service_->is_authenticated()) {
        return contracts::Result<contracts::TicketDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere inicio de sesión"
        );
    }
    auto cur_user = account_service_->current_user();
    if (!cur_user.is_success()) {
        return contracts::Result<contracts::TicketDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Usuario no autenticado o sesión inválida"
        );
    }

    bool has_reprint = account_service_->has_permission("pos.reprint");
    bool is_recent_seller = false;
    {
        std::lock_guard<std::mutex> lock(last_confirmed_mutex_);
        if (!last_confirmed_folio_.empty() && folio == last_confirmed_folio_ && cur_user.value().id == last_confirmed_user_id_) {
            is_recent_seller = account_service_->has_permission("pos.sell");
        }
    }

    if (!has_reprint && !is_recent_seller) {
        return contracts::Result<contracts::TicketDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Permiso insuficiente: pos.reprint"
        );
    }

    const char* sql_sale =
        "SELECT s.id, s.folio, s.total_cents, s.created_at, "
        "p.payment_method, p.cash_received_cents, p.change_cents, "
        "CASE "
        "    WHEN s.cashier_name IS NOT NULL AND LENGTH(s.cashier_name) > 0 THEN s.cashier_name "
        "    WHEN s.user_id IS NOT NULL AND LENGTH(s.user_id) > 0 THEN ('Nombre histórico no disponible (' || s.user_id || ')') "
        "    ELSE 'Nombre histórico no disponible' "
        "END "
        "FROM sales s "
        "LEFT JOIN payment_records p ON s.id = p.sale_id "
        "WHERE s.folio = ? LIMIT 1;";

    auto prep_s = db_.prepare(sql_sale);
    if (!prep_s.is_success()) {
        return contracts::Result<contracts::TicketDto>::fail(prep_s.error_code(), prep_s.error_message());
    }

    auto stmt_s = std::move(prep_s.value());
    stmt_s.bind_text(1, folio);
    auto step_s = stmt_s.step();
    if (!step_s.is_success() || !step_s.value()) {
        return contracts::Result<contracts::TicketDto>::fail(
            contracts::ErrorCode::NotFound,
            "Venta no encontrada para ticket con folio: " + folio
        );
    }

    std::string sale_id = stmt_s.get_text(0);
    contracts::TicketDto ticket;
    ticket.folio = stmt_s.get_text(1);
    ticket.business_name = "Cafetería ITCH II";
    ticket.created_at = stmt_s.get_text(3);
    ticket.cashier_name = stmt_s.is_null(7) ? "" : stmt_s.get_text(7);
    
    std::string pm_str = stmt_s.is_null(4) ? "EFECTIVO" : stmt_s.get_text(4);
    if (pm_str == "TARJETA") {
        ticket.payment_method = contracts::PaymentMethod::Tarjeta;
    } else if (pm_str == "TRANSFERENCIA") {
        ticket.payment_method = contracts::PaymentMethod::Transferencia;
    } else {
        ticket.payment_method = contracts::PaymentMethod::Efectivo;
    }

    ticket.total = contracts::Money(stmt_s.get_int64(2));
    ticket.subtotal = ticket.total;
    ticket.discount = contracts::Money::zero();
    ticket.cash_received = stmt_s.is_null(5) ? ticket.total : contracts::Money(stmt_s.get_int64(5));
    ticket.change = stmt_s.is_null(6) ? contracts::Money::zero() : contracts::Money(stmt_s.get_int64(6));

    // Consultar líneas inmutables de la venta
    const char* sql_items =
        "SELECT product_name, quantity, unit_price_cents, subtotal_cents, notes "
        "FROM sale_items WHERE sale_id = ? ORDER BY rowid ASC;";

    auto prep_it = db_.prepare(sql_items);
    if (!prep_it.is_success()) {
        return contracts::Result<contracts::TicketDto>::fail(prep_it.error_code(), prep_it.error_message());
    }

    auto stmt_it = std::move(prep_it.value());
    stmt_it.bind_text(1, sale_id);
    while (true) {
        auto step = stmt_it.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::TicketDto>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;

        contracts::TicketLineItemDto it;
        it.product_name = stmt_it.get_text(0);
        it.quantity = stmt_it.get_int64(1);
        it.unit_price = contracts::Money(stmt_it.get_int64(2));
        it.subtotal = contracts::Money(stmt_it.get_int64(3));
        it.notes = stmt_it.get_text(4);
        ticket.items.push_back(std::move(it));
    }

    return contracts::Result<contracts::TicketDto>::ok(ticket);
}

contracts::Result<std::vector<contracts::TicketSummaryDto>> SqliteSaleService::get_recent_tickets(int limit) {
    auto perm_check = check_permission_or_fail("pos.reprint");
    if (!perm_check.is_success()) {
        return contracts::Result<std::vector<contracts::TicketSummaryDto>>::fail(perm_check.error_code(), perm_check.error_message());
    }

    if (limit <= 0) limit = 50;

    const char* sql =
        "SELECT s.folio, s.created_at, s.total_cents, "
        "CASE "
        "    WHEN s.cashier_name IS NOT NULL AND LENGTH(s.cashier_name) > 0 THEN s.cashier_name "
        "    WHEN s.user_id IS NOT NULL AND LENGTH(s.user_id) > 0 THEN ('Nombre histórico no disponible (' || s.user_id || ')') "
        "    ELSE 'Nombre histórico no disponible' "
        "END, "
        "p.payment_method, "
        "(SELECT COALESCE(SUM(si.quantity), 0) FROM sale_items si WHERE si.sale_id = s.id) "
        "FROM sales s "
        "LEFT JOIN payment_records p ON s.id = p.sale_id "
        "ORDER BY s.created_at DESC LIMIT ?;";

    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::Result<std::vector<contracts::TicketSummaryDto>>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, limit);

    std::vector<contracts::TicketSummaryDto> result;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::vector<contracts::TicketSummaryDto>>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;

        contracts::TicketSummaryDto item;
        item.folio = stmt.get_text(0);
        item.created_at = stmt.get_text(1);
        item.total = contracts::Money(stmt.get_int64(2));
        item.cashier_name = stmt.is_null(3) ? "" : stmt.get_text(3);
        item.payment_method = parse_payment_method(stmt.is_null(4) ? "EFECTIVO" : stmt.get_text(4));
        item.items_count = stmt.get_int64(5);

        result.push_back(std::move(item));
    }

    return contracts::Result<std::vector<contracts::TicketSummaryDto>>::ok(result);
}

} // namespace app::services
