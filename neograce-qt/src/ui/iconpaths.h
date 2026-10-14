// Сгенерировано из векторных иконок (линейный стиль 24×24)
#pragma once
#include <QHash>
#include <QString>

namespace ng {
inline const QHash<QString, QString> &iconPaths()
{
    static const QHash<QString, QString> p = {
        {"new", R"(<path d="M6 3h8l5 5v11a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M14 3v5h5"/><path d="M12 11v6M9 14h6"/>)"},
        {"open", R"(<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>)"},
        {"save", R"(<path d="M5 3h11l4 4v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M7 3v5h8V3"/><path d="M7 21v-7h10v7"/>)"},
        {"export", R"(<path d="M12 3v12"/><path d="M7 8l5-5 5 5"/><path d="M5 14v5a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-5"/>)"},
        {"import", R"(<path d="M12 3v12"/><path d="M7 10l5 5 5-5"/><path d="M5 14v5a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-5"/>)"},
        {"undo", R"(<path d="M9 14L4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"/>)"},
        {"redo", R"(<path d="M15 14l5-5-5-5"/><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13"/>)"},
        {"select", R"(<path d="M6 3l12 9-5.5 1.2L9.5 19z" stroke-linejoin="round"/>)"},
        {"zoom", R"(<circle cx="10.5" cy="10.5" r="6.5"/><path d="M20.5 20.5l-5.4-5.4"/><rect x="7.5" y="8" width="6" height="5" rx="0.5" stroke-dasharray="2 1.5"/>)"},
        {"pan", R"(<path d="M12 3v18M3 12h18"/><path d="M9 6l3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3"/>)"},
        {"text", R"(<path d="M5 6V4h14v2"/><path d="M12 4v16"/><path d="M9 20h6"/>)"},
        {"line", R"(<path d="M5 19L19 5"/><path d="M12 5h7v7"/>)"},
        {"box", R"(<rect x="4" y="6" width="16" height="12" rx="1.5"/>)"},
        {"ellipse", R"(<ellipse cx="12" cy="12" rx="9" ry="6.5"/>)"},
        {"autoscale", R"(<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/><path d="M7.5 15.5c2-5 3.5-7 5-7s2 3 4 3" stroke-width="1.6"/>)"},
        {"addgraph", R"(<path d="M4 3v17h17"/><path d="M7 16l4-5 3 3 5-7" stroke-width="1.6"/><path d="M17 16h5M19.5 13.5v5" />)"},
        {"table", R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9.5h18M3 15h18M9.5 4v16"/>)"},
        {"trash", R"(<path d="M4 7h16"/><path d="M6 7l1 13h10l1-13"/><path d="M9 7V4h6v3"/><path d="M10 11v5M14 11v5"/>)"},
        {"fit", R"(<path d="M4 14v6h6M20 10V4h-6"/><path d="M14 10l6-6M4 20l6-6"/>)"},
        {"sidebar", R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>)"},
        {"inspector", R"(<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M15 4v16"/>)"},
        {"function", R"(<path d="M14 4h-1.5A2.5 2.5 0 0 0 10 6.5V18a2 2 0 0 1-2 2H6"/><path d="M7 10h7"/><path d="M15 13l5 6M20 13l-5 6"/>)"},
        {"fit_curve", R"(<circle cx="6" cy="16" r="1.2"/><circle cx="10" cy="11" r="1.2"/><circle cx="14" cy="12" r="1.2"/><circle cx="18" cy="6" r="1.2"/><path d="M3 19L21 4" stroke-dasharray="3 2"/>)"},
        {"palette", R"(<path d="M12 3a9 9 0 1 0 0 18c1.2 0 2-.8 2-1.8 0-1.3-1-1.5-1-2.7 0-1 .8-1.5 1.8-1.5H17a4 4 0 0 0 4-4c0-4.4-4-8-9-8z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7" r="1"/><circle cx="15" cy="7.5" r="1"/>)"},
        {"eye", R"(<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>)"},
        {"copy", R"(<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M5 16H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h11a1 1 0 0 1 1 1v1"/>)"},
        {"plus", R"(<path d="M12 5v14M5 12h14"/>)"},
        {"minus", R"(<path d="M5 12h14"/>)"},
        {"paste", R"(<rect x="6" y="4" width="12" height="17" rx="2"/><path d="M9 4V3h6v1"/><path d="M9 10h6M9 14h6"/>)"},
        {"sort", R"(<path d="M7 4v16M4 17l3 3 3-3"/><path d="M13 6h8M13 11h6M13 16h4"/>)"},
        {"close", R"(<path d="M6 6l12 12M18 6L6 18"/>)"},
        {"page", R"(<rect x="5" y="3" width="14" height="18" rx="1.5"/><path d="M8 8h8M8 12h8M8 16h5"/>)"},
        {"graph", R"(<path d="M4 3v17h17"/><path d="M7 15l4-5 3 3 5-7" stroke-width="1.6"/>)"},
        {"set", R"(<path d="M3 17c3-8 6-10 9-5s6 3 9-5" stroke-width="1.8"/>)"},
        {"shapes", R"(<rect x="3" y="11" width="9" height="9" rx="1"/><circle cx="16.5" cy="7.5" r="4.5"/>)"},
        {"wand", R"(<path d="M4 20L15 9"/><path d="M14 4v3M19 9h3M17.5 5.5l2-2M17 12l1.5 1.5"/>)"},
        {"check", R"(<path d="M5 12l5 5 9-10"/>)"},
        {"print", R"(<path d="M7 9V3h10v6"/><rect x="3" y="9" width="18" height="8" rx="2"/><path d="M7 14h10v7H7z"/>)"},
        {"arrange", R"(<rect x="3" y="3" width="8" height="8" rx="1.5"/><rect x="13" y="3" width="8" height="8" rx="1.5"/><rect x="3" y="13" width="8" height="8" rx="1.5"/><rect x="13" y="13" width="8" height="8" rx="1.5"/>)"},
        {"legend", R"(<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M6 10h4M6 14h4M12 10h6M12 14h6"/>)"},
        {"axis", R"(<path d="M5 3v16h16"/><path d="M5 8h2M5 13h2M10 19v-2M15 19v-2"/>)"},
        {"title", R"(<path d="M4 6V4h16v2"/><path d="M12 4v11"/><path d="M6 20h12"/>)"},
        {"hist", R"(<path d="M4 20V10h4v10M10 20V4h4v16M16 20v-7h4v7"/>)"},
        {"stats", R"(<path d="M4 19h16"/><path d="M6 16l4-6 4 3 5-8"/><circle cx="19" cy="5" r="1.2"/>)"},
        {"setmath", R"(<path d="M7 4v6M4 7h6"/><path d="M14 7h6"/><path d="M4.5 14.5l5 5M9.5 14.5l-5 5"/><path d="M14 17h6"/><circle cx="17" cy="14" r="0.8"/><circle cx="17" cy="20" r="0.8"/>)"},
    };
    return p;
}
}  // namespace ng
