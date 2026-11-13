#include "app/services/sqlite_cart_service.hpp"
#include "app/services/identifiers.hpp"
#include "app/domain/clock.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <algorithm>
#include <limits>

namespace app::services {

namespace {

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

class TransactionScope {
public:
    explicit TransactionScope(storage::SqliteDatabase& db) : db_(db) {
        begin_res_ = db_.begin_transaction();
    }

    ~TransactionScope() {
        if (begin_res_.is_success() && !committed_) {
            db_.rollback_transaction();
        }
    }

    bool is_active() const { return begin_res_.is_success(); }
    const contracts::OperationResult& begin_result() const { return begin_res_; }

    contracts::OperationResult commit() {
        if (!begin_res_.is_success()) {
            return begin_res_;
        }
        auto res = db_.commit_transaction();
        if (res.is_success()) {
            committed_ = true;
        }
        return res;
    }

private:
    storage::SqliteDatabase& db_;
    contracts::OperationResult begin_res_;
    bool committed_{false};
};

#define CHECK_BIND_OP(expr) \
    do { \
        auto _b_res = (expr); \
        if (!_b_res.is_success()) { \
            return contracts::OperationResult::fail(_b_res.error_code(), _b_res.error_message()); \
        } \
    } while (0)

#define CHECK_BIND_RES(expr, T) \
    do { \
        auto _b_res = (expr); \
        if (!_b_res.is_success()) { \
            return contracts::Result<T>::fail(_b_res.error_code(), _b_res.error_message()); \
        } \
    } while (0)

} // namespace

SqliteCartService::SqliteCartService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service,
    std::shared_ptr<contracts::ICatalogService> catalog_service
) : db_(db), account_service_(std::move(account_service)), catalog_service_(std::move(catalog_service)) {}

contracts::OperationResult SqliteCartService::check_permission_or_fail(std::string_view perm) const {
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

bool SqliteCartService::validate_line_notes(const std::string& notes, std::string& error) {
    if (notes.length() > 250) {
        error = "La nota de preparación no puede exceder 250 caracteres";
        return false;
    }
    for (char c : notes) {
        auto uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127) {
            error = "Caracteres de control no permitidos en la nota de preparación";
            return false;
        }
    }
    return true;
}

contracts::OperationResult SqliteCartService::touch_cart(const std::string& cart_id) {
    auto prep = db_.prepare("UPDATE carts SET updated_at = ? WHERE id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    CHECK_BIND_OP(stmt.bind_text(1, domain::now().local));
    CHECK_BIND_OP(stmt.bind_text(2, cart_id));
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }
    return contracts::OperationResult::ok();
}

contracts::Result<std::string> SqliteCartService::get_or_create_cart_id() {
    if (!account_service_ || !account_service_->is_authenticated()) {
        return contracts::Result<std::string>::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere inicio de sesión para operar el carrito"
        );
    }

    auto u = account_service_->current_user();
    if (!u.is_success()) {
        return contracts::Result<std::string>::fail(
            contracts::ErrorCode::Forbidden,
            "No hay una cuenta activa válida"
        );
    }
    const std::string user_id = u.value().id;
    const std::string register_id = "reg-01";

    // 1. Buscar carrito existente para el usuario y caja actual
    {
        auto prep = db_.prepare("SELECT id FROM carts WHERE user_id = ? AND register_id = ? LIMIT 1;");
        if (!prep.is_success()) {
            return contracts::Result<std::string>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_text(1, user_id), std::string);
        CHECK_BIND_RES(stmt.bind_text(2, register_id), std::string);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::string>::fail(step.error_code(), step.error_message());
        }
        if (step.value()) {
            return contracts::Result<std::string>::ok(stmt.get_text(0));
        }
    }

    // 2. Crear nuevo carrito si no existe
    std::string cart_id = generate_id("cart");
    std::string now = domain::now().local;
    {
        auto prep = db_.prepare("INSERT INTO carts (id, register_id, user_id, updated_at) VALUES (?, ?, ?, ?);");
        if (!prep.is_success()) {
            return contracts::Result<std::string>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_text(1, cart_id), std::string);
        CHECK_BIND_RES(stmt.bind_text(2, register_id), std::string);
        CHECK_BIND_RES(stmt.bind_text(3, user_id), std::string);
        CHECK_BIND_RES(stmt.bind_text(4, now), std::string);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::string>::fail(step.error_code(), step.error_message());
        }
    }

    return contracts::Result<std::string>::ok(cart_id);
}

contracts::Result<contracts::CartSummaryDto> SqliteCartService::get_cart() {
    auto perm = check_permission_or_fail("pos.sell");
    if (!perm.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(perm.error_code(), perm.error_message());
    }

    auto cart_id_res = get_or_create_cart_id();
    if (!cart_id_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(cart_id_res.error_code(), cart_id_res.error_message());
    }
    const std::string& cart_id = cart_id_res.value();

    const char* sql =
        "SELECT ci.id, ci.product_id, p.sku, p.name, ci.unit_price_cents, ci.quantity, ci.notes "
        "FROM cart_items ci "
        "JOIN products p ON ci.product_id = p.id "
        "WHERE ci.cart_id = ? "
        "ORDER BY ci.rowid ASC;";

    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    CHECK_BIND_RES(stmt.bind_text(1, cart_id), contracts::CartSummaryDto);

    contracts::CartSummaryDto summary;
    summary.total_items = 0;
    summary.subtotal = contracts::Money::zero();
    summary.discount = contracts::Money::zero();
    summary.total = contracts::Money::zero();

    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;

        contracts::CartItemDto item;
        item.line_id = stmt.get_text(0);
        item.product_id = stmt.get_text(1);
        item.sku = stmt.get_text(2);
        item.name = stmt.get_text(3);
        std::int64_t unit_cents = stmt.get_int64(4);
        item.unit_price = contracts::Money(unit_cents);
        item.quantity = stmt.get_int64(5);
        item.notes = stmt.get_text(6);

        std::int64_t subtotal_cents = 0;
        if (!safe_multiply(item.quantity, unit_cents, subtotal_cents)) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al calcular el subtotal de la línea"
            );
        }
        item.subtotal = contracts::Money::from_cents(subtotal_cents);

        std::int64_t new_total_items = 0;
        if (!safe_add(summary.total_items, item.quantity, new_total_items)) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al calcular la cantidad total de artículos"
            );
        }
        summary.total_items = new_total_items;

        std::int64_t new_subtotal = 0;
        if (!safe_add(summary.subtotal.cents(), subtotal_cents, new_subtotal)) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al calcular el subtotal acumulado del carrito"
            );
        }
        summary.subtotal = contracts::Money::from_cents(new_subtotal);
        summary.items.push_back(std::move(item));
    }

    summary.total = summary.subtotal - summary.discount;
    return contracts::Result<contracts::CartSummaryDto>::ok(summary);
}

contracts::Result<contracts::CartSummaryDto> SqliteCartService::add_item(
    const std::string& product_id,
    std::int64_t quantity,
    const std::string& notes
) {
    auto perm_check = check_permission_or_fail("pos.sell");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(perm_check.error_code(), perm_check.error_message());
    }
    if (!notes.empty()) {
        auto note_perm = check_permission_or_fail("pos.notes");
        if (!note_perm.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(note_perm.error_code(), note_perm.error_message());
        }
    }

    if (quantity <= 0 || quantity > 1000000) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "La cantidad debe ser mayor a 0 y menor o igual a 1,000,000"
        );
    }

    std::string note_err;
    if (!validate_line_notes(notes, note_err)) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            note_err
        );
    }

    // 1. Verificar producto y precio activo
    std::int64_t price_cents = 0;
    {
        auto prep = db_.prepare("SELECT price_cents, is_active FROM products WHERE id = ?;");
        if (!prep.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_text(1, product_id), contracts::CartSummaryDto);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::NotFound,
                "Producto no encontrado en catálogo"
            );
        }
        if (stmt.get_int64(1) != 1) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::Conflict,
                "El producto está inactivo en catálogo"
            );
        }
        price_cents = stmt.get_int64(0);
    }

    auto cart_id_res = get_or_create_cart_id();
    if (!cart_id_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(cart_id_res.error_code(), cart_id_res.error_message());
    }
    const std::string& cart_id = cart_id_res.value();

    // Iniciar transacción atómica para garantizar que ante cualquier error el borrador quede idéntico
    TransactionScope tx(db_);
    if (!tx.is_active()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            tx.begin_result().error_code(),
            tx.begin_result().error_message()
        );
    }

    // 2. Regla de agrupación: agrupar productos iguales solo cuando coincidan exactamente la nota y el precio unitario
    std::string existing_line_id;
    std::int64_t existing_qty = 0;
    {
        auto prep = db_.prepare("SELECT id, quantity FROM cart_items WHERE cart_id = ? AND product_id = ? AND notes = ? AND unit_price_cents = ? LIMIT 1;");
        if (!prep.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_text(1, cart_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(2, product_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(3, notes), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_int64(4, price_cents), contracts::CartSummaryDto);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
        }
        if (step.value()) {
            existing_line_id = stmt.get_text(0);
            existing_qty = stmt.get_int64(1);
        }
    }

    std::int64_t new_qty = quantity;
    if (!existing_line_id.empty()) {
        if (!safe_add(existing_qty, quantity, new_qty) || new_qty <= 0 || new_qty > 1000000) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico en la cantidad acumulada del producto"
            );
        }
    }

    // Validar subtotal de la línea antes de escribir
    std::int64_t line_subtotal = 0;
    if (!safe_multiply(new_qty, price_cents, line_subtotal)) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "Desbordamiento numérico al calcular el subtotal de la línea"
        );
    }

    if (!existing_line_id.empty()) {
        auto prep = db_.prepare("UPDATE cart_items SET quantity = ? WHERE id = ? AND cart_id = ?;");
        if (!prep.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_int64(1, new_qty), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(2, existing_line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(3, cart_id), contracts::CartSummaryDto);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
        }
    } else {
        std::string line_id = generate_id("line");
        auto prep = db_.prepare("INSERT INTO cart_items (id, cart_id, product_id, quantity, unit_price_cents, notes) VALUES (?, ?, ?, ?, ?, ?);");
        if (!prep.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_text(1, line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(2, cart_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(3, product_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_int64(4, quantity), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_int64(5, price_cents), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(6, notes), contracts::CartSummaryDto);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
        }
    }

    // Actualizar updated_at del carrito (comprobando prepare y step)
    auto touch_res = touch_cart(cart_id);
    if (!touch_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(touch_res.error_code(), touch_res.error_message());
    }

    // Validar integridad completa del carrito (subtotales y acumulados) antes de confirmar transacción
    auto summary_res = get_cart();
    if (!summary_res.is_success()) {
        return summary_res;
    }

    auto commit_res = tx.commit();
    if (!commit_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(commit_res.error_code(), commit_res.error_message());
    }

    return summary_res;
}

contracts::Result<contracts::CartSummaryDto> SqliteCartService::update_quantity(
    const std::string& line_id,
    std::int64_t new_quantity
) {
    auto perm_check = check_permission_or_fail("pos.sell");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(perm_check.error_code(), perm_check.error_message());
    }

    auto cart_res = get_or_create_cart_id();
    if (!cart_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(cart_res.error_code(), cart_res.error_message());
    }
    const std::string& cart_id = cart_res.value();

    if (new_quantity < 0) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "La cantidad no puede ser negativa"
        );
    }

    if (new_quantity == 0) {
        return remove_item(line_id);
    }

    if (new_quantity > 1000000) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "La cantidad no puede exceder 1,000,000"
        );
    }

    TransactionScope tx(db_);
    if (!tx.is_active()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            tx.begin_result().error_code(),
            tx.begin_result().error_message()
        );
    }

    // Comprobar que la línea pertenezca estrictamente al carrito del usuario/caja actual y obtener precio unitario
    std::int64_t unit_price_cents = 0;
    {
        auto prep_chk = db_.prepare("SELECT unit_price_cents FROM cart_items WHERE id = ? AND cart_id = ?;");
        if (!prep_chk.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep_chk.error_code(), prep_chk.error_message());
        }
        auto stmt_chk = std::move(prep_chk.value());
        CHECK_BIND_RES(stmt_chk.bind_text(1, line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_chk.bind_text(2, cart_id), contracts::CartSummaryDto);
        auto step_chk = stmt_chk.step();
        if (!step_chk.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step_chk.error_code(), step_chk.error_message());
        }
        if (!step_chk.value()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::NotFound,
                "La línea no pertenece al carrito activo del usuario actual"
            );
        }
        unit_price_cents = stmt_chk.get_int64(0);
    }

    // Pre-validar subtotal de la línea
    std::int64_t line_subtotal = 0;
    if (!safe_multiply(new_quantity, unit_price_cents, line_subtotal)) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "Desbordamiento numérico al calcular el subtotal de la línea"
        );
    }

    auto prep = db_.prepare("UPDATE cart_items SET quantity = ? WHERE id = ? AND cart_id = ?;");
    if (!prep.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    CHECK_BIND_RES(stmt.bind_int64(1, new_quantity), contracts::CartSummaryDto);
    CHECK_BIND_RES(stmt.bind_text(2, line_id), contracts::CartSummaryDto);
    CHECK_BIND_RES(stmt.bind_text(3, cart_id), contracts::CartSummaryDto);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
    }

    // Actualizar updated_at del carrito
    auto touch_res = touch_cart(cart_id);
    if (!touch_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(touch_res.error_code(), touch_res.error_message());
    }

    // Validar integridad acumulada del carrito antes de confirmar transacción
    auto summary_res = get_cart();
    if (!summary_res.is_success()) {
        return summary_res;
    }

    auto commit_res = tx.commit();
    if (!commit_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(commit_res.error_code(), commit_res.error_message());
    }

    return summary_res;
}

contracts::Result<contracts::CartSummaryDto> SqliteCartService::update_item_notes(
    const std::string& line_id,
    const std::string& notes
) {
    auto perm_check = check_permission_or_fail("pos.notes");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(perm_check.error_code(), perm_check.error_message());
    }

    std::string note_err;
    if (!validate_line_notes(notes, note_err)) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            contracts::ErrorCode::InvalidInput,
            note_err
        );
    }

    auto cart_res = get_or_create_cart_id();
    if (!cart_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(cart_res.error_code(), cart_res.error_message());
    }
    const std::string& cart_id = cart_res.value();

    TransactionScope tx(db_);
    if (!tx.is_active()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            tx.begin_result().error_code(),
            tx.begin_result().error_message()
        );
    }

    // Comprobar que la línea pertenezca estrictamente al carrito del usuario/caja actual
    std::string product_id;
    std::int64_t current_qty = 0;
    std::int64_t unit_price_cents = 0;
    {
        auto prep_chk = db_.prepare("SELECT id, product_id, quantity, unit_price_cents FROM cart_items WHERE id = ? AND cart_id = ?;");
        if (!prep_chk.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep_chk.error_code(), prep_chk.error_message());
        }
        auto stmt_chk = std::move(prep_chk.value());
        CHECK_BIND_RES(stmt_chk.bind_text(1, line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_chk.bind_text(2, cart_id), contracts::CartSummaryDto);
        auto step_chk = stmt_chk.step();
        if (!step_chk.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step_chk.error_code(), step_chk.error_message());
        }
        if (!step_chk.value()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::NotFound,
                "La línea no pertenece al carrito activo del usuario actual"
            );
        }
        product_id = stmt_chk.get_text(1);
        current_qty = stmt_chk.get_int64(2);
        unit_price_cents = stmt_chk.get_int64(3);
    }

    // Regla de agrupación estricta: fusionar líneas ÚNICAMENTE cuando coincidan producto,
    // notas de preparación Y precio unitario aplicado. Si los precios difieren, no se fusionan.
    std::string other_line_id;
    std::int64_t other_qty = 0;
    {
        auto prep_other = db_.prepare(
            "SELECT id, quantity FROM cart_items "
            "WHERE cart_id = ? AND product_id = ? AND notes = ? AND unit_price_cents = ? AND id != ? LIMIT 1;"
        );
        if (!prep_other.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep_other.error_code(), prep_other.error_message());
        }
        auto stmt_other = std::move(prep_other.value());
        CHECK_BIND_RES(stmt_other.bind_text(1, cart_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_other.bind_text(2, product_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_other.bind_text(3, notes), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_other.bind_int64(4, unit_price_cents), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_other.bind_text(5, line_id), contracts::CartSummaryDto);
        auto step_other = stmt_other.step();
        if (!step_other.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step_other.error_code(), step_other.error_message());
        }
        if (step_other.value()) {
            other_line_id = stmt_other.get_text(0);
            other_qty = stmt_other.get_int64(1);
        }
    }

    if (!other_line_id.empty()) {
        std::int64_t new_qty = 0;
        if (!safe_add(other_qty, current_qty, new_qty) || new_qty <= 0 || new_qty > 1000000) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al fusionar cantidades de la línea"
            );
        }

        std::int64_t line_subtotal = 0;
        if (!safe_multiply(new_qty, unit_price_cents, line_subtotal)) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "Desbordamiento numérico al calcular el subtotal de la línea fusionada"
            );
        }

        // Ejecución transaccional atómica de la fusión (UPDATE + DELETE + updated_at)
        auto prep_upd = db_.prepare("UPDATE cart_items SET quantity = ? WHERE id = ? AND cart_id = ?;");
        if (!prep_upd.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep_upd.error_code(), prep_upd.error_message());
        }
        auto stmt_upd = std::move(prep_upd.value());
        CHECK_BIND_RES(stmt_upd.bind_int64(1, new_qty), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_upd.bind_text(2, other_line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_upd.bind_text(3, cart_id), contracts::CartSummaryDto);
        auto step_upd = stmt_upd.step();
        if (!step_upd.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step_upd.error_code(), step_upd.error_message());
        }

        auto prep_del = db_.prepare("DELETE FROM cart_items WHERE id = ? AND cart_id = ?;");
        if (!prep_del.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep_del.error_code(), prep_del.error_message());
        }
        auto stmt_del = std::move(prep_del.value());
        CHECK_BIND_RES(stmt_del.bind_text(1, line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_del.bind_text(2, cart_id), contracts::CartSummaryDto);
        auto step_del = stmt_del.step();
        if (!step_del.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step_del.error_code(), step_del.error_message());
        }

        auto touch_res = touch_cart(cart_id);
        if (!touch_res.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(touch_res.error_code(), touch_res.error_message());
        }
    } else {
        auto prep = db_.prepare("UPDATE cart_items SET notes = ? WHERE id = ? AND cart_id = ?;");
        if (!prep.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        CHECK_BIND_RES(stmt.bind_text(1, notes), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(2, line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt.bind_text(3, cart_id), contracts::CartSummaryDto);
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
        }

        auto touch_res = touch_cart(cart_id);
        if (!touch_res.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(touch_res.error_code(), touch_res.error_message());
        }
    }

    // Validar integridad acumulada del carrito antes de confirmar transacción
    auto summary_res = get_cart();
    if (!summary_res.is_success()) {
        return summary_res;
    }

    auto commit_res = tx.commit();
    if (!commit_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(commit_res.error_code(), commit_res.error_message());
    }

    return summary_res;
}

contracts::Result<contracts::CartSummaryDto> SqliteCartService::remove_item(const std::string& line_id) {
    auto perm_check = check_permission_or_fail("pos.sell");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(perm_check.error_code(), perm_check.error_message());
    }

    auto cart_res = get_or_create_cart_id();
    if (!cart_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(cart_res.error_code(), cart_res.error_message());
    }
    const std::string& cart_id = cart_res.value();

    TransactionScope tx(db_);
    if (!tx.is_active()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(
            tx.begin_result().error_code(),
            tx.begin_result().error_message()
        );
    }

    // Comprobar que la línea pertenezca al carrito activo del usuario actual
    {
        auto prep_chk = db_.prepare("SELECT id FROM cart_items WHERE id = ? AND cart_id = ?;");
        if (!prep_chk.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(prep_chk.error_code(), prep_chk.error_message());
        }
        auto stmt_chk = std::move(prep_chk.value());
        CHECK_BIND_RES(stmt_chk.bind_text(1, line_id), contracts::CartSummaryDto);
        CHECK_BIND_RES(stmt_chk.bind_text(2, cart_id), contracts::CartSummaryDto);
        auto step_chk = stmt_chk.step();
        if (!step_chk.is_success()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(step_chk.error_code(), step_chk.error_message());
        }
        if (!step_chk.value()) {
            return contracts::Result<contracts::CartSummaryDto>::fail(
                contracts::ErrorCode::NotFound,
                "La línea no pertenece al carrito activo del usuario actual"
            );
        }
    }

    auto prep = db_.prepare("DELETE FROM cart_items WHERE id = ? AND cart_id = ?;");
    if (!prep.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    CHECK_BIND_RES(stmt.bind_text(1, line_id), contracts::CartSummaryDto);
    CHECK_BIND_RES(stmt.bind_text(2, cart_id), contracts::CartSummaryDto);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(step.error_code(), step.error_message());
    }

    auto touch_res = touch_cart(cart_id);
    if (!touch_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(touch_res.error_code(), touch_res.error_message());
    }

    auto summary_res = get_cart();
    if (!summary_res.is_success()) {
        return summary_res;
    }

    auto commit_res = tx.commit();
    if (!commit_res.is_success()) {
        return contracts::Result<contracts::CartSummaryDto>::fail(commit_res.error_code(), commit_res.error_message());
    }

    return summary_res;
}

contracts::OperationResult SqliteCartService::clear_cart() {
    if (!account_service_ || !account_service_->is_authenticated()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere inicio de sesión"
        );
    }
    if (!account_service_->has_permission("pos.cancel_order") && !account_service_->has_permission("pos.sell")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Permiso insuficiente para vaciar o cancelar el carrito"
        );
    }

    auto cart_id_res = get_or_create_cart_id();
    if (!cart_id_res.is_success()) {
        return contracts::OperationResult::fail(cart_id_res.error_code(), cart_id_res.error_message());
    }
    const std::string& cart_id = cart_id_res.value();

    TransactionScope tx(db_);
    if (!tx.is_active()) {
        return tx.begin_result();
    }

    auto prep = db_.prepare("DELETE FROM cart_items WHERE cart_id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    CHECK_BIND_OP(stmt.bind_text(1, cart_id));
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    auto touch_res = touch_cart(cart_id);
    if (!touch_res.is_success()) {
        return touch_res;
    }

    auto commit_res = tx.commit();
    if (!commit_res.is_success()) {
        return commit_res;
    }

    return contracts::OperationResult::ok();
}

} // namespace app::services
