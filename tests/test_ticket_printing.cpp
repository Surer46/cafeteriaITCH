#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cassert>

#include <wx/wx.h>
#include <wx/init.h>
#include <wx/dcmemory.h>
#include <wx/bitmap.h>

#include "src/frontend/ticket_layout.hpp"
#include "app/contracts/sale_service.hpp"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FALLO: " << msg << " (" << #cond << ") en " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace app::contracts;
using namespace app::frontend;

TicketDto create_short_ticket() {
    TicketDto t;
    t.folio = "V-10001";
    t.created_at = "2026-09-30 14:30:00";
    t.business_name = "Cafetería ITCH II";
    t.cashier_name = "Cajero Turno Matutino";
    t.payment_method = PaymentMethod::Efectivo;

    TicketLineItemDto item;
    item.product_name = "Torta de Jamon";
    item.quantity = 1;
    item.unit_price = Money::from_major_minor(45, 0);
    item.subtotal = Money::from_major_minor(45, 0);
    item.notes = "Sin cebolla";
    t.items.push_back(item);

    t.subtotal = Money::from_major_minor(45, 0);
    t.discount = Money::zero();
    t.total = Money::from_major_minor(45, 0);
    t.cash_received = Money::from_major_minor(100, 0);
    t.change = Money::from_major_minor(55, 0);

    return t;
}

TicketDto create_long_ticket() {
    TicketDto t;
    t.folio = "V-88888";
    t.created_at = "2026-09-30 18:45:22";
    t.business_name = "Cafetería Tecnológica ITCH II — Campus Central Especialidad Gastronómica";
    t.cashier_name = "Lic. Juan Francisco De La Mora Y Mendoza (Nombre histórico no disponible)";
    t.payment_method = PaymentMethod::Efectivo;

    for (int i = 1; i <= 15; ++i) {
        TicketLineItemDto item;
        item.product_name = "Platillo Especial Gourmet Combinado #" + std::to_string(i) + " con Guarnicion Doble";
        item.quantity = i;
        item.unit_price = Money::from_major_minor(65 + i * 5, 50);
        item.subtotal = Money::from_cents(item.unit_price.cents() * i);
        item.notes = "Nota especial " + std::to_string(i) + ": Con salsa verde tatemada aparte, sin crema, bien dorado y servido caliente";
        t.items.push_back(item);
    }

    // Huge item
    TicketLineItemDto huge_item;
    huge_item.product_name = "Servicio de Catering y Banquete Institucional";
    huge_item.quantity = 1000000;
    huge_item.unit_price = Money::from_major_minor(200000, 0);
    huge_item.subtotal = Money::from_major_minor(200000000000LL, 0);
    huge_item.notes = "Aprobado por comité directivo escolar";
    t.items.push_back(huge_item);

    t.subtotal = Money::from_major_minor(200000010500LL, 0);
    t.discount = Money::from_major_minor(500, 0);
    t.total = Money::from_major_minor(200000010000LL, 0);
    t.cash_received = Money::from_major_minor(210000000000LL, 0);
    t.change = Money::from_major_minor(9999990000LL, 0);

    return t;
}

void test_short_ticket_rendering() {
    std::cout << "[TEST 1] Ticket corto (1 página) en área estándar (400x600 px)..." << std::endl;

    wxBitmap bmp(400, 600);
    wxMemoryDC dc(bmp);

    TicketDto t = create_short_ticket();
    TicketPrintLayout layout(t);

    wxRect printable(0, 0, 400, 600);
    layout.calculate_layout(dc, printable, 10, 10);

    TEST_CHECK(layout.get_total_pages() == 1, "Ticket corto debe requerir exactamente 1 página");
    TEST_CHECK(layout.get_columns() >= 20, "Columnas calculadas deben ser al menos 20");

    bool ok_p1 = layout.draw_page(dc, 1);
    TEST_CHECK(ok_p1, "draw_page en página 1 debe ser exitoso");

    bool ok_p0 = layout.draw_page(dc, 0);
    TEST_CHECK(!ok_p0, "draw_page en página 0 debe fallar");

    bool ok_p2 = layout.draw_page(dc, 2);
    TEST_CHECK(!ok_p2, "draw_page en página 2 para ticket de 1 página debe fallar");

    // Verificar que el rango de página 1 cubra todas las líneas
    auto range = layout.get_page_range(1);
    TEST_CHECK(range.start_line == 0, "Página 1 debe iniciar en línea 0");
    TEST_CHECK(range.end_line == layout.get_lines().size(), "Página 1 de ticket corto debe terminar en la última línea");

    std::cout << "  -> Página 1: líneas [" << range.start_line << ", " << range.end_line 
              << ") de " << layout.get_lines().size() << " totales. OK." << std::endl;
}

void test_multipage_ticket_invariants() {
    std::cout << "[TEST 2] Ticket multipágina: encabezados largos, notas extensas, importes grandes..." << std::endl;

    wxBitmap bmp(450, 500);
    wxMemoryDC dc(bmp);

    TicketDto t = create_long_ticket();
    TicketPrintLayout layout(t);

    wxRect printable(0, 0, 450, 500);
    layout.calculate_layout(dc, printable, 10, 10);

    int total_pages = layout.get_total_pages();
    TEST_CHECK(total_pages > 1, "Ticket extenso en área de 500px debe requerir múltiples páginas");
    std::cout << "  -> Total de páginas calculadas: " << total_pages << " para " 
              << layout.get_lines().size() << " líneas (" << layout.get_lines_per_page() << " líneas/página)" << std::endl;

    size_t lines_drawn = 0;
    size_t prev_end = 0;

    for (int p = 1; p <= total_pages; ++p) {
        auto range = layout.get_page_range(p);
        TEST_CHECK(range.start_line == prev_end, "Cada línea debe aparecer exactamente una vez (sin saltos ni duplicados)");
        TEST_CHECK(range.end_line > range.start_line, "Cada página debe contener al menos una línea");
        TEST_CHECK(range.end_line <= layout.get_lines().size(), "Rango de página no debe exceder total de líneas");

        size_t page_lines = range.end_line - range.start_line;
        lines_drawn += page_lines;
        prev_end = range.end_line;

        // Comprobación geométrica: el contenido no debe solaparse con el pie de página
        int content_bottom_y = layout.get_content_rect().y + static_cast<int>(page_lines) * layout.get_line_height();
        int footer_y = layout.get_footer_rect().y;
        TEST_CHECK(content_bottom_y <= footer_y, 
                   "El contenido dibujado nunca debe solaparse con el pie de página");
        TEST_CHECK(footer_y + layout.get_line_height() <= printable.y + printable.height,
                   "El pie de página no debe rebasar el área imprimible");

        // Renderizar página
        bool ok = layout.draw_page(dc, p);
        TEST_CHECK(ok, "Renderizado de página debe ser exitoso");
    }

    TEST_CHECK(lines_drawn == layout.get_lines().size(), 
               "La suma de líneas de todas las páginas debe ser exactamente igual al total de líneas");
    std::cout << "  -> Invariante de líneas y separación de pie de página verificada al 100%." << std::endl;
}

void test_various_printable_areas() {
    std::cout << "[TEST 3] Áreas imprimibles de distintos tamaños (58mm, 80mm, altura reducida)..." << std::endl;

    TicketDto t = create_long_ticket();

    // Caso 3.1: Impresora estrecha tipo 58mm (240px de ancho)
    {
        wxBitmap bmp(240, 600);
        wxMemoryDC dc(bmp);
        TicketPrintLayout layout(t);
        layout.calculate_layout(dc, wxRect(0, 0, 240, 600), 6, 8);

        TEST_CHECK(layout.get_columns() <= 30, "En 240px de ancho, las columnas deben ser <= 30");
        int usable_w = 240 - 12;

        for (const auto& line : layout.get_lines()) {
            wxCoord lw = 0, lh = 0;
            dc.GetTextExtent(wxString::FromUTF8(line.c_str()), &lw, &lh);
            TEST_CHECK(lw <= usable_w + 4, "Ninguna línea debe salirse del área útil en 58mm");
        }

        for (int p = 1; p <= layout.get_total_pages(); ++p) {
            TEST_CHECK(layout.draw_page(dc, p), "Renderizado en 58mm debe ser exitoso");
        }
        std::cout << "  -> Formato 58mm (240px ancho, " << layout.get_columns() << " cols, " 
                  << layout.get_total_pages() << " páginas): OK." << std::endl;
    }

    // Caso 3.2: Impresora ancha tipo 80mm (480px de ancho)
    {
        wxBitmap bmp(480, 800);
        wxMemoryDC dc(bmp);
        TicketPrintLayout layout(t);
        layout.calculate_layout(dc, wxRect(0, 0, 480, 800), 12, 12);

        TEST_CHECK(layout.get_columns() >= 40, "En 480px de ancho, las columnas deben ser >= 40");
        int usable_w = 480 - 24;

        for (const auto& line : layout.get_lines()) {
            wxCoord lw = 0, lh = 0;
            dc.GetTextExtent(wxString::FromUTF8(line.c_str()), &lw, &lh);
            TEST_CHECK(lw <= usable_w + 4, "Ninguna línea debe salirse del área útil en 80mm");
        }

        for (int p = 1; p <= layout.get_total_pages(); ++p) {
            TEST_CHECK(layout.draw_page(dc, p), "Renderizado en 80mm debe ser exitoso");
        }
        std::cout << "  -> Formato 80mm (480px ancho, " << layout.get_columns() << " cols, " 
                  << layout.get_total_pages() << " páginas): OK." << std::endl;
    }

    // Caso 3.3: Altura reducida (200px)
    {
        wxBitmap bmp(400, 200);
        wxMemoryDC dc(bmp);
        TicketPrintLayout layout(t);
        layout.calculate_layout(dc, wxRect(0, 0, 400, 200), 8, 8);

        TEST_CHECK(layout.get_total_pages() > 5, "Altura reducida de 200px debe generar > 5 páginas");
        for (int p = 1; p <= layout.get_total_pages(); ++p) {
            TEST_CHECK(layout.draw_page(dc, p), "Renderizado con altura reducida debe ser exitoso");
        }
        std::cout << "  -> Altura reducida (200px alto, " << layout.get_total_pages() << " páginas): OK." << std::endl;
    }
}

void test_ticket_printout_lifecycle() {
    std::cout << "[TEST 4] TicketPrintout ciclo de vida (GetPageInfo, HasPage, OnPrintPage)..." << std::endl;

    wxBitmap bmp(420, 600);
    wxMemoryDC dc(bmp);

    TicketDto t = create_long_ticket();
    TicketPrintout printout(t, wxT("Ticket Test Unit"));

    // Simular inicialización del printout por wxWidgets
    printout.SetUp(dc);
    printout.OnPreparePrinting();

    int minP = 0, maxP = 0, fromP = 0, toP = 0;
    printout.GetPageInfo(&minP, &maxP, &fromP, &toP);

    int total_pages = printout.get_layout().get_total_pages();
    TEST_CHECK(minP == 1, "minPage debe ser 1");
    TEST_CHECK(maxP == total_pages, "maxPage debe coincidir con total_pages de layout");
    TEST_CHECK(fromP == 1 && toP == total_pages, "Rango de páginas seleccionado debe cubrir todas las páginas");

    // HasPage
    TEST_CHECK(!printout.HasPage(0), "HasPage(0) debe ser false");
    TEST_CHECK(!printout.HasPage(total_pages + 1), "HasPage(total_pages + 1) debe ser false");
    for (int p = 1; p <= total_pages; ++p) {
        TEST_CHECK(printout.HasPage(p), "HasPage(p) debe ser true para páginas válidas");
    }

    // OnBeginDocument
    bool begin_ok = printout.OnBeginDocument(1, total_pages);
    TEST_CHECK(begin_ok, "OnBeginDocument debe ser exitoso");

    // OnPrintPage
    for (int p = 1; p <= total_pages; ++p) {
        bool print_ok = printout.OnPrintPage(p);
        TEST_CHECK(print_ok, "OnPrintPage debe ser exitoso para cada página válida");
    }

    TEST_CHECK(!printout.OnPrintPage(total_pages + 1), "OnPrintPage para página fuera de rango debe retornar false");
    std::cout << "  -> Ciclo de vida de TicketPrintout consistente y verificado al 100%." << std::endl;
}

void test_physical_boundary_limits() {
    std::cout << "[TEST 5] Límites físicos: anchos insuficientes, alturas ínfimas y márgenes excesivos..." << std::endl;

    TicketDto t = create_short_ticket();

    // Caso 5.1: Ancho físicamente insuficiente (< 20 columnas)
    {
        wxBitmap bmp(60, 400);
        wxMemoryDC dc(bmp);
        TicketPrintLayout layout(t);
        layout.calculate_layout(dc, wxRect(0, 0, 60, 400), 4, 4);

        TEST_CHECK(!layout.is_valid(), "Layout con 60px de ancho no debe ser válido (no caben 20 columnas)");
        TEST_CHECK(layout.get_total_pages() == 0, "Páginas totales deben ser 0 si layout es inválido");
        TEST_CHECK(!layout.draw_page(dc, 1), "draw_page en layout inválido debe retornar false sin dibujar");
        TEST_CHECK(!layout.get_error_message().empty(), "Debe existir un mensaje explicativo del error de layout");
        std::cout << "  -> Caso 5.1 Ancho insuficiente detectado correctamente: " << layout.get_error_message() << std::endl;
    }

    // Caso 5.2: Altura físicamente insuficiente (< 1 línea)
    {
        wxBitmap bmp(400, 10);
        wxMemoryDC dc(bmp);
        TicketPrintLayout layout(t);
        layout.calculate_layout(dc, wxRect(0, 0, 400, 10), 4, 4);

        TEST_CHECK(!layout.is_valid(), "Layout con 10px de alto no debe ser válido (no cabe ni 1 línea)");
        TEST_CHECK(layout.get_total_pages() == 0, "Páginas totales deben ser 0 si altura es insuficiente");
        TEST_CHECK(!layout.draw_page(dc, 1), "draw_page en layout con altura insuficiente debe retornar false");
        std::cout << "  -> Caso 5.2 Altura insuficiente detectada correctamente: " << layout.get_error_message() << std::endl;
    }

    // Caso 5.3: Márgenes que agotan el área y no permiten 20 columnas útiles
    {
        wxBitmap bmp(100, 100);
        wxMemoryDC dc(bmp);
        TicketPrintLayout layout(t);
        // Márgenes excesivos en rectángulo pequeño
        layout.calculate_layout(dc, wxRect(0, 0, 100, 100), 45, 45);

        TEST_CHECK(!layout.is_valid(), "Layout con área útil remanente < 20 cols debe ser inválido");
        TEST_CHECK(layout.get_total_pages() == 0, "Páginas deben ser 0");
        TEST_CHECK(!layout.draw_page(dc, 1), "draw_page debe ser false");
        std::cout << "  -> Caso 5.3 Márgenes excesivos manejados limpiamente: " << layout.get_error_message() << std::endl;
    }

    // Caso 5.4: TicketPrintout en lienzo dimensionalmente inválido
    {
        wxBitmap bmp(50, 50);
        wxMemoryDC dc(bmp);
        TicketPrintout printout(t, wxT("Ticket Inválido"));
        printout.SetUp(dc);
        printout.OnPreparePrinting();

        int minP = -1, maxP = -1, fromP = -1, toP = -1;
        printout.GetPageInfo(&minP, &maxP, &fromP, &toP);
        TEST_CHECK(minP == 0 && maxP == 0, "GetPageInfo debe reportar 0 páginas para lienzo inválido");
        TEST_CHECK(!printout.HasPage(1), "HasPage(1) debe ser false para lienzo inválido");
        TEST_CHECK(!printout.OnBeginDocument(1, 1), "OnBeginDocument debe abortar (false) en lienzo inválido");
        TEST_CHECK(!printout.OnPrintPage(1), "OnPrintPage debe fallar limpiamente (false) sin dibujar");
        std::cout << "  -> Caso 5.4 TicketPrintout aborta limpiamente ante geometría física inviable. OK." << std::endl;
    }
}

void test_thermal_paper_profiles() {
    std::cout << "[TEST 6] Perfiles térmicos nominales 55 mm y 80 mm (distinción nominal vs DC)..." << std::endl;

    TicketDto t = create_short_ticket();
    t.items[0].product_name = "Torta Especial Ahogada con Salsa Muy Picante";
    t.items[0].notes = "sin cebolla, con extra picante y limon aparte";

    // 6.1 Perfil térmico 55 mm (nominal 55 mm, 28 columnas)
    {
        wxBitmap bmp(300, 600);
        wxMemoryDC dc(bmp);

        TicketPrintLayout layout55(t, ThermalPaperProfile::Profile55mm);
        TEST_CHECK(layout55.get_profile() == ThermalPaperProfile::Profile55mm, "Perfil debe ser Profile55mm");

        wxRect printable(0, 0, 300, 600);
        layout55.calculate_layout(dc, printable, 8, 8);

        TEST_CHECK(layout55.is_valid(), "Layout 55 mm debe ser válido");
        TEST_CHECK(layout55.get_columns() == 28, "Perfil 55 mm debe calcular exactamente 28 columnas nominales");

        // Comprobar que ninguna línea supere 28 columnas utf8
        for (const auto& line : layout55.get_lines()) {
            size_t w = TicketDto::utf8_column_width(line);
            TEST_CHECK(w <= 28, "Línea en perfil 55 mm no debe exceder 28 columnas: " + line);
        }

        // Renderizado en DC sin errores
        TEST_CHECK(layout55.draw_page(dc, 1), "draw_page para perfil 55 mm debe retornar true");
        std::cout << "  -> Caso 6.1 Perfil 55 mm nominal verificado: 28 cols, wrapping exacto sin desbordamiento. OK." << std::endl;
    }

    // 6.2 Perfil térmico 80 mm (nominal 80 mm, 42 columnas)
    {
        wxBitmap bmp(450, 600);
        wxMemoryDC dc(bmp);

        TicketPrintLayout layout80(t, ThermalPaperProfile::Profile80mm);
        TEST_CHECK(layout80.get_profile() == ThermalPaperProfile::Profile80mm, "Perfil debe ser Profile80mm");

        wxRect printable(0, 0, 450, 600);
        layout80.calculate_layout(dc, printable, 10, 10);

        TEST_CHECK(layout80.is_valid(), "Layout 80 mm debe ser válido");
        TEST_CHECK(layout80.get_columns() == 42, "Perfil 80 mm debe calcular exactamente 42 columnas nominales");

        for (const auto& line : layout80.get_lines()) {
            size_t w = TicketDto::utf8_column_width(line);
            TEST_CHECK(w <= 42, "Línea en perfil 80 mm no debe exceder 42 columnas: " + line);
        }

        TEST_CHECK(layout80.draw_page(dc, 1), "draw_page para perfil 80 mm debe retornar true");
        std::cout << "  -> Caso 6.2 Perfil 80 mm nominal verificado: 42 cols, renderizado correcto. OK." << std::endl;
    }

    // 6.3 Alternar perfil de impresión no altera datos históricos del TicketDto
    {
        TicketDto before = t;
        std::string txt55 = t.to_formatted_text(28);
        std::string txt80 = t.to_formatted_text(42);

        TEST_CHECK(t.folio == before.folio, "Folio inmutable al formatear en distintos perfiles");
        TEST_CHECK(t.total == before.total, "Total inmutable");
        TEST_CHECK(t.items.size() == before.items.size(), "Items inmutables");
        TEST_CHECK(t.items[0].product_name == before.items[0].product_name, "Nombre de producto inmutable");
        TEST_CHECK(t.items[0].notes == before.items[0].notes, "Notas de producto inmutables");
        TEST_CHECK(t.cashier_name == before.cashier_name, "Cajero histórico inmutable");

        // El texto a 55 columnas debe ser distinto del texto a 80 columnas debido al ajuste de ancho
        TEST_CHECK(txt55 != txt80, "Formato 55 mm y 80 mm deben tener anchos y wrapping distintos");
        std::cout << "  -> Caso 6.3 Alternancia de perfil conserva inmutabilidad de datos históricos. OK." << std::endl;
    }
}

class TestApp : public wxApp {
public:
    bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(TestApp);

int main(int argc, char** argv) {
    wxApp::SetInstance(new TestApp());
    if (!wxEntryStart(argc, argv)) {
        std::cerr << "ERROR: Falló wxEntryStart para inicializar subsistema gráfico." << std::endl;
        return 1;
    }

    if (wxTheApp && !wxTheApp->CallOnInit()) {
        std::cerr << "ERROR: Falló CallOnInit de wxTheApp." << std::endl;
        wxEntryCleanup();
        return 1;
    }

    std::cout << "=== PRUEBAS DE DISEÑO Y RENDERIZADO DE TICKETS (Debug y Release) ===" << std::endl;
    test_short_ticket_rendering();
    test_multipage_ticket_invariants();
    test_various_printable_areas();
    test_ticket_printout_lifecycle();
    test_physical_boundary_limits();
    test_thermal_paper_profiles();
    std::cout << "\n>>> TODAS LAS PRUEBAS DE IMPRESIÓN Y RENDERIZADO PASARON CON ÉXITO! <<<\n" << std::endl;

    wxEntryCleanup();
    return 0;
}
