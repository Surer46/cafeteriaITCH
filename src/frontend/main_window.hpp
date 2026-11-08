#ifndef APP_FRONTEND_MAIN_WINDOW_HPP
#define APP_FRONTEND_MAIN_WINDOW_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <memory>
#include <string>
#include "src/frontend/header_panel.hpp"
#include "src/frontend/sidebar_panel.hpp"
#include "src/frontend/subnav_bar.hpp"
#include "app/services/mock_services.hpp"

namespace app::frontend {

class MainWindow : public wxFrame {
public:
    explicit MainWindow(const services::ServiceRegistry& services);

private:
    void setup_ui();
    void on_section_changed(SectionId section);
    void on_subsection_changed(const std::string& sub_name);
    void update_content_view();
    void update_header_status();

    wxPanel* create_placeholder_view(wxWindow* parent, const std::string& title, const std::string& desc);

    services::ServiceRegistry services_;

    HeaderPanel* header_panel_{nullptr};
    SidebarPanel* sidebar_panel_{nullptr};
    SubnavBar* subnav_bar_{nullptr};
    wxPanel* content_container_{nullptr};
    wxBoxSizer* content_sizer_{nullptr};
    wxPanel* current_view_{nullptr};

    SectionId current_section_{SectionId::PointOfSale};
    std::string current_subsection_{"Venta"};
};

} // namespace app::frontend

#endif // APP_FRONTEND_MAIN_WINDOW_HPP
