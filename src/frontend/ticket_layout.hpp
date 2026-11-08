#ifndef APP_FRONTEND_TICKET_LAYOUT_HPP
#define APP_FRONTEND_TICKET_LAYOUT_HPP

#include <wx/wx.h>
#include <wx/print.h>
#include <wx/dc.h>
#include <string>
#include <vector>
#include "app/contracts/sale_service.hpp"
#include "app/contracts/print_config_service.hpp"

namespace app::frontend {

struct PageLineRange {
    size_t start_line{0};
    size_t end_line{0}; // exclusive
};

class TicketPrintLayout {
public:
    explicit TicketPrintLayout(const contracts::TicketDto& ticket,
                               contracts::ThermalPaperProfile profile = contracts::ThermalPaperProfile::Profile80mm);

    contracts::ThermalPaperProfile get_profile() const { return profile_; }
    void set_profile(contracts::ThermalPaperProfile profile) { profile_ = profile; }

    // Calculate layout, wrapping and pagination on a DC with printable area
    void calculate_layout(wxDC& dc, const wxRect& printable_rect, int margin_x = 10, int margin_y = 10);

    // Number of pages
    int get_total_pages() const { return total_pages_; }

    // Column count calculated for the printable width
    size_t get_columns() const { return columns_; }

    // Lines per page
    int get_lines_per_page() const { return lines_per_page_; }

    // Formatted ticket lines for this layout
    const std::vector<std::string>& get_lines() const { return lines_; }

    // Range of lines for a page (1-indexed: 1 .. total_pages_)
    PageLineRange get_page_range(int page_num) const;

    // Render a single page (1-indexed: 1 .. total_pages_)
    bool draw_page(wxDC& dc, int page_num) const;

    // Metrics for testing and verification
    bool is_valid() const { return is_valid_; }
    const std::string& get_error_message() const { return error_message_; }
    int get_char_width() const { return char_w_; }
    int get_char_height() const { return char_h_; }
    int get_line_height() const { return line_height_; }
    wxRect get_printable_rect() const { return printable_rect_; }
    wxRect get_content_rect() const { return content_rect_; }
    wxRect get_footer_rect() const { return footer_rect_; }
    int get_footer_reserved_height() const { return footer_reserved_h_; }

private:
    contracts::TicketDto ticket_;
    contracts::ThermalPaperProfile profile_{contracts::ThermalPaperProfile::Profile80mm};
    bool is_valid_{false};
    std::string error_message_;
    wxRect printable_rect_;
    wxRect content_rect_;
    wxRect footer_rect_;
    int margin_x_{10};
    int margin_y_{10};
    int char_w_{8};
    int char_h_{14};
    int line_height_{16};
    int footer_reserved_h_{32};
    size_t columns_{40};
    int lines_per_page_{50};
    int total_pages_{1};
    std::vector<std::string> lines_;
    std::vector<PageLineRange> page_ranges_;
};

class TicketPrintout : public wxPrintout {
public:
    TicketPrintout(const contracts::TicketDto& ticket, const wxString& title,
                   contracts::ThermalPaperProfile profile = contracts::ThermalPaperProfile::Profile80mm);

    void OnPreparePrinting() override;
    bool OnBeginDocument(int startPage, int endPage) override;
    void GetPageInfo(int* minPage, int* maxPage, int* selPageFrom, int* selPageTo) override;
    bool HasPage(int pageNum) override;
    bool OnPrintPage(int pageNum) override;

    const TicketPrintLayout& get_layout() const { return layout_; }

private:
    void ensure_layout();

    contracts::TicketDto ticket_;
    contracts::ThermalPaperProfile profile_{contracts::ThermalPaperProfile::Profile80mm};
    TicketPrintLayout layout_;
    bool layout_done_{false};
};

} // namespace app::frontend

#endif // APP_FRONTEND_TICKET_LAYOUT_HPP
