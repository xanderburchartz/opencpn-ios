# Draait de vier fixes in de master-reeks terug (voor-build) of zet ze terug (na).
import sys
R = [
 ("gui/src/ocpn_frame.cpp",
  "  int monitor_idx = wxDisplay::GetFromWindow(this);\n  g_current_monitor = monitor_idx == wxNOT_FOUND ? 0 : monitor_idx;\n",
  "  g_current_monitor = wxDisplay::GetFromWindow(this);  // VOOR-PROEF\n"),
 ("gui/src/chcanv.cpp",
  "        int dx = x - wxRound(m_touchdownPos.x / m_displayScale);\n        int dy = y - wxRound(m_touchdownPos.y / m_displayScale);\n",
  "        int dx = x - m_touchdownPos.x;  // VOOR-PROEF\n        int dy = y - m_touchdownPos.y;\n"),
 ("gui/src/chcanv.cpp",
  "          int mdx = x - wxRound(m_touchdownPos.x / m_displayScale);\n          int mdy = y - wxRound(m_touchdownPos.y / m_displayScale);\n",
  "          int mdx = x - m_touchdownPos.x;  // VOOR-PROEF\n          int mdy = y - m_touchdownPos.y;\n"),
 ("libs/s52plib/src/s52plib.cpp",
  "  if (m_useGLSL && !m_pdc)\n    return RenderCARC_GLSL",
  "  if (m_useGLSL)  // VOOR-PROEF\n    return RenderCARC_GLSL"),
 ("gui/src/options.cpp",
  "      pDistanceFormats[i] = wxGetTranslation(kDistanceFormats[i].label);\n",
  "      pDistanceFormats[i] = kDistanceFormats[i].label;  // VOOR-PROEF\n"),
]
# Include the enum value so only the kDistanceFormats table matches (other tables in
# options.cpp also start with {_("Nautical miles"), ...).
for name, enum in (("Nautical miles", "DISTANCE_NMI"), ("Statute miles", "DISTANCE_MI"),
                   ("Kilometers", "DISTANCE_KM"), ("Meters", "DISTANCE_M")):
  R.append(("gui/src/options.cpp", f'{{wxTRANSLATE("{name}"), {enum}}}',
            f'{{_("{name}"), {enum}}}'))
mode = sys.argv[1]
for f, a, b in R:
  s = open(f).read()
  old, new = (a, b) if mode == "voor" else (b, a)
  n = s.count(old)
  assert n >= 1, (f, old)
  open(f, "w").write(s.replace(old, new))
  print(mode, f, n)
