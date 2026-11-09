#ifndef APP_FRONTEND_AUDIT_LOG_PANEL_HPP
#define APP_FRONTEND_AUDIT_LOG_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <memory>
#include <string>
#include <vector>
#include "app/contracts/account_service.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

class AuditLogPanel : public wxPanel {
public:
    AuditLogPanel(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service);
    ~AuditLogPanel() override = default;

private:
    void setup_ui();
    void load_events();
    void auto_size_columns();
    void on_search_clicked(wxCommandEvent& event);
    void on_prev_page(wxCommandEvent& event);
    void on_next_page(wxCommandEvent& event);
    void on_item_selected(wxListEvent& event);
    void on_item_deselected(wxListEvent& event);
    void on_size(wxSizeEvent& event);

    std::shared_ptr<contracts::IAccountService> account_service_;

    // Componentes visuales
    CardPanel* main_card_{nullptr};
    wxPanel* pnl_warning_{nullptr};
    wxStaticText* lbl_warning_{nullptr};

    // Filtros
    wxChoice* choice_event_type_{nullptr};
    wxTextCtrl* txt_user_{nullptr};
    wxTextCtrl* txt_date_from_{nullptr};
    wxTextCtrl* txt_date_to_{nullptr};
    wxButton* btn_search_{nullptr};

    // Lista y visualizador de detalle
    wxListView* list_events_{nullptr};
    wxTextCtrl* txt_detail_{nullptr};

    // Paginación y resumen
    wxButton* btn_prev_{nullptr};
    wxButton* btn_next_{nullptr};
    wxStaticText* lbl_page_{nullptr};
    wxStaticText* lbl_count_{nullptr};

    // Estado interno
    std::size_t current_page_{1};
    static constexpr std::size_t kPageSize{100};
    std::vector<contracts::AuditEventDto> current_events_;
    bool in_size_event_{false};
};

} // namespace app::frontend

#endif // APP_FRONTEND_AUDIT_LOG_PANEL_HPP
