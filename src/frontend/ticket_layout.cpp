#include "src/frontend/ticket_layout.hpp"
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace app::frontend {

TicketPrintLayout::TicketPrintLayout(const contracts::TicketDto& ticket, contracts::ThermalPaperProfile profile)
    : ticket_(ticket), profile_(profile) {
}

PageLineRange TicketPrintLayout::get_page_range(int page_num) const {
    if (page_num < 1 || page_num > total_pages_ || page_ranges_.empty()) {
        return {0, 0};
    }
    return page_ranges_[page_num - 1];
}

void TicketPrintLayout::calculate_layout(wxDC& dc, const wxRect& printable_rect, int margin_x, int margin_y) {
    printable_rect_ = printable_rect;
    margin_x_ = margin_x;
    margin_y_ = margin_y;
    is_valid_ = false;
    error_message_.clear();
    total_pages_ = 0;
    lines_.clear();
    page_ranges_.clear();

    // Use standard teletype monospace font for receipt printing
    wxFont font(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(font);

    // Measure character dimensions in DC's current coordinate system
    wxCoord cw = 0, ch = 0;
    dc.GetTextExtent(wxT("M"), &cw, &ch);
    if (cw <= 0) cw = dc.GetCharWidth();
    if (ch <= 0) ch = dc.GetCharHeight();
    char_w_ = (cw > 0) ? cw : 8;
    char_h_ = (ch > 0) ? ch : 14;
    line_height_ = char_h_ + 2;

    if (printable_rect_.width <= 0 || printable_rect_.height <= 0) {
        error_message_ = "Dimensiones del área imprimible inválidas o nulas.";
        return;
    }

    int min_w_required = 20 * char_w_;
    if (printable_rect_.width < min_w_required) {
        error_message_ = "Ancho imprimible insuficiente (" + std::to_string(printable_rect_.width) +
                         "px): se requieren al menos " + std::to_string(min_w_required) + "px para 20 columnas.";
        return;
    }

    if (printable_rect_.height < line_height_) {
        error_message_ = "Altura imprimible insuficiente (" + std::to_string(printable_rect_.height) +
                         "px): no cabe una línea mínima (" + std::to_string(line_height_) + "px).";
        return;
    }

    // Adaptar márgenes si exceden el espacio disponible para asegurar que no agoten el área útil
    if (printable_rect_.width - 2 * margin_x_ < min_w_required) {
        margin_x_ = std::max(0, (printable_rect_.width - min_w_required) / 2);
    }
    if (printable_rect_.height - 2 * margin_y_ < line_height_) {
        margin_y_ = std::max(0, (printable_rect_.height - line_height_) / 2);
    }

    int usable_w = printable_rect_.width - 2 * margin_x_;
    int usable_h = printable_rect_.height - 2 * margin_y_;

    // Distinguir ancho nominal (55/80 mm) del área imprimible real en DC:
    // 55 mm nominal: objetivo 28 columnas (margen físico ~48 mm)
    // 80 mm nominal: objetivo 42 columnas (margen físico ~72 mm)
    size_t target_nominal_cols = (profile_ == contracts::ThermalPaperProfile::Profile55mm) ? 28 : 42;
    size_t max_cols_from_dc = static_cast<size_t>(usable_w / char_w_);
    if (max_cols_from_dc < 20) {
        error_message_ = "Ancho imprimible insuficiente (" + std::to_string(usable_w) +
                         "px): se requieren al menos " + std::to_string(min_w_required) + "px para 20 columnas.";
        return;
    }

    columns_ = std::min(target_nominal_cols, max_cols_from_dc);
    if (columns_ < 20) columns_ = 20;
    if (columns_ > 80) columns_ = 80;

    // Formatear líneas del ticket para este ancho exacto
    std::string full_text = ticket_.to_formatted_text(columns_);
    lines_.clear();
    std::istringstream stream(full_text);
    std::string l;
    while (std::getline(stream, l)) {
        lines_.push_back(l);
    }
    if (lines_.empty()) {
        lines_.push_back("");
    }

    // Medir dimensiones del pie de página para multipágina
    wxString sample_footer = wxT("--- Página 99 de 99 ---");
    wxCoord foot_w = 0, foot_h = 0;
    dc.GetTextExtent(sample_footer, &foot_w, &foot_h);
    if (foot_h <= 0) foot_h = char_h_;
    footer_reserved_h_ = foot_h + 4;

    // Caso A: Si todas las líneas caben en una sola página sin pie
    if (static_cast<int>(lines_.size()) * line_height_ <= usable_h) {
        lines_per_page_ = std::max(1, usable_h / line_height_);
        total_pages_ = 1;
        content_rect_ = wxRect(printable_rect_.x + margin_x_, printable_rect_.y + margin_y_, usable_w, usable_h);
        footer_rect_ = wxRect(0, 0, 0, 0);
        page_ranges_.push_back({0, lines_.size()});
        is_valid_ = true;
        return;
    }

    // Caso B: Multipágina requiere espacio para contenido + pie de página
    if (usable_h < line_height_ + footer_reserved_h_) {
        footer_reserved_h_ = foot_h;
        if (usable_h < line_height_ + footer_reserved_h_) {
            error_message_ = "Altura insuficiente para paginación con pie de página en ticket multipágina.";
            return;
        }
    }

    if (foot_w > usable_w) {
        wxCoord short_w = 0, short_h = 0;
        dc.GetTextExtent(wxT("Pág. 99/99"), &short_w, &short_h);
        if (short_w > usable_w) {
            error_message_ = "Ancho insuficiente para imprimir el pie de página.";
            return;
        }
    }

    int content_h = usable_h - footer_reserved_h_;
    lines_per_page_ = content_h / line_height_;
    if (lines_per_page_ < 1) {
        error_message_ = "Altura insuficiente para líneas de contenido en ticket multipágina.";
        return;
    }

    total_pages_ = static_cast<int>((lines_.size() + lines_per_page_ - 1) / lines_per_page_);
    content_rect_ = wxRect(printable_rect_.x + margin_x_, printable_rect_.y + margin_y_, usable_w, content_h);
    footer_rect_ = wxRect(
        printable_rect_.x + margin_x_,
        printable_rect_.y + printable_rect_.height - margin_y_ - footer_reserved_h_,
        usable_w,
        footer_reserved_h_
    );

    page_ranges_.resize(total_pages_);
    for (int p = 0; p < total_pages_; ++p) {
        size_t start_idx = static_cast<size_t>(p * lines_per_page_);
        size_t end_idx = std::min(start_idx + static_cast<size_t>(lines_per_page_), lines_.size());
        page_ranges_[p] = {start_idx, end_idx};
    }
    is_valid_ = true;
}

bool TicketPrintLayout::draw_page(wxDC& dc, int page_num) const {
    if (!is_valid_ || page_num < 1 || page_num > total_pages_) return false;

    // Región de recorte estricta: garantiza que nada se dibuje fuera del área imprimible útil
    dc.SetClippingRegion(printable_rect_);

    wxFont font(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(font);
    dc.SetTextForeground(*wxBLACK);

    const auto& range = page_ranges_[page_num - 1];
    int x = content_rect_.x;
    int y = content_rect_.y;

    for (size_t i = range.start_line; i < range.end_line; ++i) {
        dc.DrawText(wxString::FromUTF8(lines_[i].c_str()), x, y);
        y += line_height_;
    }

    // Dibujar pie de página en tickets multipágina dentro del rectángulo reservado
    if (total_pages_ > 1 && footer_rect_.height > 0) {
        wxString page_str = wxString::Format(wxT("--- Página %d de %d ---"), page_num, total_pages_);
        wxCoord foot_w = 0, foot_h = 0;
        dc.GetTextExtent(page_str, &foot_w, &foot_h);
        if (foot_w > footer_rect_.width) {
            page_str = wxString::Format(wxT("Pág. %d/%d"), page_num, total_pages_);
            dc.GetTextExtent(page_str, &foot_w, &foot_h);
        }
        int foot_x = footer_rect_.x + (footer_rect_.width - foot_w) / 2;
        if (foot_x < printable_rect_.x) foot_x = printable_rect_.x;
        int foot_y = footer_rect_.y + (footer_rect_.height - foot_h) / 2;
        dc.DrawText(page_str, foot_x, foot_y);
    }

    dc.DestroyClippingRegion();
    return true;
}

// -------------------------------------------------------------
// TicketPrintout Implementation
// -------------------------------------------------------------

TicketPrintout::TicketPrintout(const contracts::TicketDto& ticket, const wxString& title,
                               contracts::ThermalPaperProfile profile)
    : wxPrintout(title), ticket_(ticket), profile_(profile), layout_(ticket, profile) {
}

void TicketPrintout::ensure_layout() {
    wxDC* dc = GetDC();
    if (!dc) return;

    // Unified coordinate system: map screen size to page
    MapScreenSizeToPage();

    wxRect pageRect = GetLogicalPageRect();
    if (pageRect.width <= 0 || pageRect.height <= 0) {
        int w = 0, h = 0;
        GetPageSizePixels(&w, &h);
        if (w <= 0 || h <= 0) {
            dc->GetSize(&w, &h);
        }
        pageRect = wxRect(0, 0, w > 0 ? w : 400, h > 0 ? h : 600);
    }

    int margin_x = std::max(6, pageRect.width / 40);
    int margin_y = std::max(8, pageRect.height / 50);

    layout_.calculate_layout(*dc, pageRect, margin_x, margin_y);
    layout_done_ = true;
}

void TicketPrintout::OnPreparePrinting() {
    ensure_layout();
}

bool TicketPrintout::OnBeginDocument(int startPage, int endPage) {
#ifdef CAFETERIA_TEST_HOOKS
    const char* sim_err = std::getenv("CAFETERIA_SIMULATE_PRINT_ERROR");
    if (sim_err && (std::strcmp(sim_err, "1") == 0 || std::strcmp(sim_err, "true") == 0)) {
        return false;
    }
#endif
    ensure_layout();
    if (!layout_.is_valid()) {
        return false;
    }
    if (!wxPrintout::OnBeginDocument(startPage, endPage)) return false;
    return true;
}

void TicketPrintout::GetPageInfo(int* minPage, int* maxPage, int* selPageFrom, int* selPageTo) {
    ensure_layout();
    if (!layout_.is_valid() || layout_.get_total_pages() < 1) {
        *minPage = 0;
        *maxPage = 0;
        *selPageFrom = 0;
        *selPageTo = 0;
        return;
    }
    int tot = layout_.get_total_pages();
    *minPage = 1;
    *maxPage = tot;
    *selPageFrom = 1;
    *selPageTo = tot;
}

bool TicketPrintout::HasPage(int pageNum) {
    ensure_layout();
    if (!layout_.is_valid()) return false;
    return pageNum >= 1 && pageNum <= layout_.get_total_pages();
}

bool TicketPrintout::OnPrintPage(int pageNum) {
    wxDC* dc = GetDC();
    if (!dc) return false;

    // Use the EXACT same coordinate system as ensure_layout
    MapScreenSizeToPage();
    ensure_layout();

    if (!layout_.is_valid()) return false;
    return layout_.draw_page(*dc, pageNum);
}

} // namespace app::frontend
