#ifndef APP_FRONTEND_SIDEBAR_PANEL_HPP
#define APP_FRONTEND_SIDEBAR_PANEL_HPP

#include <wx/wx.h>
#include <functional>
#include <vector>

namespace app::frontend {

enum class SectionId {
    PointOfSale = 0,
    Inventory,
    CashAndReports,
    BackOffice
};

class SidebarPanel : public wxPanel {
public:
    using OnSectionChangedCallback = std::function<void(SectionId)>;

    SidebarPanel(wxWindow* parent, OnSectionChangedCallback callback);

    void set_active_section(SectionId section);
    SectionId active_section() const { return current_section_; }
    void refresh_profile();

private:
    void on_button_click(SectionId section);
    void update_button_styles();

    OnSectionChangedCallback callback_;
    SectionId current_section_{SectionId::PointOfSale};

    wxButton* btn_pos_{nullptr};
    wxButton* btn_inventory_{nullptr};
    wxButton* btn_cash_{nullptr};
    wxButton* btn_backoffice_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_SIDEBAR_PANEL_HPP
