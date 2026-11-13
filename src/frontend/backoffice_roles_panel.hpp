#ifndef APP_FRONTEND_BACKOFFICE_ROLES_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_ROLES_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include "app/contracts/account_service.hpp"

namespace app::frontend {

class CardPanel;
class ToggleSwitch;

class BackofficeRolesPanel : public wxPanel {
public:
    BackofficeRolesPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IAccountService> account_service
    );

    void refresh_roles(const std::string& select_role_id = "");

private:
    struct PermissionRow {
        contracts::PermissionDto perm;
        wxPanel* row_panel{nullptr};
        wxStaticText* lbl_title{nullptr};
        wxStaticText* lbl_desc{nullptr};
        ToggleSwitch* toggle{nullptr};
    };

    void setup_ui();
    void load_permissions();
    void build_permission_matrix();
    void update_permission_matrix_layout();
    void update_permission_count();
    void set_permissions_editable(bool editable);
    void auto_size_columns();
    void on_size(wxSizeEvent& event);
    void on_role_selected(wxListEvent& event);
    void on_role_deselected(wxListEvent& event);
    void on_new_role(wxCommandEvent& event);
    void on_save_role(wxCommandEvent& event);
    void on_delete_role(wxCommandEvent& event);
    void populate_role_details(long index);
    void reset_form_to_empty();

    std::shared_ptr<contracts::IAccountService> account_service_;

    // Componentes principales
    wxButton* btn_new_role_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxStaticText* lbl_top_status_{nullptr};
    wxPanel* pnl_warning_{nullptr};
    wxStaticText* lbl_warning_{nullptr};

    // Columna izquierda: lista de roles
    CardPanel* left_card_{nullptr};
    wxListCtrl* list_roles_{nullptr};
    wxStaticText* lbl_roles_count_{nullptr};

    // Columna derecha: formulario y permisos
    CardPanel* right_card_{nullptr};
    wxStaticText* lbl_editor_title_{nullptr};
    wxStaticText* lbl_system_notice_{nullptr};
    wxTextCtrl* txt_name_{nullptr};
    wxTextCtrl* txt_desc_{nullptr};
    wxScrolledWindow* scroll_perms_{nullptr};
    wxBoxSizer* perms_sizer_{nullptr};
    std::vector<PermissionRow> perm_rows_;
    std::map<std::string, ToggleSwitch*> perm_toggles_;
    std::vector<wxPanel*> perm_separators_;

    wxButton* btn_save_{nullptr};
    wxButton* btn_delete_{nullptr};
    wxStaticText* lbl_perm_count_{nullptr};
    wxStaticText* lbl_form_feedback_{nullptr};

    // Datos en memoria
    std::vector<contracts::RoleDto> roles_;
    std::vector<contracts::PermissionDto> all_permissions_;
    long selected_index_{-1};
    bool is_creating_new_{false};
    bool can_manage_{false};
    bool in_size_event_{false};
    bool in_matrix_layout_{false};
    int last_matrix_avail_w_{-1};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_ROLES_PANEL_HPP
