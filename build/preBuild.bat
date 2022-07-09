echo on
subwcrev.exe ../ "../src/version.temp.h" "../src/version.h"
copy "..\src\res\tds.temp.rc" "..\src\res\tds.rc"


for /F %%i in ('svn info --show-item  revision') do set svnVersion=%%i
echo %svnVersion%
set buildDate=%date:~0,4%%date:~5,2%%date:~8,2%
echo %buildDate%

set buildTag=TDS_V1.0.%svnVersion%_%buildDate%
echo %buildTag%

.\tds -m replace -path ../src/res/tds.rc -os CI_VERSION_TAG -ns %buildTag%
echo %buildTag%