from pathlib import Path
import json
r=Path(__file__).resolve().parent
(r/'src/file_page.h').write_text('/* Generated from file_page.html. */\nstatic const char FILE_PAGE[] = '+json.dumps((r/'file_page.html').read_text(encoding='utf-8'),ensure_ascii=False)+';\n', encoding='utf-8')
