#ifndef APP_CONTRACTS_SALE_SERVICE_HPP
#define APP_CONTRACTS_SALE_SERVICE_HPP

#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <iomanip>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

enum class PaymentMethod {
    Efectivo,
    Tarjeta,
    Transferencia
};

inline const char* to_string(PaymentMethod method) {
    switch (method) {
        case PaymentMethod::Efectivo: return "EFECTIVO";
        case PaymentMethod::Tarjeta: return "TARJETA";
        case PaymentMethod::Transferencia: return "TRANSFERENCIA";
        default: return "DESCONOCIDO";
    }
}

struct SaleConfirmationDto {
    std::string sale_id;
    std::string folio;
    Money total;
    Money cash_received;
    Money change;
    PaymentMethod payment_method;
    std::string created_at;
};

struct TicketLineItemDto {
    std::string product_name;
    std::int64_t quantity{1};
    Money unit_price{Money::zero()};
    Money subtotal{Money::zero()};
    std::string notes;
};

struct TicketDto {
    std::string business_name{"Cafetería ITCH II"};
    std::string folio;
    std::string created_at;
    std::string cashier_name;
    std::vector<TicketLineItemDto> items;
    Money subtotal{Money::zero()};
    Money discount{Money::zero()};
    Money total{Money::zero()};
    PaymentMethod payment_method{PaymentMethod::Efectivo};
    Money cash_received{Money::zero()};
    Money change{Money::zero()};

    static size_t utf8_column_width(const std::string& str) {
        size_t width = 0;
        for (size_t i = 0; i < str.length(); ++i) {
            if ((static_cast<unsigned char>(str[i]) & 0xC0) != 0x80) {
                width++;
            }
        }
        return width;
    }

    static std::string pad_right_utf8(const std::string& str, size_t target_cols) {
        size_t w = utf8_column_width(str);
        if (w < target_cols) {
            return str + std::string(target_cols - w, ' ');
        }
        return str;
    }

    // Divide una palabra larga en fragmentos de hasta max_cols columnas.
    // El corte se realiza estrictamente en límites de puntos de código UTF-8 (code points),
    // preservando caracteres multibyte completos y evitando fragmentos inválidos.
    // Nota: la segmentación opera a nivel de puntos de código UTF-8, no de cúmulos de grafemas extendidos.
    static void split_long_word_utf8(const std::string& word, size_t max_cols,
                                     std::vector<std::string>& out_lines, std::string& remainder) {
        remainder.clear();
        if (word.empty() || max_cols == 0) return;

        size_t i = 0;
        size_t cur_w = 0;
        std::string part;

        while (i < word.length()) {
            unsigned char c = static_cast<unsigned char>(word[i]);
            size_t char_len = 1;
            if ((c & 0x80) == 0x00) {
                char_len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                char_len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                char_len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                char_len = 4;
            } else {
                // Byte de continuación huérfano o lead byte fuera de rango
                char_len = 1;
            }

            if (i + char_len > word.length()) {
                char_len = word.length() - i;
            }

            part.append(word, i, char_len);
            i += char_len;
            cur_w++;

            if (cur_w == max_cols) {
                out_lines.push_back(part);
                part.clear();
                cur_w = 0;
            }
        }
        remainder = part;
    }

    static std::vector<std::string> wrap_text_words(const std::string& text, size_t max_cols) {
        std::vector<std::string> result_lines;
        if (text.empty() || max_cols == 0) return result_lines;
        
        std::istringstream line_stream(text);
        std::string raw_line;
        while (std::getline(line_stream, raw_line)) {
            if (raw_line.empty()) {
                result_lines.push_back("");
                continue;
            }
            std::istringstream iss(raw_line);
            std::string word;
            std::string current_line;

            while (iss >> word) {
                if (current_line.empty()) {
                    if (utf8_column_width(word) <= max_cols) {
                        current_line = word;
                    } else {
                        split_long_word_utf8(word, max_cols, result_lines, current_line);
                    }
                } else {
                    size_t needed = utf8_column_width(current_line) + 1 + utf8_column_width(word);
                    if (needed <= max_cols) {
                        current_line += " " + word;
                    } else {
                        result_lines.push_back(current_line);
                        current_line.clear();
                        if (utf8_column_width(word) <= max_cols) {
                            current_line = word;
                        } else {
                            split_long_word_utf8(word, max_cols, result_lines, current_line);
                        }
                    }
                }
            }
            if (!current_line.empty()) {
                result_lines.push_back(current_line);
            }
        }
        return result_lines;
    }

    std::string to_formatted_text(size_t max_cols = 40) const {
        if (max_cols < 20) max_cols = 20;

        std::stringstream ss;
        std::string sep_double(max_cols, '=');
        std::string sep_single(max_cols, '-');

        ss << sep_double << "\n";
        {
            auto bname_lines = wrap_text_words(business_name, max_cols);
            if (bname_lines.empty()) bname_lines.push_back("");
            for (const auto& bline : bname_lines) {
                size_t bname_len = utf8_column_width(bline);
                size_t pad_total = (bname_len < max_cols) ? (max_cols - bname_len) : 0;
                size_t pad_left = pad_total / 2;
                size_t pad_right = pad_total - pad_left;
                ss << std::string(pad_left, ' ') << bline << std::string(pad_right, ' ') << "\n";
            }
        }
        ss << sep_double << "\n";

        auto append_labeled_field = [&](const std::string& label, const std::string& value) {
            size_t label_w = utf8_column_width(label);
            if (max_cols > label_w + 3) {
                size_t val_avail_w = max_cols - label_w;
                auto val_lines = wrap_text_words(value, val_avail_w);
                if (val_lines.empty()) val_lines.push_back("");
                ss << label << val_lines[0] << "\n";
                for (size_t i = 1; i < val_lines.size(); ++i) {
                    ss << std::string(label_w, ' ') << val_lines[i] << "\n";
                }
            } else {
                ss << label << "\n";
                size_t val_w = (max_cols > 2) ? (max_cols - 2) : max_cols;
                auto val_lines = wrap_text_words(value, val_w);
                for (const auto& vl : val_lines) {
                    ss << "  " << vl << "\n";
                }
            }
        };

        append_labeled_field("Folio:   ", folio);
        append_labeled_field("Fecha:   ", created_at);
        if (!cashier_name.empty()) {
            append_labeled_field("Cajero:  ", cashier_name);
        }
        ss << sep_single << "\n";

        {
            std::string col_cant = "Cant. ";
            std::string col_imp = " Importe";
            std::string desc_label = (max_cols >= 25) ? "Descripcion" : "Desc.";
            size_t fixed_w = utf8_column_width(col_cant) + utf8_column_width(col_imp);
            if (max_cols >= fixed_w + utf8_column_width(desc_label)) {
                size_t desc_w = max_cols - fixed_w;
                ss << col_cant << pad_right_utf8(desc_label, desc_w) << col_imp << "\n";
            } else {
                std::string short_cant = "Cant ";
                std::string short_imp = " Imp";
                size_t sw = utf8_column_width(short_cant) + utf8_column_width(short_imp);
                size_t dw = (max_cols > sw) ? (max_cols - sw) : 0;
                ss << short_cant << pad_right_utf8("Desc", dw) << short_imp << "\n";
            }
        }
        ss << sep_single << "\n";

        for (const auto& item : items) {
            std::string qty_str = std::to_string(item.quantity) + "x ";
            std::string sub_str = item.subtotal.to_string();
            size_t qty_w = utf8_column_width(qty_str);
            size_t sub_w = utf8_column_width(sub_str);

            if (max_cols >= qty_w + sub_w + 7) {
                size_t name_avail_w = max_cols - qty_w - sub_w - 1;
                auto name_lines = wrap_text_words(item.product_name, name_avail_w);
                if (name_lines.empty()) {
                    name_lines.push_back("");
                }
                std::string padded_name = pad_right_utf8(name_lines[0], name_avail_w + 1);
                ss << qty_str << padded_name << sub_str << "\n";
                for (size_t i = 1; i < name_lines.size(); ++i) {
                    ss << std::string(qty_w, ' ') << name_lines[i] << "\n";
                }
            } else {
                auto name_lines = wrap_text_words(item.product_name, max_cols);
                if (name_lines.empty()) {
                    name_lines.push_back("");
                }
                for (const auto& nl : name_lines) {
                    ss << nl << "\n";
                }
                if (max_cols >= qty_w + sub_w) {
                    size_t pad_w = max_cols - qty_w - sub_w;
                    ss << qty_str << std::string(pad_w, ' ') << sub_str << "\n";
                } else {
                    ss << qty_str << "\n";
                    if (max_cols >= sub_w) {
                        size_t pad_w = max_cols - sub_w;
                        ss << std::string(pad_w, ' ') << sub_str << "\n";
                    } else {
                        auto sub_lines = wrap_text_words(sub_str, max_cols);
                        for (const auto& sl : sub_lines) {
                            ss << sl << "\n";
                        }
                    }
                }
            }

            if (item.quantity > 1) {
                std::string up_str = "  (" + std::to_string(item.quantity) + " x " + item.unit_price.to_string() + ")";
                auto up_lines = wrap_text_words(up_str, max_cols);
                for (const auto& upl : up_lines) {
                    ss << upl << "\n";
                }
            }

            if (!item.notes.empty()) {
                std::string note_pfx = "  Nota: ";
                size_t pfx_w = utf8_column_width(note_pfx);
                size_t note_avail_w = (max_cols > pfx_w + 2) ? (max_cols - pfx_w) : max_cols;
                auto note_lines = wrap_text_words(item.notes, note_avail_w);
                if (!note_lines.empty()) {
                    if (max_cols > pfx_w + 2) {
                        ss << note_pfx << note_lines[0] << "\n";
                        for (size_t i = 1; i < note_lines.size(); ++i) {
                            ss << std::string(pfx_w, ' ') << note_lines[i] << "\n";
                        }
                    } else {
                        ss << "Nota:\n";
                        size_t sub_w = (max_cols > 2) ? (max_cols - 2) : max_cols;
                        auto sub_lines = wrap_text_words(item.notes, sub_w);
                        for (const auto& nl : sub_lines) {
                            ss << "  " << nl << "\n";
                        }
                    }
                }
            }
        }
        ss << sep_single << "\n";

        auto format_kv_line = [&](const std::string& label, const std::string& val_str) -> std::string {
            size_t label_w = utf8_column_width(label);
            size_t val_w = utf8_column_width(val_str);
            if (max_cols >= label_w + val_w) {
                size_t pad_w = max_cols - label_w - val_w;
                return label + std::string(pad_w, ' ') + val_str + "\n";
            } else {
                std::string res;
                auto lbl_lines = wrap_text_words(label, max_cols);
                for (const auto& ll : lbl_lines) res += ll + "\n";
                if (max_cols >= val_w) {
                    size_t pad_w = max_cols - val_w;
                    res += std::string(pad_w, ' ') + val_str + "\n";
                } else {
                    auto v_lines = wrap_text_words(val_str, max_cols);
                    for (const auto& vl : v_lines) res += vl + "\n";
                }
                return res;
            }
        };

        ss << format_kv_line("Subtotal:", subtotal.to_string());
        if (discount.cents() > 0) {
            ss << format_kv_line("Descuento:", discount.to_string());
        }
        ss << format_kv_line("TOTAL:", total.to_string());
        ss << sep_single << "\n";
        ss << format_kv_line("Metodo de Pago:", to_string(payment_method));
        if (payment_method == PaymentMethod::Efectivo) {
            ss << format_kv_line("Recibido:", cash_received.to_string());
            ss << format_kv_line("Cambio:", change.to_string());
        }
        ss << sep_double << "\n";

        std::string ty = "Gracias por su preferencia!";
        auto ty_lines = wrap_text_words(ty, max_cols);
        for (const auto& tyl : ty_lines) {
            size_t ty_w = utf8_column_width(tyl);
            size_t pad_total = (max_cols > ty_w) ? (max_cols - ty_w) : 0;
            size_t pad_left = pad_total / 2;
            size_t pad_right = pad_total - pad_left;
            ss << std::string(pad_left, ' ') << tyl << std::string(pad_right, ' ') << "\n";
        }
        ss << sep_double << "\n";
        return ss.str();
    }
};

struct TicketSummaryDto {
    std::string folio;
    std::string created_at;
    Money total{Money::zero()};
    std::string cashier_name;
    PaymentMethod payment_method{PaymentMethod::Efectivo};
    std::int64_t items_count{0};
};

class ISaleService {
public:
    virtual ~ISaleService() = default;

    virtual Result<SaleConfirmationDto> confirm_sale(
        PaymentMethod payment_method,
        Money cash_received,
        const std::string& idempotency_key = ""
    ) = 0;

    virtual Result<SaleConfirmationDto> get_sale_by_folio(const std::string& folio) = 0;
    virtual Result<TicketDto> get_ticket(const std::string& folio) = 0;
    virtual Result<std::vector<TicketSummaryDto>> get_recent_tickets(int limit = 50) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_SALE_SERVICE_HPP
