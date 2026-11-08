#ifndef APP_FRONTEND_SUBNAV_BAR_HPP
#define APP_FRONTEND_SUBNAV_BAR_HPP

#include <wx/wx.h>
#include <functional>
#include <vector>
#include <string>
#include "src/frontend/sidebar_panel.hpp"

namespace app::frontend {

class SubnavBar : public wxPanel {
public:
    using OnSubsectionChangedCallback = std::function<void(const std::string& sub_name)>;

    SubnavBar(wxWindow* parent, OnSubsectionChangedCallback callback);

    void set_section(SectionId section, bool can_view_dev_server = false);
    std::string active_subsection() const { return current_subsection_; }

private:
    void rebuild_buttons();
    void on_button_clicked(const std::string& name);

    OnSubsectionChangedCallback callback_;
    SectionId current_section_{SectionId::PointOfSale};
    bool can_view_dev_server_{false};
    std::string current_subsection_;
    wxBoxSizer* buttons_sizer_{nullptr};
    std::vector<wxButton*> buttons_;
};

} // namespace app::frontend

#endif // APP_FRONTEND_SUBNAV_BAR_HPP
