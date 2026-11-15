#include "src/frontend/backoffice_backups_panel.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/statline.h>
#include <wx/stdpaths.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace app::frontend {

namespace {

int btn_height_for_profile() {
    return Theme::is_touch() ? 48 : 38;
}

int primary_btn_height_for_profile() {
    return Theme::is_touch() ? 52 : 42;
}

int field_height_for_profile() {
    return Theme::is_touch() ? 48 : 36;
}

bool parse_strict_int(const wxString& raw, int& out_value) {
    wxString trimmed = raw;
    trimmed.Trim(true).Trim(false);
    if (trimmed.empty()) {
        return false;
    }
    const std::string utf8 = trimmed.ToUTF8().data();
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(utf8[i]))) {
            return false;
        }
    }
    char* end_ptr = nullptr;
    const long val = std::strtol(utf8.c_str(), &end_ptr, 10);
    if (end_ptr == utf8.c_str() || *end_ptr != '\0') {
        return false;
    }
    if (val < 0 || val > 1000000L) {
        return false;
    }
    out_value = static_cast<int>(val);
    return true;
}

class ConfirmActionDialog final : public wxDialog {
public:
    ConfirmActionDialog(
        wxWindow* parent,
        const wxString& title,
        const wxString& heading,
        const wxString& body,
        const wxString& cancel_label,
        const wxString& confirm_label,
        bool destructive,
        const wxString& info_box_title = wxEmptyString,
        const wxString& info_box_body = wxEmptyString,
        const wxString& warning_box_text = wxEmptyString
    ) : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE) {
        SetBackgroundColour(Theme::ColorSurface);
        const bool touch = Theme::is_touch();
        const int btn_h = touch ? 48 : 38;
        const int wrap_w = FromDIP(touch ? 660 : 540);

        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* lbl_head = new wxStaticText(this, wxID_ANY, heading);
        lbl_head->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
        lbl_head->SetForegroundColour(destructive ? Theme::ColorError : Theme::ColorBrandDeep);
        lbl_head->Wrap(wrap_w);
        root->Add(lbl_head, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

        if (!info_box_body.empty()) {
            auto* info_card = new CardPanel(this, wxID_ANY);
            info_card->set_card_background(Theme::ColorSurfaceSubtle);
            info_card->set_card_border(Theme::ColorBorder, 1);
            auto* info_sizer = new wxBoxSizer(wxVERTICAL);

            if (!info_box_title.empty()) {
                auto* lbl_it = new wxStaticText(info_card, wxID_ANY, info_box_title);
                lbl_it->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
                lbl_it->SetForegroundColour(Theme::ColorText);
                info_sizer->Add(lbl_it, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
            }
            auto* lbl_ib = new wxStaticText(info_card, wxID_ANY, info_box_body);
            lbl_ib->SetFont(Theme::get_font_body());
            lbl_ib->SetForegroundColour(Theme::ColorText);
            lbl_ib->Wrap(wrap_w - FromDIP(28));
            info_sizer->Add(lbl_ib, 0, wxALL, FromDIP(12));

            info_card->SetSizer(info_sizer);
            root->Add(info_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
        }

        if (!warning_box_text.empty()) {
            auto* warn_card = new CardPanel(this, wxID_ANY);
            warn_card->set_card_background(Theme::ColorErrorSoft);
            warn_card->set_card_border(Theme::ColorError, 1);
            auto* warn_sizer = new wxBoxSizer(wxVERTICAL);

            auto* lbl_w = new wxStaticText(warn_card, wxID_ANY, warning_box_text);
            lbl_w->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
            lbl_w->SetForegroundColour(Theme::ColorError);
            lbl_w->Wrap(wrap_w - FromDIP(28));
            warn_sizer->Add(lbl_w, 0, wxALL, FromDIP(14));

            warn_card->SetSizer(warn_sizer);
            root->Add(warn_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
        }

        if (!body.empty()) {
            auto* lbl_body = new wxStaticText(this, wxID_ANY, body);
            lbl_body->SetFont(Theme::get_font_body());
            lbl_body->SetForegroundColour(Theme::ColorText);
            lbl_body->Wrap(wrap_w);
            root->Add(lbl_body, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
        }

        auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
        btn_row->AddStretchSpacer(1);

        wxButton* btn_cancel = nullptr;
        if (!cancel_label.empty()) {
            btn_cancel = new wxButton(this, wxID_CANCEL, cancel_label);
            Theme::style_button_secondary(btn_cancel, btn_h);
            Theme::setup_button_icon(btn_cancel, "cancel", wxSize(20, 20), Theme::ColorTextSecondary);
            Theme::ensure_button_min_size(btn_cancel, touch ? 140 : 120, btn_h);
            btn_row->Add(btn_cancel, 0, wxRIGHT, FromDIP(12));
        }

        auto* btn_confirm = new wxButton(this, wxID_OK, confirm_label);
        if (destructive) {
            Theme::style_button_destructive(btn_confirm, btn_h);
            Theme::setup_button_icon(btn_confirm, cancel_label.empty() ? "cancel" : "history", wxSize(20, 20), Theme::ColorSurface);
        } else {
            Theme::style_button_primary(btn_confirm, btn_h);
            Theme::setup_button_icon(btn_confirm, "check_circle", wxSize(20, 20), Theme::ColorSurface);
        }
        const int min_confirm_w = cancel_label.empty() ? (touch ? 150 : 130) : (touch ? 220 : 180);
        Theme::ensure_button_min_size(btn_confirm, min_confirm_w, btn_h);
        btn_row->Add(btn_confirm, 0);

        root->Add(btn_row, 0, wxEXPAND | wxALL, FromDIP(20));
        SetSizerAndFit(root);
        if (GetSize().GetWidth() > FromDIP(880)) {
            SetClientSize(wxSize(FromDIP(880), GetClientSize().GetHeight()));
        }
        if (btn_cancel) {
            btn_cancel->SetFocus();
        } else {
            btn_confirm->SetFocus();
        }
        CentreOnParent();
    }
};

void show_notice_dialog(
    wxWindow* parent,
    const wxString& title,
    const wxString& body,
    bool is_error
) {
    ConfirmActionDialog dlg(
        parent,
        title,
        title,
        body,
        wxEmptyString,
        wxT("Aceptar"),
        is_error
    );
    dlg.ShowModal();
}

void set_wrapped_text(wxStaticText* lbl, const wxString& raw, int max_w) {
    if (!lbl) {
        return;
    }
    lbl->SetMinSize(wxDefaultSize);
    if (max_w <= 0) {
        lbl->SetLabel(raw);
        lbl->InvalidateBestSize();
        return;
    }
    wxString out;
    const wxArrayString paragraphs = wxSplit(raw, wxT('\n'), wxT('\0'));
    for (size_t pi = 0; pi < paragraphs.size(); ++pi) {
        if (pi > 0) {
            out << wxT('\n');
        }
        wxString line;
        const wxArrayString words = wxSplit(paragraphs[pi], wxT(' '), wxT('\0'));
        for (size_t wi = 0; wi < words.size(); ++wi) {
            const wxString& w = words[wi];
            if (w.empty()) {
                continue;
            }
            const wxString cand = line.empty() ? w : (line + wxT(' ') + w);
            if (!line.empty() && lbl->GetTextExtent(cand).GetWidth() > max_w) {
                if (!out.empty() && out.Last() != wxT('\n')) {
                    out << wxT('\n');
                }
                out << line;
                line = w;
            } else {
                line = cand;
            }
        }
        if (!line.empty()) {
            if (!out.empty() && out.Last() != wxT('\n')) {
                out << wxT('\n');
            }
            out << line;
        }
    }
    lbl->SetLabel(out);
    lbl->InvalidateBestSize();
}

wxString cleanup_description_text() {
    return wxT("Revisa y borra respaldos antiguos hechos por este programa, ")
           wxT("incluso los guardados en otra carpeta o en una memoria USB, según las reglas de abajo.\n")
           wxT("Siempre conserva el respaldo más reciente de cada tipo y nunca borra otros archivos.");
}

} // namespace

BackofficeBackupsPanel::BusyScope::BusyScope(BackofficeBackupsPanel& owner)
    : owner_(owner),
      prev_focus_(wxWindow::FindFocus()) {
    owner_.op_busy_ = true;
    owner_.sync_action_states();
    owner_.Refresh();
    owner_.Update();
}

BackofficeBackupsPanel::BusyScope::~BusyScope() {
    owner_.op_busy_ = false;
    owner_.sync_action_states();
    if (prev_focus_ && prev_focus_->IsEnabled() && prev_focus_->IsShownOnScreen()) {
        prev_focus_->SetFocus();
    }
    if (owner_.keypad_ && owner_.keypad_->IsShown()) {
        owner_.keypad_->hide_keypad(false);
    }
}

BackofficeBackupsPanel::BackofficeBackupsPanel(wxWindow* parent, services::ServiceRegistry services)
    : wxPanel(parent, wxID_ANY),
      services_(std::move(services)) {
    SetBackgroundColour(Theme::ColorBackground);
    can_manage_ = services_.account_service &&
                  services_.backup_service &&
                  services_.account_service->has_permission("backup.manage");
    can_restore_ = can_manage_ &&
                   services_.account_service->has_permission("backup.restore");

    if (!can_manage_) {
        setup_access_denied_ui();
        return;
    }

    setup_ui();
    refresh_all();
}

void BackofficeBackupsPanel::setup_access_denied_ui() {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* card = new CardPanel(this, wxID_ANY);
    card->set_card_background(Theme::ColorSurface);
    auto* cs = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(card, wxID_ANY, wxT("Sin permiso para respaldos"));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    cs->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* body = new wxStaticText(
        card,
        wxID_ANY,
        wxT("Tu cuenta no tiene permiso para ver ni administrar los respaldos del sistema.\n\n")
        wxT("Solicita ayuda al Dueño si necesitas guardar o restaurar una copia de seguridad.")
    );
    body->SetFont(Theme::get_font_body());
    body->SetForegroundColour(Theme::ColorText);
    body->Wrap(FromDIP(640));
    cs->Add(body, 0, wxALL, FromDIP(20));

    card->SetSizer(cs);
    root->Add(card, 0, wxEXPAND | wxALL, FromDIP(12));
    root->AddStretchSpacer(1);
    SetSizer(root);
}

void BackofficeBackupsPanel::setup_ui() {
    const int gap_card = FromDIP(12);
    const int btn_h = btn_height_for_profile();
    const int prim_h = primary_btn_height_for_profile();
    const int field_h = field_height_for_profile();

    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    scroll_->SetBackgroundColour(Theme::ColorBackground);
    scroll_->SetScrollRate(0, FromDIP(16));

    scroll_sizer_ = new wxBoxSizer(wxVERTICAL);

    // Banner de mensajes inline
    pnl_feedback_ = new wxPanel(scroll_, wxID_ANY);
    pnl_feedback_->SetBackgroundColour(Theme::ColorSuccessSoft);
    pnl_feedback_->SetMinSize(wxSize(FromDIP(120), -1));
    auto* fb_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_feedback_ = new wxStaticText(
        pnl_feedback_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE
    );
    lbl_feedback_->SetMinSize(wxSize(FromDIP(100), -1));
    lbl_feedback_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_feedback_->SetForegroundColour(Theme::ColorSuccess);
    fb_sizer->Add(lbl_feedback_, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(12));
    pnl_feedback_->SetSizer(fb_sizer);
    pnl_feedback_->Hide();
    scroll_sizer_->Add(pnl_feedback_, 0, wxEXPAND | wxBOTTOM, gap_card);

    // =========================================================================
    // 1. Tarjeta: Estado del respaldo y respaldo automático
    // =========================================================================
    card_status_ = new CardPanel(scroll_);
    card_status_->set_card_background(Theme::ColorSurface);
    auto* status_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_st_title = new wxStaticText(card_status_, wxID_ANY, wxT("Estado del respaldo"));
    lbl_st_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_st_title->SetForegroundColour(Theme::ColorBrandDeep);
    status_sizer->Add(lbl_st_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto* st_grid = new wxFlexGridSizer(2, 2, FromDIP(8), FromDIP(14));
    st_grid->AddGrowableCol(1, 1);

    auto* lbl_k_date = new wxStaticText(card_status_, wxID_ANY, wxT("Último respaldo:"));
    lbl_k_date->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_k_date->SetForegroundColour(Theme::ColorTextSecondary);
    st_grid->Add(lbl_k_date, 0, wxALIGN_CENTER_VERTICAL);

    lbl_last_date_ = new wxStaticText(card_status_, wxID_ANY, wxT("Nunca"));
    lbl_last_date_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_last_date_->SetForegroundColour(Theme::ColorText);
    st_grid->Add(lbl_last_date_, 0, wxALIGN_CENTER_VERTICAL);

    auto* lbl_k_res = new wxStaticText(card_status_, wxID_ANY, wxT("Resultado:"));
    lbl_k_res->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_k_res->SetForegroundColour(Theme::ColorTextSecondary);
    st_grid->Add(lbl_k_res, 0, wxALIGN_CENTER_VERTICAL);

    lbl_last_result_ = new wxStaticText(card_status_, wxID_ANY, wxT("Sin respaldos aún"));
    lbl_last_result_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_last_result_->SetForegroundColour(Theme::ColorText);
    st_grid->Add(lbl_last_result_, 0, wxALIGN_CENTER_VERTICAL);

    status_sizer->Add(st_grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    pnl_last_error_ = new wxPanel(card_status_, wxID_ANY);
    pnl_last_error_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* err_box = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_err_prefix = new wxStaticText(pnl_last_error_, wxID_ANY, wxT("Motivo del fallo:"));
    lbl_err_prefix->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_err_prefix->SetForegroundColour(Theme::ColorError);
    err_box->Add(lbl_err_prefix, 0, wxLEFT | wxTOP | wxBOTTOM, FromDIP(10));

    lbl_last_error_ = new wxStaticText(pnl_last_error_, wxID_ANY, wxEmptyString);
    lbl_last_error_->SetFont(Theme::get_font_body());
    lbl_last_error_->SetForegroundColour(Theme::ColorError);
    err_box->Add(lbl_last_error_, 1, wxALL, FromDIP(10));
    pnl_last_error_->SetSizer(err_box);
    pnl_last_error_->Hide();
    status_sizer->Add(pnl_last_error_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    status_sizer->Add(new wxStaticLine(card_status_), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    auto* auto_row = new wxBoxSizer(wxHORIZONTAL);
    toggle_auto_backup_ = new ToggleSwitch(
        card_status_, wxID_ANY, true, wxT("Respaldo automático al cerrar turno")
    );
    toggle_auto_backup_->Bind(wxEVT_CHECKBOX, &BackofficeBackupsPanel::on_toggle_auto_backup, this);
    auto_row->Add(toggle_auto_backup_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));

    auto* auto_text_col = new wxBoxSizer(wxVERTICAL);
    auto* auto_head_row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_auto_title = new wxStaticText(
        card_status_, wxID_ANY, wxT("Respaldo automático al cerrar turno")
    );
    lbl_auto_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_auto_title->SetForegroundColour(Theme::ColorText);
    auto_head_row->Add(lbl_auto_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    lbl_auto_state_ = new wxStaticText(card_status_, wxID_ANY, wxT("Activado"));
    lbl_auto_state_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_auto_state_->SetForegroundColour(Theme::ColorSuccess);
    auto_head_row->Add(lbl_auto_state_, 0, wxALIGN_CENTER_VERTICAL);
    auto_text_col->Add(auto_head_row, 0, wxEXPAND);

    lbl_auto_desc_ = new wxStaticText(
        card_status_,
        wxID_ANY,
        wxT("Guarda una copia automática al cerrar el turno de caja.")
    );
    lbl_auto_desc_->SetFont(Theme::get_font_body());
    lbl_auto_desc_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_auto_desc_->Wrap(FromDIP(560));
    auto_text_col->Add(lbl_auto_desc_, 0, wxEXPAND | wxTOP, FromDIP(4));

    auto_row->Add(auto_text_col, 1, wxALIGN_CENTER_VERTICAL);
    status_sizer->Add(auto_row, 0, wxEXPAND | wxALL, FromDIP(16));

    card_status_->SetSizer(status_sizer);
    scroll_sizer_->Add(card_status_, 0, wxEXPAND | wxBOTTOM, gap_card);

    // =========================================================================
    // 2 y 3. Fila horizontal: Hacer respaldo ahora | Restaurar desde un respaldo
    // =========================================================================
    auto* actions_row = new wxBoxSizer(wxHORIZONTAL);

    card_manual_ = new CardPanel(scroll_);
    card_manual_->set_card_background(Theme::ColorSurface);
    auto* manual_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_man_title = new wxStaticText(card_manual_, wxID_ANY, wxT("Hacer respaldo ahora"));
    lbl_man_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_man_title->SetForegroundColour(Theme::ColorBrandDeep);
    manual_sizer->Add(lbl_man_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    lbl_manual_desc_ = new wxStaticText(
        card_manual_,
        wxID_ANY,
        wxT("Guarda una copia completa de ventas, inventario y cuentas en una carpeta o memoria USB.")
    );
    lbl_manual_desc_->SetFont(Theme::get_font_body());
    lbl_manual_desc_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_manual_desc_->Wrap(FromDIP(340));
    manual_sizer->Add(lbl_manual_desc_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    manual_sizer->AddStretchSpacer(1);

    btn_backup_now_ = new wxButton(card_manual_, wxID_ANY, wxT("Hacer un respaldo ahora"));
    Theme::style_button_primary(btn_backup_now_, prim_h);
    Theme::setup_button_icon(btn_backup_now_, "inventory_2", wxSize(20, 20), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_backup_now_, 240, prim_h);
    btn_backup_now_->Bind(wxEVT_BUTTON, &BackofficeBackupsPanel::on_backup_now, this);
    manual_sizer->Add(btn_backup_now_, 0, wxALL, FromDIP(14));

    card_manual_->SetSizer(manual_sizer);
    actions_row->Add(card_manual_, 1, wxEXPAND | wxRIGHT, gap_card);

    card_restore_ = new CardPanel(scroll_);
    card_restore_->set_card_background(Theme::ColorSurface);
    auto* restore_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_res_title = new wxStaticText(card_restore_, wxID_ANY, wxT("Restaurar desde un respaldo"));
    lbl_res_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_res_title->SetForegroundColour(Theme::ColorBrandDeep);
    restore_sizer->Add(lbl_res_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    lbl_restore_desc_ = new wxStaticText(
        card_restore_,
        wxID_ANY,
        wxT("Recupera la información de la cafetería desde un archivo de respaldo guardado antes.")
    );
    lbl_restore_desc_->SetFont(Theme::get_font_body());
    lbl_restore_desc_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_restore_desc_->Wrap(FromDIP(340));
    restore_sizer->Add(lbl_restore_desc_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    restore_sizer->AddStretchSpacer(1);

    if (can_restore_) {
        btn_restore_ = new wxButton(card_restore_, wxID_ANY, wxT("Restaurar desde un respaldo"));
        Theme::style_button_secondary(btn_restore_, prim_h);
        Theme::setup_button_icon(btn_restore_, "history", wxSize(20, 20), Theme::ColorTextSecondary);
        Theme::ensure_button_min_size(btn_restore_, 250, prim_h);
        btn_restore_->Bind(wxEVT_BUTTON, &BackofficeBackupsPanel::on_restore_backup, this);
        restore_sizer->Add(btn_restore_, 0, wxALL, FromDIP(14));
    } else {
        lbl_restore_restricted_ = new wxStaticText(
            card_restore_,
            wxID_ANY,
            wxT("Solo el Dueño puede restaurar respaldos en el sistema.")
        );
        lbl_restore_restricted_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        lbl_restore_restricted_->SetForegroundColour(Theme::ColorText);
        lbl_restore_restricted_->Wrap(FromDIP(340));
        restore_sizer->Add(lbl_restore_restricted_, 0, wxEXPAND | wxALL, FromDIP(14));
    }

    card_restore_->SetSizer(restore_sizer);
    actions_row->Add(card_restore_, 1, wxEXPAND);
    scroll_sizer_->Add(actions_row, 0, wxEXPAND | wxBOTTOM, gap_card);

    // =========================================================================
    // 4. Tarjeta: Limpieza de respaldos antiguos
    // =========================================================================
    card_cleanup_ = new CardPanel(scroll_);
    card_cleanup_->set_card_background(Theme::ColorSurface);
    auto* cleanup_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_cl_title = new wxStaticText(card_cleanup_, wxID_ANY, wxT("Limpieza de respaldos antiguos"));
    lbl_cl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_cl_title->SetForegroundColour(Theme::ColorBrandDeep);
    cleanup_sizer->Add(lbl_cl_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_cleanup_desc_ = new wxStaticText(
        card_cleanup_,
        wxID_ANY,
        cleanup_description_text()
    );
    lbl_cleanup_desc_->SetFont(Theme::get_font_body());
    lbl_cleanup_desc_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_cleanup_desc_->Wrap(FromDIP(640));
    cleanup_sizer->Add(lbl_cleanup_desc_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto* policy_row = new wxBoxSizer(wxHORIZONTAL);

    auto* col_n = new wxBoxSizer(wxVERTICAL);
    auto* lbl_n = new wxStaticText(card_cleanup_, wxID_ANY, wxT("Conservar al menos (respaldos):"));
    lbl_n->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_n->SetForegroundColour(Theme::ColorText);
    col_n->Add(lbl_n, 0, wxBOTTOM, FromDIP(4));
    box_keep_last_ = ui::make_text_field(
        card_cleanup_, txt_keep_last_, field_h, 110, Theme::get_font_body(wxFONTWEIGHT_BOLD)
    );
    txt_keep_last_->Bind(wxEVT_TEXT, &BackofficeBackupsPanel::on_policy_field_edited, this);
    col_n->Add(box_keep_last_, 0);
    policy_row->Add(col_n, 0, wxALIGN_BOTTOM | wxRIGHT, FromDIP(16));

    auto* col_d = new wxBoxSizer(wxVERTICAL);
    auto* lbl_d = new wxStaticText(card_cleanup_, wxID_ANY, wxT("Y los de los últimos (días):"));
    lbl_d->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_d->SetForegroundColour(Theme::ColorText);
    col_d->Add(lbl_d, 0, wxBOTTOM, FromDIP(4));
    box_keep_days_ = ui::make_text_field(
        card_cleanup_, txt_keep_days_, field_h, 110, Theme::get_font_body(wxFONTWEIGHT_BOLD)
    );
    txt_keep_days_->Bind(wxEVT_TEXT, &BackofficeBackupsPanel::on_policy_field_edited, this);
    col_d->Add(box_keep_days_, 0);
    policy_row->Add(col_d, 0, wxALIGN_BOTTOM | wxRIGHT, FromDIP(16));

    btn_save_policy_ = new wxButton(card_cleanup_, wxID_ANY, wxT("Guardar"));
    Theme::style_button_primary(btn_save_policy_, btn_h);
    Theme::setup_button_icon(btn_save_policy_, "check_circle", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_save_policy_, 120, btn_h);
    btn_save_policy_->Bind(wxEVT_BUTTON, &BackofficeBackupsPanel::on_save_policy, this);
    policy_row->Add(btn_save_policy_, 0, wxALIGN_BOTTOM);

    cleanup_sizer->Add(policy_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto* cleanup_btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_preview_cleanup_ = new wxButton(card_cleanup_, wxID_ANY, wxT("Revisar qué se borraría"));
    Theme::style_button_secondary(btn_preview_cleanup_, btn_h);
    Theme::setup_button_icon(btn_preview_cleanup_, "search", wxSize(18, 18), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_preview_cleanup_, 220, btn_h);
    btn_preview_cleanup_->Bind(wxEVT_BUTTON, &BackofficeBackupsPanel::on_preview_cleanup, this);
    cleanup_btn_row->Add(btn_preview_cleanup_, 0, wxRIGHT, FromDIP(12));

    btn_execute_cleanup_ = new wxButton(card_cleanup_, wxID_ANY, wxT("Borrar respaldos antiguos"));
    Theme::style_button_destructive(btn_execute_cleanup_, btn_h);
    Theme::setup_button_icon(btn_execute_cleanup_, "delete", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_execute_cleanup_, 220, btn_h);
    btn_execute_cleanup_->Enable(false);
    btn_execute_cleanup_->Bind(wxEVT_BUTTON, &BackofficeBackupsPanel::on_execute_cleanup, this);
    cleanup_btn_row->Add(btn_execute_cleanup_, 0);

    cleanup_sizer->Add(cleanup_btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_cleanup_summary_ = new wxStaticText(
        card_cleanup_,
        wxID_ANY,
        wxT("Pulsa \"Revisar qué se borraría\" para ver qué copias antiguas se pueden eliminar.")
    );
    lbl_cleanup_summary_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_cleanup_summary_->SetForegroundColour(Theme::ColorText);
    lbl_cleanup_summary_->Wrap(FromDIP(640));
    cleanup_sizer->Add(lbl_cleanup_summary_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    list_cleanup_ = new wxListCtrl(
        card_cleanup_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    list_cleanup_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        ui::apply_touch_row_height(list_cleanup_);
    }
    list_cleanup_->InsertColumn(0, wxT("Archivo"), wxLIST_FORMAT_LEFT, FromDIP(185));
    list_cleanup_->InsertColumn(1, wxT("Carpeta"), wxLIST_FORMAT_LEFT, FromDIP(210));
    list_cleanup_->InsertColumn(2, wxT("Fecha"), wxLIST_FORMAT_LEFT, FromDIP(155));
    list_cleanup_->InsertColumn(3, wxT("Tamaño (MB)"), wxLIST_FORMAT_RIGHT, FromDIP(110));
    list_cleanup_->SetMinSize(wxSize(-1, FromDIP(Theme::is_touch() ? 180 : 140)));
    list_cleanup_->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        auto_size_cleanup_columns();
    });
    cleanup_sizer->Add(list_cleanup_, 0, wxEXPAND | wxALL, FromDIP(16));

    card_cleanup_->SetSizer(cleanup_sizer);
    scroll_sizer_->Add(card_cleanup_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    scroll_->SetSizer(scroll_sizer_);
    scroll_->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        update_scroll_layout();
    });

    root_sizer->Add(scroll_, 1, wxEXPAND);
    SetSizer(root_sizer);

    // Teclado numérico flotante en Táctil para los campos enteros N y D
    if (Theme::is_touch()) {
        keypad_ = new ui::NumericKeypad(this);
        auto protected_fn = [this]() {
            std::vector<wxRect> rects;
            auto push_ctrl = [this, &rects](wxWindow* w) {
                if (w && w->IsShownOnScreen()) {
                    rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
                }
            };
            push_ctrl(box_keep_last_);
            push_ctrl(box_keep_days_);
            push_ctrl(btn_save_policy_);
            push_ctrl(btn_preview_cleanup_);
            push_ctrl(btn_execute_cleanup_);
            return rects;
        };
        keypad_->bind_field(txt_keep_last_, box_keep_last_, false, card_cleanup_, protected_fn);
        keypad_->bind_field(txt_keep_days_, box_keep_days_, false, card_cleanup_, protected_fn);
        scroll_->Bind(wxEVT_SCROLLWIN_THUMBTRACK, [this](wxScrollWinEvent& ev) {
            ev.Skip();
            CallAfter([this]() {
                if (keypad_ && keypad_->IsShown()) {
                    keypad_->reposition();
                }
            });
        });
        scroll_->Bind(wxEVT_SCROLLWIN_LINEUP, [this](wxScrollWinEvent& ev) {
            ev.Skip();
            CallAfter([this]() {
                if (keypad_ && keypad_->IsShown()) {
                    keypad_->reposition();
                }
            });
        });
        scroll_->Bind(wxEVT_SCROLLWIN_LINEDOWN, [this](wxScrollWinEvent& ev) {
            ev.Skip();
            CallAfter([this]() {
                if (keypad_ && keypad_->IsShown()) {
                    keypad_->reposition();
                }
            });
        });
    }

    update_scroll_layout();
}

void BackofficeBackupsPanel::update_scroll_layout() {
    if (!scroll_ || !scroll_sizer_) {
        return;
    }
    const wxSize client = scroll_->GetClientSize();
    const int avail_w = client.GetWidth();
    if (avail_w > FromDIP(240)) {
        const int wrap_w = std::max(FromDIP(200), avail_w - FromDIP(56));
        const int half_wrap = std::max(FromDIP(180), (avail_w - FromDIP(12)) / 2 - FromDIP(44));
        if (lbl_feedback_ && pnl_feedback_ && pnl_feedback_->IsShown()) {
            lbl_feedback_->SetMaxSize(wxSize(std::max(FromDIP(180), avail_w - FromDIP(40)), -1));
            pnl_feedback_->InvalidateBestSize();
            pnl_feedback_->Layout();
        }
        if (lbl_last_error_ && pnl_last_error_ && pnl_last_error_->IsShown()) {
            set_wrapped_text(lbl_last_error_, last_error_text_, std::max(FromDIP(180), wrap_w - FromDIP(160)));
            pnl_last_error_->InvalidateBestSize();
            pnl_last_error_->Layout();
        }
        set_wrapped_text(
            lbl_auto_desc_,
            wxT("Guarda una copia automática al cerrar el turno de caja."),
            std::max(FromDIP(180), wrap_w - FromDIP(90))
        );
        set_wrapped_text(
            lbl_manual_desc_,
            wxT("Guarda una copia completa de ventas, inventario y cuentas en una carpeta o memoria USB."),
            half_wrap
        );
        set_wrapped_text(
            lbl_restore_desc_,
            wxT("Recupera la información de la cafetería desde un archivo de respaldo guardado antes."),
            half_wrap
        );
        set_wrapped_text(
            lbl_restore_restricted_,
            wxT("Solo el Dueño puede restaurar respaldos en el sistema."),
            half_wrap
        );
        set_wrapped_text(
            lbl_cleanup_desc_,
            cleanup_description_text(),
            wrap_w
        );
        if (!cleanup_summary_text_.empty()) {
            set_wrapped_text(lbl_cleanup_summary_, cleanup_summary_text_, wrap_w);
        }
        if (card_status_) {
            card_status_->InvalidateBestSize();
            card_status_->Layout();
        }
        if (card_manual_) {
            card_manual_->InvalidateBestSize();
            card_manual_->Layout();
        }
        if (card_restore_) {
            card_restore_->InvalidateBestSize();
            card_restore_->Layout();
        }
        if (card_cleanup_) {
            card_cleanup_->InvalidateBestSize();
            card_cleanup_->Layout();
        }
    }

    const wxSize min_sz = scroll_sizer_->GetMinSize();
    const int virt_w = std::max(client.GetWidth(), 0);
    const int virt_h = std::max(client.GetHeight(), min_sz.GetHeight());
    scroll_->SetVirtualSize(virt_w, min_sz.GetHeight());
    scroll_sizer_->SetDimension(scroll_->CalcScrolledPosition(wxPoint(0, 0)), wxSize(virt_w, virt_h));
    scroll_sizer_->Layout();
}

void BackofficeBackupsPanel::refresh_all() {
    refresh_status();
    load_retention_policy();
    reset_cleanup_preview();
    sync_action_states();
    update_scroll_layout();
}

void BackofficeBackupsPanel::refresh_status() {
    if (!services_.backup_service) {
        return;
    }
    auto res = services_.backup_service->get_status();
    if (!res.is_success()) {
        show_feedback(FeedbackKind::Error, wxString::FromUTF8(res.error_message().c_str()));
        return;
    }

    const auto& st = res.value();
    lbl_last_date_->SetLabel(format_human_datetime(st.last_backup_time));

    if (st.last_status == "EXITOSA") {
        lbl_last_result_->SetLabel(wxT("Correcto"));
        lbl_last_result_->SetForegroundColour(Theme::ColorSuccess);
        pnl_last_error_->Hide();
    } else if (st.last_status == "FALLIDA") {
        lbl_last_result_->SetLabel(wxT("Falló"));
        lbl_last_result_->SetForegroundColour(Theme::ColorError);
        if (!st.last_error.empty()) {
            last_error_text_ = wxString::FromUTF8(st.last_error.c_str());
            lbl_last_error_->SetLabel(last_error_text_);
            pnl_last_error_->Show();
        } else {
            last_error_text_.clear();
            pnl_last_error_->Hide();
        }
    } else {
        lbl_last_result_->SetLabel(wxT("Sin respaldos aún"));
        lbl_last_result_->SetForegroundColour(Theme::ColorTextSecondary);
        pnl_last_error_->Hide();
    }

    syncing_toggle_ = true;
    toggle_auto_backup_->SetValue(st.is_enabled);
    syncing_toggle_ = false;
    lbl_auto_state_->SetLabel(st.is_enabled ? wxT("Activado") : wxT("Desactivado"));
    lbl_auto_state_->SetForegroundColour(st.is_enabled ? Theme::ColorSuccess : Theme::ColorTextSecondary);

    card_status_->Layout();
    update_scroll_layout();
}

void BackofficeBackupsPanel::load_retention_policy() {
    if (!services_.backup_service) {
        return;
    }
    auto res = services_.backup_service->get_retention_policy();
    if (!res.is_success()) {
        show_feedback(FeedbackKind::Error, wxString::FromUTF8(res.error_message().c_str()));
        return;
    }
    loading_policy_ = true;
    txt_keep_last_->SetValue(wxString::Format(wxT("%d"), res.value().keep_last));
    txt_keep_days_->SetValue(wxString::Format(wxT("%d"), res.value().keep_days));
    loading_policy_ = false;
}

void BackofficeBackupsPanel::reset_cleanup_preview() {
    preview_ready_ = false;
    preview_delete_count_ = 0;
    preview_bytes_freed_ = 0;
    if (list_cleanup_) {
        list_cleanup_->DeleteAllItems();
    }
    if (lbl_cleanup_summary_) {
        cleanup_summary_text_ = wxT("Pulsa \"Revisar qué se borraría\" para ver qué copias antiguas se pueden eliminar.");
        lbl_cleanup_summary_->SetLabel(cleanup_summary_text_);
    }
    sync_action_states();
    update_scroll_layout();
}

void BackofficeBackupsPanel::auto_size_cleanup_columns() {
    if (!list_cleanup_ || list_cleanup_->GetColumnCount() < 4) {
        return;
    }
    int w_file = list_cleanup_->GetTextExtent(wxT("cafeteria-respaldo-2026-11-14-2359.db")).GetWidth();
    int w_dir = list_cleanup_->GetTextExtent(wxT("C:\\Respaldos\\Cafeteria")).GetWidth();
    int w_date = list_cleanup_->GetTextExtent(wxT("14/11/2026 23:59:59")).GetWidth();
    int w_size = list_cleanup_->GetTextExtent(wxT("999.99 MB")).GetWidth();

    const int header_dir = list_cleanup_->GetTextExtent(wxT("Carpeta")).GetWidth();
    w_dir = std::max(w_dir, header_dir);

    const int n = list_cleanup_->GetItemCount();
    for (int i = 0; i < n; ++i) {
        w_file = std::max(w_file, list_cleanup_->GetTextExtent(list_cleanup_->GetItemText(i, 0)).GetWidth());
        w_dir = std::max(w_dir, list_cleanup_->GetTextExtent(list_cleanup_->GetItemText(i, 1)).GetWidth());
        w_date = std::max(w_date, list_cleanup_->GetTextExtent(list_cleanup_->GetItemText(i, 2)).GetWidth());
        w_size = std::max(w_size, list_cleanup_->GetTextExtent(list_cleanup_->GetItemText(i, 3)).GetWidth());
    }

    const int touch_extra = Theme::is_touch() ? 10 : 0;
    const std::vector<ui::ColumnSpec> specs = {
        {wxT("Archivo"), w_file, list_cleanup_->GetTextExtent(wxT("archivo.db")).GetWidth(), 16, touch_extra},
        {wxT("Carpeta"), w_dir, header_dir, 16, touch_extra},
        {wxT("Fecha"), w_date, w_date, 16, touch_extra},
        {wxT("Tamaño (MB)"), w_size, w_size, 16, touch_extra},
    };
    ui::distribute_list_columns(list_cleanup_, specs, 1);
}

void BackofficeBackupsPanel::sync_action_states() {
    const bool enable_common = can_manage_ && !op_busy_;
    if (txt_keep_last_) txt_keep_last_->Enable(enable_common);
    if (txt_keep_days_) txt_keep_days_->Enable(enable_common);
    if (toggle_auto_backup_) toggle_auto_backup_->Enable(enable_common);
    if (btn_backup_now_) btn_backup_now_->Enable(enable_common);
    if (btn_restore_) btn_restore_->Enable(enable_common && can_restore_);
    if (btn_save_policy_) btn_save_policy_->Enable(enable_common);
    if (btn_preview_cleanup_) btn_preview_cleanup_->Enable(enable_common);
    if (btn_execute_cleanup_) {
        btn_execute_cleanup_->Enable(enable_common && preview_ready_ && preview_delete_count_ > 0);
    }
}

void BackofficeBackupsPanel::show_feedback(FeedbackKind kind, const wxString& message) {
    if (!pnl_feedback_ || !lbl_feedback_) {
        return;
    }
    if (kind == FeedbackKind::Success) {
        pnl_feedback_->SetBackgroundColour(Theme::ColorSuccessSoft);
        lbl_feedback_->SetForegroundColour(Theme::ColorSuccess);
    } else if (kind == FeedbackKind::Error) {
        pnl_feedback_->SetBackgroundColour(Theme::ColorErrorSoft);
        lbl_feedback_->SetForegroundColour(Theme::ColorError);
    } else {
        pnl_feedback_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
        lbl_feedback_->SetForegroundColour(Theme::ColorText);
    }
    lbl_feedback_->SetLabel(message);
    pnl_feedback_->Show();
    pnl_feedback_->Layout();
    update_scroll_layout();
}

void BackofficeBackupsPanel::hide_feedback() {
    if (pnl_feedback_) {
        pnl_feedback_->Hide();
        update_scroll_layout();
    }
}

void BackofficeBackupsPanel::on_toggle_auto_backup(wxCommandEvent& evt) {
    if (syncing_toggle_ || op_busy_ || !services_.backup_service) {
        return;
    }
    const bool next_enabled = evt.IsChecked();
    auto res = services_.backup_service->set_backup_enabled(next_enabled);
    if (!res.is_success()) {
        syncing_toggle_ = true;
        toggle_auto_backup_->SetValue(!next_enabled);
        syncing_toggle_ = false;
        const wxString err = wxString::FromUTF8(res.error_message().c_str());
        show_feedback(FeedbackKind::Error, err);
        show_notice_dialog(this, wxT("No se pudo cambiar el respaldo automático"), err, true);
        return;
    }
    refresh_status();
    show_feedback(
        FeedbackKind::Success,
        next_enabled
            ? wxT("Respaldo automático al cerrar turno activado.")
            : wxT("Respaldo automático al cerrar turno desactivado.")
    );
}

void BackofficeBackupsPanel::on_backup_now(wxCommandEvent&) {
    if (op_busy_ || !services_.backup_service) {
        return;
    }
    if (keypad_ && keypad_->IsShown()) {
        keypad_->hide_keypad(false);
    }

    {
        BusyScope busy(*this);
        wxString default_dir = last_backup_dir_;
        if (default_dir.empty() || !wxDirExists(default_dir)) {
            default_dir = wxStandardPaths::Get().GetDocumentsDir();
        }
        const wxString proposed_name = wxDateTime::Now().Format(wxT("cafeteria-respaldo-%Y-%m-%d-%H%M.db"));

        wxFileDialog save_dlg(
            this,
            wxT("Guardar respaldo de la cafetería"),
            default_dir,
            proposed_name,
            wxT("Respaldo de base de datos (*.db)|*.db"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        if (save_dlg.ShowModal() != wxID_OK) {
            return;
        }

        last_backup_dir_ = save_dlg.GetDirectory();
        wxString dest_path = save_dlg.GetPath();
        if (wxFileName(dest_path).GetExt().empty()) {
            dest_path += wxT(".db");
        }

        show_feedback(FeedbackKind::Info, wxT("Guardando copia de respaldo, espere un momento..."));
        const auto res = services_.backup_service->trigger_local_backup(std::string(dest_path.ToUTF8().data()));

        refresh_status();

        if (res.is_success()) {
            const wxString ok_msg = wxString::Format(
                wxT("El respaldo se guardó correctamente en:\n\n%s"),
                dest_path
            );
            show_feedback(FeedbackKind::Success, wxT("Respaldo guardado en: ") + dest_path);
            show_notice_dialog(this, wxT("Respaldo guardado"), ok_msg, false);
        } else {
            const wxString err_text = wxString::FromUTF8(res.error_message().c_str());
            show_feedback(FeedbackKind::Error, wxT("No se pudo guardar el respaldo: ") + err_text);
            show_notice_dialog(
                this,
                wxT("No se pudo hacer el respaldo"),
                wxT("No se pudo guardar el respaldo.\n\n") + err_text,
                true
            );
        }
    }
    if (btn_backup_now_) {
        btn_backup_now_->SetFocus();
    }
    if (keypad_ && keypad_->IsShown()) {
        keypad_->hide_keypad(false);
    }
}

void BackofficeBackupsPanel::on_restore_backup(wxCommandEvent&) {
    if (op_busy_ || !can_restore_ || !services_.backup_service) {
        return;
    }
    if (keypad_ && keypad_->IsShown()) {
        keypad_->hide_keypad(false);
    }

    bool do_relogin = false;
    {
        BusyScope busy(*this);
        wxString default_dir = last_backup_dir_;
        if (default_dir.empty() || !wxDirExists(default_dir)) {
            default_dir = wxStandardPaths::Get().GetDocumentsDir();
        }

        wxFileDialog open_dlg(
            this,
            wxT("Elegir archivo de respaldo"),
            default_dir,
            wxEmptyString,
            wxT("Respaldo de base de datos (*.db)|*.db"),
            wxFD_OPEN | wxFD_FILE_MUST_EXIST
        );
        if (open_dlg.ShowModal() != wxID_OK) {
            return;
        }

        last_backup_dir_ = open_dlg.GetDirectory();
        const wxString src_path = open_dlg.GetPath();
        const std::string src_utf8(src_path.ToUTF8().data());

        show_feedback(FeedbackKind::Info, wxT("Revisando archivo de respaldo..."));
        const auto verify_res = services_.backup_service->verify_backup(src_utf8);

        if (!verify_res.is_success()) {
            const wxString err = wxString::FromUTF8(verify_res.error_message().c_str());
            show_feedback(FeedbackKind::Error, err);
            show_notice_dialog(
                this,
                wxT("Archivo no válido"),
                wxT("No se pudo revisar el archivo seleccionado.\n\n") + err,
                true
            );
            return;
        }

        const auto& info = verify_res.value();
        if (!info.integrity_ok || !info.detail.empty()) {
            const wxString detail = info.detail.empty()
                ? wxT("El archivo elegido no es un respaldo válido o está dañado.")
                : wxString::FromUTF8(info.detail.c_str());
            show_feedback(FeedbackKind::Error, detail);
            show_notice_dialog(
                this,
                wxT("Respaldo inválido o dañado"),
                wxT("Este archivo no se puede usar para restaurar.\n\n") + detail,
                true
            );
            return;
        }

        const wxString file_date = info.file_modified_at.empty()
            ? wxT("No disponible")
            : format_human_datetime(info.file_modified_at);

        const wxString info_body = wxString::Format(
            wxT("Archivo: %s\nFecha del respaldo: %s\nEstado: Archivo íntegro y verificable"),
            wxFileName(src_path).GetFullName(),
            file_date
        );

        const wxString warning_text = wxString::Format(
            wxT("Al restaurar este respaldo se reemplazarán todos los datos actuales ")
            wxT("(ventas, inventario, turnos y usuarios).\n")
            wxT("Se perderá todo lo que se vendió o cambió después de la fecha del respaldo (%s).\n")
            wxT("Antes de aplicar el cambio se guardará una copia de seguridad de los datos actuales ")
            wxT("en la carpeta de respaldos de la carpeta de datos.\n")
            wxT("Al terminar, la aplicación se cerrará para que vuelvas a iniciar sesión."),
            file_date
        );

        ConfirmActionDialog step1_dlg(
            this,
            wxT("Revisar respaldo antes de restaurar"),
            wxT("Respaldo verificado"),
            wxEmptyString,
            wxT("Cancelar"),
            wxT("Continuar con restauración"),
            false,
            wxT("Datos del archivo seleccionado"),
            info_body,
            warning_text
        );
        if (step1_dlg.ShowModal() != wxID_OK) {
            hide_feedback();
            return;
        }

        // Segunda confirmación explícita (botón rojo) sin requerir teclado alfabético
        ConfirmActionDialog step2_dlg(
            this,
            wxT("Confirmación final"),
            wxT("¿Reemplazar los datos actuales?"),
            wxT("Esta acción reemplazará toda la información de la cafetería con el respaldo elegido ")
            wxT("y cerrará el programa al terminar para iniciar sesión nuevamente."),
            wxT("Cancelar"),
            wxT("Sí, reemplazar y restaurar"),
            true
        );
        if (step2_dlg.ShowModal() != wxID_OK) {
            hide_feedback();
            return;
        }

        show_feedback(FeedbackKind::Info, wxT("Restaurando respaldo, por favor espere..."));
        const auto restore_res = services_.backup_service->restore_from_backup(src_utf8);

        if (!restore_res.is_success()) {
            const wxString err = wxString::FromUTF8(restore_res.error_message().c_str());
            show_feedback(FeedbackKind::Error, err);
            show_notice_dialog(
                this,
                wxT("Restauración no aplicada"),
                wxT("No se pudo restaurar el respaldo.\n\n") + err +
                wxT("\n\nLos datos actuales siguen intactos."),
                true
            );
            return;
        }

        const auto& dto = restore_res.value();
        const wxString pre_path = wxString::FromUTF8(dto.pre_restore_backup_path.c_str());
        const wxString done_msg = wxString::Format(
            wxT("La restauración se completó correctamente.\n\n")
            wxT("Se guardó una copia de seguridad de los datos anteriores en:\n%s\n\n")
            wxT("La aplicación se cerrará ahora para que vuelvas a iniciar sesión."),
            pre_path
        );
        show_notice_dialog(this, wxT("Restauración completada"), done_msg, false);
        do_relogin = dto.requires_relogin;
    }

    if (do_relogin) {
        if (wxWindow* top = wxGetTopLevelParent(this)) {
            top->Close(true);
        }
    }
}

void BackofficeBackupsPanel::on_policy_field_edited(wxCommandEvent&) {
    if (loading_policy_) {
        return;
    }
    if (preview_ready_) {
        reset_cleanup_preview();
    }
}

void BackofficeBackupsPanel::on_save_policy(wxCommandEvent&) {
    if (op_busy_ || !services_.backup_service) {
        return;
    }
    if (keypad_ && keypad_->IsShown()) {
        keypad_->hide_keypad(false);
    }

    int keep_last = 0;
    int keep_days = 0;
    if (!parse_strict_int(txt_keep_last_->GetValue(), keep_last) ||
        !parse_strict_int(txt_keep_days_->GetValue(), keep_days)) {
        const wxString msg = wxT("Escribe números enteros válidos en ambos campos (sin letras ni símbolos).");
        show_feedback(FeedbackKind::Error, msg);
        show_notice_dialog(this, wxT("Dato no válido"), msg, true);
        return;
    }

    contracts::BackupRetentionPolicyDto policy{keep_last, keep_days};
    auto res = services_.backup_service->set_retention_policy(policy);
    if (!res.is_success()) {
        const wxString err = wxString::FromUTF8(res.error_message().c_str());
        show_feedback(FeedbackKind::Error, err);
        show_notice_dialog(this, wxT("No se pudo guardar la regla"), err, true);
        return;
    }

    reset_cleanup_preview();
    show_feedback(
        FeedbackKind::Success,
        wxString::Format(
            wxT("Regla guardada: conservar al menos %d respaldo(s) y los de los últimos %d día(s)."),
            keep_last, keep_days
        )
    );
}

void BackofficeBackupsPanel::on_preview_cleanup(wxCommandEvent&) {
    if (op_busy_ || !services_.backup_service) {
        return;
    }
    if (keypad_ && keypad_->IsShown()) {
        keypad_->hide_keypad(false);
    }

    auto res = [&]() {
        BusyScope busy(*this);
        return services_.backup_service->preview_backup_cleanup();
    }();

    if (!res.is_success()) {
        const wxString err = wxString::FromUTF8(res.error_message().c_str());
        show_feedback(FeedbackKind::Error, err);
        show_notice_dialog(this, wxT("No se pudo revisar respaldos"), err, true);
        return;
    }

    const auto& dto = res.value();
    list_cleanup_->DeleteAllItems();
    for (std::size_t i = 0; i < dto.to_delete.size(); ++i) {
        const auto& item = dto.to_delete[i];
        const wxFileName fn(wxString::FromUTF8(item.path.c_str()));
        const wxString dir_label = fn.GetPath().empty() ? wxT("(carpeta actual)") : fn.GetPath();
        const long idx = list_cleanup_->InsertItem(static_cast<long>(i), fn.GetFullName());
        list_cleanup_->SetItem(idx, 1, dir_label);
        list_cleanup_->SetItem(idx, 2, format_human_datetime(item.created_at));
        const auto sz_u64 = item.size_bytes > 0 ? static_cast<std::uint64_t>(item.size_bytes) : 0ULL;
        list_cleanup_->SetItem(idx, 3, format_megabytes(sz_u64));
    }

    preview_ready_ = true;
    preview_delete_count_ = dto.to_delete.size();
    preview_bytes_freed_ = dto.bytes_freed > 0 ? static_cast<std::uint64_t>(dto.bytes_freed) : 0ULL;

    if (dto.to_delete.empty()) {
        cleanup_summary_text_ = wxString::Format(
            wxT("No hay respaldos antiguos por borrar con esta regla (se conservan %d)."),
            dto.kept_count
        );
        lbl_cleanup_summary_->SetLabel(cleanup_summary_text_);
        show_feedback(FeedbackKind::Info, wxT("No hay respaldos antiguos para borrar."));
    } else {
        cleanup_summary_text_ = wxString::Format(
            wxT("Se borrarían %zu respaldo(s) antiguo(s) para liberar %s. Se conservarán %d respaldo(s)."),
            dto.to_delete.size(),
            format_megabytes(preview_bytes_freed_),
            dto.kept_count
        );
        lbl_cleanup_summary_->SetLabel(cleanup_summary_text_);
        hide_feedback();
    }

    auto_size_cleanup_columns();
    sync_action_states();
    card_cleanup_->Layout();
    update_scroll_layout();
}

void BackofficeBackupsPanel::on_execute_cleanup(wxCommandEvent&) {
    if (op_busy_ || !preview_ready_ || preview_delete_count_ == 0 || !services_.backup_service) {
        return;
    }
    if (keypad_ && keypad_->IsShown()) {
        keypad_->hide_keypad(false);
    }

    BusyScope busy(*this);
    const wxString confirm_body = wxString::Format(
        wxT("Se borrarán %zu respaldo(s) antiguo(s) registrados por el programa (incluidos los guardados en otras carpetas o USB si aparecen en la lista) y se liberarán %s.\n\n")
        wxT("Siempre se conserva el respaldo más reciente de cada tipo. Esta acción no se puede deshacer."),
        preview_delete_count_,
        format_megabytes(preview_bytes_freed_)
    );

    ConfirmActionDialog confirm_dlg(
        this,
        wxT("Confirmar limpieza de respaldos"),
        wxT("¿Borrar los respaldos antiguos?"),
        confirm_body,
        wxT("Cancelar"),
        wxT("Sí, borrar respaldos"),
        true
    );
    if (confirm_dlg.ShowModal() != wxID_OK) {
        return;
    }

    const auto res = services_.backup_service->cleanup_old_backups();

    if (!res.is_success()) {
        const wxString err = wxString::FromUTF8(res.error_message().c_str());
        show_feedback(FeedbackKind::Error, err);
        show_notice_dialog(this, wxT("No se pudo completar la limpieza"), err, true);
        return;
    }

    const auto& dto = res.value();
    const auto freed_u64 = dto.bytes_freed > 0 ? static_cast<std::uint64_t>(dto.bytes_freed) : 0ULL;
    const wxString freed_str = format_megabytes(freed_u64);
    wxString summary = wxString::Format(
        wxT("Limpieza completada: se borraron %zu respaldo(s) y se liberaron %s."),
        dto.to_delete.size(),
        freed_str
    );
    if (!dto.failed.empty()) {
        summary += wxString::Format(wxT(" No se pudieron borrar %zu archivo(s)."), dto.failed.size());
    }
    if (!dto.changed.empty()) {
        summary += wxString::Format(wxT(" %zu archivo(s) cambiaron antes de borrarse y no se eliminaron."), dto.changed.size());
    }

    reset_cleanup_preview();
    refresh_status();
    show_feedback(!dto.failed.empty() ? FeedbackKind::Error : FeedbackKind::Success, summary);
    show_notice_dialog(this, wxT("Limpieza de respaldos"), summary, !dto.failed.empty());
}

wxString BackofficeBackupsPanel::format_human_datetime(const std::string& raw) {
    if (raw.empty() || raw == "Nunca") {
        return wxT("Nunca");
    }
    // Esperado: "YYYY-MM-DD HH:MM:SS" o "YYYY-MM-DDTHH:MM:SS-06:00"
    if (raw.size() >= 19 && raw[4] == '-' && raw[7] == '-' && (raw[10] == 'T' || raw[10] == ' ')) {
        const wxString yyyy = wxString::FromUTF8(raw.substr(0, 4).c_str());
        const wxString mm = wxString::FromUTF8(raw.substr(5, 2).c_str());
        const wxString dd = wxString::FromUTF8(raw.substr(8, 2).c_str());
        const wxString hms = wxString::FromUTF8(raw.substr(11, 8).c_str());
        return dd + wxT("/") + mm + wxT("/") + yyyy + wxT(" ") + hms;
    }
    if (raw.size() >= 16 && raw[4] == '-' && raw[7] == '-') {
        const wxString yyyy = wxString::FromUTF8(raw.substr(0, 4).c_str());
        const wxString mm = wxString::FromUTF8(raw.substr(5, 2).c_str());
        const wxString dd = wxString::FromUTF8(raw.substr(8, 2).c_str());
        const wxString hm = wxString::FromUTF8(raw.substr(11, 5).c_str());
        return dd + wxT("/") + mm + wxT("/") + yyyy + wxT(" ") + hm;
    }
    return wxString::FromUTF8(raw.c_str());
}

wxString BackofficeBackupsPanel::format_megabytes(std::uint64_t bytes) {
    if (bytes == 0) {
        return wxT("0.00 MB");
    }
    const std::uint64_t centi_mb = (bytes * 100ULL + 524288ULL) / 1048576ULL;
    if (centi_mb == 0) {
        return wxT("< 0.01 MB");
    }
    return wxString::Format(wxT("%llu.%02llu MB"), centi_mb / 100ULL, centi_mb % 100ULL);
}

} // namespace app::frontend
