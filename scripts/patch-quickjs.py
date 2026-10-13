from pathlib import Path

source = Path('dependencies/quickjs/quickjs.c')
text = source.read_text()
text = text.replace('#if defined(__APPLE__)\n    return malloc_size(ptr);', '#if defined(__SWITCH__)\n    return 0; /* newlib does not expose malloc_usable_size */\n#elif defined(__APPLE__)\n    return malloc_size(ptr);')
text = text.replace('res = -tm.tm_gmtoff / 60;', '#if defined(__SWITCH__)\n        res = 0; /* epoch time remains UTC; platform has no tm_gmtoff */\n#else\n        res = -tm.tm_gmtoff / 60;\n#endif')
source.write_text(text)
