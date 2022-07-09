echo on
subwcrev.exe ../ "../src/version.temp.h" "../src/version.h"
copy "..\src\res\tds.temp.rc" "..\src\res\tds.rc"
replaceFileContent.bat
