"""Force the app's framework hints to Chinese independent of console locale."""
from pathlib import Path
p=Path('dependencies/nx-player/library/borealis/library/lib/core/i18n.cpp')
s=p.read_text()
old='std::string currentLocaleName = Application::getLocale();'
new='std::string currentLocaleName = LOCALE_ZH_HANS; // Hongguo Chinese UI'
if new not in s:
    assert s.count(old)==1
    p.write_text(s.replace(old,new))
