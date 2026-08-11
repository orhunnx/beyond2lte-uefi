wpeinit
if exist C:\ goto var
if exist D:\ goto var
if exist E:\ goto var
if exist F:\ goto var
if exist G:\ goto var
goto yok
:var
ping -n 31 127.0.0.1 >nul
goto son
:yok
ping -n 6 127.0.0.1 >nul
:son
